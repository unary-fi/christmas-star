/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "bm78_eeprom.h"
#include "bm78.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#define HCI_CMD 0x01
#define HCI_EVT 0x04
#define EVT_CMD_COMPLETE 0x0E
#define OP_RESET 0x0C03
#define OP_EE_WRITE 0xFC27
#define OP_EE_READ 0xFC29
#define CHUNK 16
#define TRIES 4

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

static uint8_t rx[64];
static size_t rx_len;
static struct k_spinlock rx_lock;
static K_SEM_DEFINE(rx_sem, 0, 1);

void bm78_ee_rx(const uint8_t *data, size_t len)
{
	K_SPINLOCK(&rx_lock) {
		while (len-- > 0 && rx_len < sizeof(rx)) {
			rx[rx_len++] = *data++;
		}
	}
	k_sem_give(&rx_sem);
}

static void flush(void)
{
	K_SPINLOCK(&rx_lock) {
		rx_len = 0;
	}
	k_sem_reset(&rx_sem);
}

/* Wait until `want` bytes have arrived (or the timeout); returns the count. */
static size_t wait_rx(size_t want, int timeout_ms, uint8_t *out)
{
	int64_t end = k_uptime_get() + timeout_ms;
	size_t n = 0;

	while (true) {
		K_SPINLOCK(&rx_lock) {
			n = rx_len;
		}
		if (n >= want || k_uptime_get() >= end) {
			break;
		}
		(void)k_sem_take(&rx_sem, K_MSEC(20));
	}
	K_SPINLOCK(&rx_lock) {
		n = MIN(rx_len, want);
		memcpy(out, rx, n);
	}
	return n;
}

/* Send a command; check the Command Complete header (status 0). Returns the
 * number of return-parameter bytes after the status copied to `ret`, or <0.
 */
static int command(uint16_t op, const uint8_t *params, uint8_t plen, uint8_t *ret,
		   size_t ret_len)
{
	uint8_t hdr[4] = {HCI_CMD, (uint8_t)op, (uint8_t)(op >> 8), plen};
	uint8_t evt[7 + 3 + CHUNK];
	size_t want = 7 + ret_len;

	if (want > sizeof(evt)) {
		return -EINVAL;
	}
	flush();
	for (size_t i = 0; i < sizeof(hdr); i++) {
		uart_poll_out(uart, hdr[i]);
	}
	for (size_t i = 0; i < plen; i++) {
		uart_poll_out(uart, params[i]);
	}
	if (wait_rx(want, 400, evt) != want) {
		return -ETIMEDOUT;
	}
	/* 04 0e <len> <ncmd> <op lo> <op hi> <status> <params...> */
	if (evt[0] != HCI_EVT || evt[1] != EVT_CMD_COMPLETE || evt[2] != want - 3 ||
	    evt[4] != (uint8_t)op || evt[5] != (uint8_t)(op >> 8) || evt[6] != 0) {
		return -EIO;
	}
	memcpy(ret, evt + 7, ret_len);
	return 0;
}

int bm78_ee_begin(void)
{
	uint8_t none[1];

	(void)bm78_start(true);
	k_msleep(600);
	for (int i = 0; i < TRIES; i++) {
		if (command(OP_RESET, NULL, 0, none, 0) == 0) {
			return 0;
		}
		k_msleep(100);
	}
	return -EIO;
}

void bm78_ee_end(void)
{
	(void)bm78_start(false);
}

static int read_chunk(uint16_t addr, uint8_t *out, uint8_t len)
{
	uint8_t p[3] = {(uint8_t)(addr >> 8), (uint8_t)addr, len};
	uint8_t a[3 + CHUNK];
	uint8_t b[3 + CHUNK];

	/* Read twice and accept only matching copies, as the bridge tool does. */
	for (int i = 0; i < TRIES; i++) {
		if (command(OP_EE_READ, p, 3, a, 3 + len) == 0 &&
		    command(OP_EE_READ, p, 3, b, 3 + len) == 0 && memcmp(a, b, 3 + len) == 0 &&
		    a[0] == p[0] && a[1] == p[1] && a[2] == len) {
			memcpy(out, a + 3, len);
			return 0;
		}
	}
	return -EIO;
}

int bm78_ee_read(uint16_t addr, uint8_t *out, size_t len)
{
	for (size_t i = 0; i < len; i += CHUNK) {
		uint8_t n = (uint8_t)MIN(CHUNK, len - i);
		int ret = read_chunk((uint16_t)(addr + i), out + i, n);

		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

int bm78_ee_write(uint16_t addr, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i += CHUNK) {
		uint8_t n = (uint8_t)MIN(CHUNK, len - i);
		uint16_t a = (uint16_t)(addr + i);
		uint8_t p[3 + CHUNK] = {(uint8_t)(a >> 8), (uint8_t)a, n};
		uint8_t check[CHUNK];
		uint8_t none[1];
		bool ok = false;

		memcpy(p + 3, data + i, n);
		for (int t = 0; t < TRIES && !ok; t++) {
			ok = command(OP_EE_WRITE, p, 3 + n, none, 0) == 0 &&
			     read_chunk(a, check, n) == 0 && memcmp(check, data + i, n) == 0;
		}
		if (!ok) {
			return -EIO;
		}
	}
	return 0;
}

int bm78_ee_rename(const char *name, char out[BM78_NAME_MAX + 1])
{
	uint8_t head[BM78_EE_NAME + 32];
	uint8_t adv[BM78_AD_BLOCK], adv_old[BM78_AD_BLOCK];
	uint8_t scan[BM78_AD_BLOCK], scan_old[BM78_AD_BLOCK];
	uint8_t field[32] = {0};
	size_t old_len, width;
	int ret, svc;

	ret = bm78_ee_read(BM78_EE_ADDR, head, sizeof(head));
	ret = ret ? ret : bm78_ee_read(BM78_EE_ADV, adv, sizeof(adv));
	ret = ret ? ret : bm78_ee_read(BM78_EE_SCAN, scan, sizeof(scan));
	if (ret != 0) {
		return ret;
	}
	if (name == NULL) {
		bm78_default_name(head, out);
	} else {
		if (bm78_name_valid(name) != 0) {
			return -EINVAL;
		}
		strncpy(out, name, BM78_NAME_MAX);
		out[BM78_NAME_MAX] = '\0';
	}

	memcpy(adv_old, adv, sizeof(adv));
	memcpy(scan_old, scan, sizeof(scan));
	svc = bm78_ad_add_service(scan);
	if (bm78_ad_set_name(adv, out) != 0 || svc < 0) {
		return -EINVAL;
	}
	old_len = strnlen((const char *)head + BM78_EE_NAME, 32);
	width = MAX(old_len, strlen(out));
	memcpy(field, out, strlen(out));

	ret = bm78_ee_write(BM78_EE_NAME, field, width);
	ret = ret ? ret : bm78_ee_write(BM78_EE_ADV, adv, sizeof(adv));
	if (ret == 0 && svc > 0) {
		ret = bm78_ee_write(BM78_EE_SCAN, scan, sizeof(scan));
	}
	if (ret != 0) {
		/* Put back what was there; ignore errors, the first one counts. */
		(void)bm78_ee_write(BM78_EE_NAME, head + BM78_EE_NAME, width);
		(void)bm78_ee_write(BM78_EE_ADV, adv_old, sizeof(adv_old));
		(void)bm78_ee_write(BM78_EE_SCAN, scan_old, sizeof(scan_old));
	}
	return ret;
}
