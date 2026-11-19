/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "bt/bm78.h"
#include "config/star_settings.h"
#include "imu/lsm6ds.h"
#include "storage/effect_store.h"
#include "storage/zd35_nand.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

/* ---- imu ---------------------------------------------------------------------- */

static int cmd_imu_read(const struct shell *sh, size_t argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 1;
	struct imu_state s;

	if (imu_who_am_i() == 0) {
		shell_error(sh, "accelerometer not detected");
		return -ENODEV;
	}
	for (int i = 0; i < n && i < 100; i++) {
		imu_get(&s);
		shell_print(sh, "a=(%5d,%5d,%5d) g=(%5d,%5d,%5d) shake=%u mg", s.ax, s.ay, s.az,
			    s.gx, s.gy, s.gz, s.shake);
		if (i + 1 < n) {
			k_msleep(200);
		}
	}
	return 0;
}

static int cmd_imu_axis(const struct shell *sh, size_t argc, char **argv)
{
	struct imu_axis_map m;
	char buf[7];

	if (argc > 1) {
		if (imu_axis_map_parse(argv[1], &m) != 0) {
			shell_error(sh, "axis map like +x+y+z or -y+x+z (board x, y, z)");
			return -EINVAL;
		}
		imu_set_axis_map(&m);
		star_settings_save_imu();
	}
	imu_get_axis_map(&m);
	imu_axis_map_format(&m, buf);
	shell_print(sh, "axis map %s (board x right, y up, z out of the front)", buf);
	return 0;
}

static int cmd_imu_threshold(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		int v = atoi(argv[1]);

		if (v < 50 || v > 8000) {
			shell_error(sh, "threshold 50-8000 mg");
			return -EINVAL;
		}
		imu_set_shake_threshold((uint16_t)v);
		star_settings_save_imu();
	}
	shell_print(sh, "shake threshold %u mg", imu_get_shake_threshold());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_imu,
	SHELL_CMD_ARG(read, NULL, "Read acceleration: read [count]", cmd_imu_read, 1, 1),
	SHELL_CMD_ARG(axis, NULL, "Sensor-to-board axis map: axis [+x+y+z]", cmd_imu_axis, 1, 1),
	SHELL_CMD_ARG(threshold, NULL, "Shake threshold: threshold [mg]", cmd_imu_threshold, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(imu, &sub_imu, "Accelerometer", NULL);

/* ---- bt ----------------------------------------------------------------------- */

static int cmd_bt_status(const struct shell *sh, size_t argc, char **argv)
{
	int s1, s2;

	bm78_status(&s1, &s2);
	shell_print(sh, "BM78 status pins P1_5=%d P0_4=%d, RTS=%d", s1, s2, bm78_rts());
	return 0;
}

static int cmd_bt_reset(const struct shell *sh, size_t argc, char **argv)
{
	return bm78_reset();
}

/*
 * Diagnostics: send an IS1678 UART command frame to the BM78 and capture its
 * reply. Only usable from the J-Link (RTT) shell, because the Bluetooth UART
 * is taken over for the duration of the command.
 */
#if defined(CONFIG_SHELL_BACKEND_RTT)
#include <zephyr/drivers/uart.h>
#include <zephyr/shell/shell_uart.h>

static uint8_t bt_rx[192];
static size_t bt_rx_len;

static void bt_capture(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);
	while (len-- > 0 && bt_rx_len < sizeof(bt_rx)) {
		bt_rx[bt_rx_len++] = *data++;
	}
}

static int cmd_bt_cmd(const struct shell *sh, size_t argc, char **argv)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	const struct shell *bt_sh = shell_backend_uart_get_ptr();
	uint8_t frame[24];
	size_t n = 0;
	uint8_t sum = 0;

	if (sh == bt_sh) {
		shell_error(sh, "use this from the J-Link (RTT) shell");
		return -EPERM;
	}
	if (argc - 1 > sizeof(frame) - 4) {
		return -EINVAL;
	}
	frame[n++] = 0xAA;
	frame[n++] = 0x00;
	frame[n++] = (uint8_t)(argc - 1);
	for (size_t i = 1; i < argc; i++) {
		frame[n++] = (uint8_t)strtoul(argv[i], NULL, 16);
	}
	for (size_t i = 1; i < n; i++) {
		sum += frame[i];
	}
	frame[n++] = (uint8_t)(0x100 - sum);

	bt_rx_len = 0;
	shell_set_bypass(bt_sh, bt_capture, NULL);
	for (size_t i = 0; i < n; i++) {
		uart_poll_out(uart, frame[i]);
	}
	k_msleep(1500);
	shell_set_bypass(bt_sh, NULL, NULL);

	shell_fprintf(sh, SHELL_NORMAL, "tx:");
	for (size_t i = 0; i < n; i++) {
		shell_fprintf(sh, SHELL_NORMAL, " %02x", frame[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "\nrx (%u):", (unsigned int)bt_rx_len);
	for (size_t i = 0; i < bt_rx_len; i++) {
		shell_fprintf(sh, SHELL_NORMAL, " %02x", bt_rx[i]);
	}
	shell_print(sh, "");
	return 0;
}

/*
 * Transparent bridge RTT <-> BM78 UART, for Microchip's configuration tools
 * (sw/tools/bm78_bridge.py). Both shells stay in bypass until the firmware is
 * restarted, which also restarts the BM78 in application mode.
 */
#include <SEGGER_RTT.h>

static const struct device *const bridge_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

static void bridge_from_bt(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);
	(void)SEGGER_RTT_Write(0, data, len);
}

static void bridge_from_rtt(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(bridge_uart, data[i]);
	}
}

static int cmd_bt_bridge(const struct shell *sh, size_t argc, char **argv)
{
	const struct shell *bt_sh = shell_backend_uart_get_ptr();
	bool test_mode = argc < 2 || strcmp(argv[1], "app") != 0;

	if (sh == bt_sh) {
		shell_error(sh, "use this from the J-Link (RTT) shell");
		return -EPERM;
	}
	shell_set_bypass(bt_sh, bridge_from_bt, NULL);
	if (bm78_start(test_mode) != 0) {
		shell_set_bypass(bt_sh, NULL, NULL);
		return -EIO;
	}
	shell_print(sh, "BRIDGE READY (%s mode); restart the firmware to leave",
		    test_mode ? "test" : "app");
	shell_set_bypass(sh, bridge_from_rtt, NULL);
	return 0;
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bt,
	SHELL_CMD(status, NULL, "Module status pins", cmd_bt_status),
	SHELL_CMD(reset, NULL, "Reset the module (drops the connection)", cmd_bt_reset),
#if defined(CONFIG_SHELL_BACKEND_RTT)
	SHELL_CMD_ARG(cmd, NULL, "Send an IS1678 command (hex opcode [params]) and dump the reply",
		      cmd_bt_cmd, 2, 16),
	SHELL_CMD_ARG(bridge, NULL, "Bridge RTT <-> BM78 UART: bridge [test|app]", cmd_bt_bridge, 1, 1),
#endif
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(bt, &sub_bt, "BM78 Bluetooth module", NULL);

/* ---- nand --------------------------------------------------------------------- */

static int cmd_nand_info(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t id[3];

	nand_get_id(id);
	shell_print(sh, "ID %02x %02x %02x, %u blocks x %u KB, %d bad blocks, capacity %u frames", id[0], id[1],
		    id[2], nand_block_count(), NAND_BLOCK_SIZE / 1024,
		    effect_store_bad_blocks(), effect_store_capacity());
	return 0;
}

/* Destructive read/write test on one block (may damage the stored effect). */
static int cmd_nand_test(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t block = (uint32_t)strtoul(argv[1], NULL, 0);
	uint8_t buf[64];
	int ret;

	if (block >= nand_block_count() || nand_block_is_bad(block)) {
		shell_error(sh, "invalid or bad block");
		return -EINVAL;
	}
	if (effect_store_busy()) {
		return -EBUSY;
	}
	nand_lock();
	ret = nand_erase(block);
	for (uint32_t page = 0; ret == 0 && page < NAND_PAGES_PER_BLOCK; page += 21) {
		for (int i = 0; i < (int)sizeof(buf); i++) {
			buf[i] = (uint8_t)(block + page * 7 + i);
		}
		ret = nand_load(block, 0, buf, sizeof(buf), true);
		if (ret == 0) {
			ret = nand_load(block, NAND_PAGE_SIZE - sizeof(buf), buf, sizeof(buf), false);
		}
		if (ret == 0) {
			ret = nand_program(block, page);
		}
		for (int k = 0; ret == 0 && k < 2; k++) {
			uint8_t rd[sizeof(buf)];

			ret = nand_read(block, page, k ? NAND_PAGE_SIZE - sizeof(buf) : 0, rd, sizeof(rd));
			if (ret == 0 && memcmp(rd, buf, sizeof(buf)) != 0) {
				shell_error(sh, "mismatch at page %u part %d", page, k);
				ret = -EIO;
			}
		}
	}
	if (ret == 0) {
		ret = nand_erase(block);
	}
	nand_unlock();
	shell_print(sh, "block %u test %s (%d)", block, ret == 0 ? "OK" : "FAILED", ret);
	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_nand,
	SHELL_CMD(info, NULL, "Chip ID and geometry", cmd_nand_info),
	SHELL_CMD_ARG(test, NULL, "Erase/write/read test (destroys data): test <block>",
		      cmd_nand_test, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(nand, &sub_nand, "SPI NAND flash", NULL);

/* ---- reboot ------------------------------------------------------------------- */

static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "rebooting");
	k_msleep(100);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_CMD_REGISTER(reboot, NULL, "Restart the star", cmd_reboot);
