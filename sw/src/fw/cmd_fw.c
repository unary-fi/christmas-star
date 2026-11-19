/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Firmware update over the command line (Bluetooth or J-Link), with MCUboot:
 *   fw status            running version, confirmed or not, the second slot
 *   fw upload            receive a signed image (zephyr.signed.bin) with
 *                        XMODEM-1K into the second slot
 *   fw apply [permanent] restart into the new image; without "permanent" it
 *                        is a test: MCUboot goes back to the old firmware on
 *                        the next restart unless the new one confirms itself
 *   fw confirm           keep the running firmware
 *
 * A new firmware confirms itself FW_CONFIRM_AFTER_S seconds after starting
 * (see fw_boot_check()), so a firmware that crashes or hangs early is rolled
 * back at the next restart.
 */

#include "fw_image.h"
#include "fw_update.h"
#include "cli/cli_hooks.h"
#include "transfer/xmodem.h"
#include "effects/effect.h"
#include "led/color.h"

#include <zephyr/sys/atomic.h>

#include <zephyr/app_version.h>
#include <errno.h>
#include <string.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#define SLOT1_ID PARTITION_ID(slot1_partition)
#define SLOT1_SIZE PARTITION_SIZE(slot1_partition)
#define FW_CONFIRM_AFTER_S 20

static struct flash_img_context img;
static struct xmodem_rx fx;
static const struct shell *fw_sh;
static K_MUTEX_DEFINE(fw_mutex);
static bool fw_active;
static size_t fw_received;
static struct fw_image_info fw_info;

/* For the LEDs: -1 = no update, 0..1000 = received, FW_INSTALLING = restarting. */
#define FW_INSTALLING 1001
static atomic_t fw_phase = ATOMIC_INIT(-1);
static int fw_error;

static void fw_timeout(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(fw_timer, fw_timeout);

/* ---- Confirming a new firmware ------------------------------------------- */

static void confirm_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (!boot_is_img_confirmed()) {
		(void)boot_write_img_confirmed();
	}
}

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_fn);

void fw_boot_check(void)
{
	if (!boot_is_img_confirmed()) {
		k_work_schedule(&confirm_work, K_SECONDS(FW_CONFIRM_AFTER_S));
	}
}

/* ---- XMODEM into the second slot ----------------------------------------- */

static void fw_send(void *ctx, uint8_t c)
{
	size_t cnt;

	ARG_UNUSED(ctx);
	(void)fw_sh->iface->api->write(fw_sh->iface, &c, 1, &cnt);
}

static int fw_sink(void *ctx, const uint8_t *data, size_t len)
{
	int ret;

	ARG_UNUSED(ctx);
	if (fw_received == 0) {
		ret = fw_image_parse(data, len, SLOT1_SIZE, &fw_info);
		if (ret != 0) {
			return ret;
		}
	}
	if (fw_received + len > SLOT1_SIZE) {
		return -EFBIG;
	}
	ret = flash_img_buffered_write(&img, data, len, false);
	if (ret == 0) {
		fw_received += len;
		/* The image plus its signature trailer, roughly. */
		uint32_t total = fw_info.hdr_size + fw_info.img_size + fw_info.protect_tlv_size + 512U;

		atomic_set(&fw_phase, (atomic_val_t)MIN(1000U, fw_received * 1000U / total));
	}
	return ret;
}

static void fw_finish(enum xmodem_result r)
{
	int ret = 0;

	fw_active = false;
	k_work_cancel_delayable(&fw_timer);
	shell_set_bypass(fw_sh, NULL, NULL);
	if (r == XMODEM_DONE) {
		ret = flash_img_buffered_write(&img, NULL, 0, true);
	}
	/* Give the sender time to leave its transfer mode before printing. */
	k_msleep(200);
	if (r == XMODEM_DONE && ret == 0) {
		shell_print(fw_sh, "\nupload OK %u bytes, version %u.%u.%u+%u (fw apply to start it)",
			    (unsigned int)fw_received, fw_info.major, fw_info.minor,
			    fw_info.revision, (unsigned int)fw_info.build);
	} else if (fx.sink_error == -EINVAL) {
		shell_error(fw_sh, "\nupload failed: not a signed firmware image (zephyr.signed.bin)");
	} else if (fx.sink_error == -EFBIG) {
		shell_error(fw_sh, "\nupload failed: the image is too big for the slot");
	} else if (r == XMODEM_DONE || fx.sink_error != 0) {
		shell_error(fw_sh, "\nupload failed: flash write error (%d)", ret ? ret : fx.sink_error);
	} else {
		shell_error(fw_sh, "\nupload %s", r == XMODEM_CANCELLED ? "cancelled" : "timed out");
	}
	fw_error = (r == XMODEM_DONE && ret == 0) ? 0 : -EIO;
	/* Received: keep the ring full until `fw apply`; failed: back to the effect. */
	atomic_set(&fw_phase, fw_error == 0 ? 1000 : -1);
}

static void fw_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);

	k_mutex_lock(&fw_mutex, K_FOREVER);
	if (fw_active) {
		enum xmodem_result r = xmodem_rx_feed(&fx, data, len);

		if (r != XMODEM_RUNNING) {
			fw_finish(r);
		} else {
			k_work_reschedule(&fw_timer, K_MSEC(XMODEM_TICK_MS));
		}
	}
	k_mutex_unlock(&fw_mutex);
}

static void fw_timeout(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&fw_mutex, K_FOREVER);
	if (fw_active) {
		enum xmodem_result r = xmodem_rx_tick(&fx);

		if (r != XMODEM_RUNNING) {
			fw_finish(r);
		} else {
			k_work_reschedule(&fw_timer, K_MSEC(XMODEM_TICK_MS));
		}
	}
	k_mutex_unlock(&fw_mutex);
}

void fw_upload_abort(void)
{
	k_mutex_lock(&fw_mutex, K_FOREVER);
	if (fw_active) {
		fw_finish(XMODEM_CANCELLED);
	}
	k_mutex_unlock(&fw_mutex);
}

/* ---- Commands --------------------------------------------------------------- */

static const char *swap_name(int t)
{
	switch (t) {
	case BOOT_SWAP_TYPE_NONE: return "none";
	case BOOT_SWAP_TYPE_TEST: return "test pending";
	case BOOT_SWAP_TYPE_PERM: return "permanent pending";
	case BOOT_SWAP_TYPE_REVERT: return "revert pending";
	default: return "error";
	}
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	struct mcuboot_img_header h;

	shell_print(sh, "running %s (%s), next restart: %s", APP_VERSION_STRING,
		    boot_is_img_confirmed() ? "confirmed" : "on trial, confirms itself after 20 s",
		    swap_name(mcuboot_swap_type()));
	if (boot_read_bank_header(SLOT1_ID, &h, sizeof(h)) == 0 && h.mcuboot_version == 1) {
		shell_print(sh, "second slot: %u.%u.%u+%u, %u bytes", h.h.v1.sem_ver.major,
			    h.h.v1.sem_ver.minor, h.h.v1.sem_ver.revision,
			    (unsigned int)h.h.v1.sem_ver.build_num, (unsigned int)h.h.v1.image_size);
	} else {
		shell_print(sh, "second slot: empty");
	}
	return 0;
}

static int cmd_upload(const struct shell *sh, size_t argc, char **argv)
{
	int ret = flash_img_init_id(&img, SLOT1_ID);

	if (ret != 0) {
		shell_error(sh, "cannot open the second slot (%d)", ret);
		return ret;
	}
	shell_print(sh, "Send the firmware (zephyr.signed.bin) with XMODEM-1K (CRC) now...");
	k_mutex_lock(&fw_mutex, K_FOREVER);
	fw_sh = sh;
	fw_received = 0;
	fw_error = -EAGAIN;
	atomic_set(&fw_phase, 0);
	fw_active = true;
	shell_set_bypass(sh, fw_bypass, NULL);
	xmodem_rx_start(&fx, fw_send, fw_sink, NULL);
	k_work_reschedule(&fw_timer, K_MSEC(XMODEM_TICK_MS));
	k_mutex_unlock(&fw_mutex);
	return 0;
}

static void reboot_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_fn);

static int cmd_apply(const struct shell *sh, size_t argc, char **argv)
{
	struct mcuboot_img_header h;
	bool permanent = argc > 1 && strcmp(argv[1], "permanent") == 0;
	int ret;

	if (boot_read_bank_header(SLOT1_ID, &h, sizeof(h)) != 0) {
		shell_error(sh, "no firmware in the second slot (fw upload first)");
		return -ENOENT;
	}
	ret = boot_request_upgrade(permanent ? BOOT_UPGRADE_PERMANENT : BOOT_UPGRADE_TEST);
	if (ret != 0) {
		shell_error(sh, "cannot mark the update (%d)", ret);
		return ret;
	}
	shell_print(sh, "ok restarting into %u.%u.%u (%s)", h.h.v1.sem_ver.major,
		    h.h.v1.sem_ver.minor, h.h.v1.sem_ver.revision,
		    permanent ? "permanent" : "test, rolls back unless it confirms");
	/* Let the reply leave before restarting. */
	/* The LEDs keep this last picture while MCUboot swaps the firmware. */
	atomic_set(&fw_phase, FW_INSTALLING);
	k_work_schedule(&reboot_work, K_MSEC(800));
	return 0;
}

static int cmd_confirm(const struct shell *sh, size_t argc, char **argv)
{
	int ret = boot_write_img_confirmed();

	shell_print(sh, ret == 0 ? "ok confirmed" : "confirm failed (%d)", ret);
	return ret;
}

void fw_progress_overlay(struct led_rgb *out, int64_t now_ms)
{
	int phase = (int)atomic_get(&fw_phase);
	static const struct led_rgb gold = {.r = 255, .g = 165, .b = 35};
	static const struct led_rgb night = {.r = 3, .g = 6, .b = 26};

	if (phase < 0) {
		return;
	}
	if (phase == FW_INSTALLING) {
		for (int i = 0; i < LED_COUNT; i++) {
			out[i] = gold;
		}
		return;
	}
	/* Outline steps 0..47 fill with the progress; the next one twinkles. */
	uint32_t lit = (uint32_t)phase * 48U / 1000U;
	uint8_t tw = fx_tri((uint8_t)(now_ms / 3));
	uint8_t breath = fx_tri((uint8_t)(now_ms / 8));

	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		if (l->group != LED_GROUP_OUTLINE) {
			out[i] = color_scale((struct led_rgb){.r = 120, .g = 150, .b = 255},
					     (uint8_t)(20 + breath / 5));
		} else if (l->step < lit) {
			out[i] = gold;
		} else if (l->step == lit) {
			out[i] = color_scale(gold, tw);
		} else {
			out[i] = night;
		}
	}
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_fw,
	SHELL_CMD(status, NULL, "Firmware versions and update state", cmd_status),
	SHELL_CMD(upload, NULL, "Receive a signed firmware with XMODEM-1K", cmd_upload),
	SHELL_CMD_ARG(apply, NULL, "Restart into the uploaded firmware: apply [permanent]", cmd_apply,
		      1, 1),
	SHELL_CMD(confirm, NULL, "Keep the running firmware", cmd_confirm),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fw, &sub_fw, "Firmware update", NULL);
