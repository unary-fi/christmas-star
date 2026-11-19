/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "pairing.h"
#include "pair_logic.h"
#include "sha256.h"
#include "bt/bm78.h"
#include "bt/bm78_eeprom.h"
#include "cli/cli_hooks.h"
#include "imu/lsm6ds.h"

#include <zephyr/app_version.h>
#include <dev_key.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#define APP_PROTO 3
#define LINE_MAX 96
#define FLASH_MS 1200
#define LINK_POLL_MS 100

enum bt_mode {
	BT_UNLOCKED,
	BT_LOCKED,
	BT_HCI,
};

static struct k_spinlock lock;
static struct pair_logic logic;
static uint8_t owner_key[PAIR_KEY_LEN];
static bool owner_loaded;
static char star_name[BM78_NAME_MAX + 1];
static uint8_t seed[SHA256_LEN];
static uint32_t nonce_count;
static uint8_t nonce[PAIR_NONCE_LEN];
static bool nonce_valid;
static enum pair_access bt_access;
static enum bt_mode bt_mode;
static int64_t flash_at = -FLASH_MS;
static bool flash_green;

static char line[LINE_MAX + 1];
static size_t line_len;

/* Rename requests, handled by the Bluetooth manager thread. */
static K_SEM_DEFINE(rename_sem, 0, 1);
static K_SEM_DEFINE(rename_done, 0, 1);
static char rename_to[BM78_NAME_MAX + 1]; /* "" = default name */
static int rename_result;

static const struct shell *bt_shell(void)
{
	return shell_backend_uart_get_ptr();
}

/* ---- Settings -------------------------------------------------------------- */

static int set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, "owner") == 0 && len == PAIR_KEY_LEN) {
		owner_loaded = read_cb(cb_arg, owner_key, PAIR_KEY_LEN) == PAIR_KEY_LEN;
		return 0;
	}
	if (strcmp(key, "name") == 0 && len <= BM78_NAME_MAX) {
		ssize_t n = read_cb(cb_arg, star_name, len);

		star_name[n > 0 ? n : 0] = '\0';
		return 0;
	}
	if (strcmp(key, "seed") == 0 && len == sizeof(seed)) {
		(void)read_cb(cb_arg, seed, sizeof(seed));
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(pair, "pair", NULL, set_cb, NULL, NULL);

/* ---- Keys and nonces ------------------------------------------------------- */

static bool dev_key_usable(void)
{
	static const uint8_t zero[PAIR_KEY_LEN];

	return DEV_KEY_PRESENT && !pair_equal(dev_key, zero, PAIR_KEY_LEN);
}

static void new_nonce(void)
{
	struct sha256 s;
	uint8_t h[SHA256_LEN];
	uint32_t mix[3] = {nonce_count++, k_cycle_get_32(), (uint32_t)k_uptime_get()};

	sha256_init(&s);
	sha256_update(&s, seed, sizeof(seed));
	sha256_update(&s, mix, sizeof(mix));
	sha256_final(&s, h);
	memcpy(nonce, h, PAIR_NONCE_LEN);
	nonce_valid = true;
}

static bool check_mac(const uint8_t *key, const uint8_t mac[SHA256_LEN])
{
	uint8_t want[SHA256_LEN];

	hmac_sha256(key, PAIR_KEY_LEN, "auth", 4, nonce, PAIR_NONCE_LEN, want);
	return pair_equal(want, mac, SHA256_LEN);
}

/* ---- Shell output while the shell is in bypass ---------------------------- */

static void raw_write(const struct shell *sh, const char *s)
{
	size_t len = strlen(s);

	while (len > 0) {
		size_t done = 0;

		(void)sh->iface->api->write(sh->iface, s, len, &done);
		s += done;
		len -= done;
		if (done == 0) {
			k_msleep(1);
		}
	}
}

static void build_hello(char *buf, size_t n, enum pair_access access)
{
	char hex[2 * PAIR_NONCE_LEN + 1];
	bool owned, open;

	K_SPINLOCK(&lock) {
		new_nonce();
		pair_hex_encode(nonce, PAIR_NONCE_LEN, hex);
		owned = logic.owned;
		open = logic.window;
	}
	snprintf(buf, n, "ok proto=%d leds=%d fw=%s owner=%s pairing=%s access=%s nonce=%s name=%s",
		 APP_PROTO, LED_COUNT, APP_VERSION_STRING, owned ? "paired" : "free",
		 open ? "open" : "closed",
		 access == PAIR_ACCESS_OWNER ? "owner" :
		 access == PAIR_ACCESS_DEV ? "dev" :
		 access == PAIR_ACCESS_LOCAL ? "local" : "none",
		 hex, star_name);
}

/* ---- Locking --------------------------------------------------------------- */

static void bt_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data);

static void set_mode(enum bt_mode mode)
{
	const struct shell *sh = bt_shell();

	if (mode != BT_UNLOCKED) {
		/* Leave binary modes first; they own the bypass. */
		app_stream_abort();
		effect_upload_abort();
		fw_upload_abort();
	}
	K_SPINLOCK(&lock) {
		bt_mode = mode;
		if (mode != BT_UNLOCKED) {
			bt_access = PAIR_ACCESS_NONE;
			nonce_valid = false;
		}
	}
	line_len = 0;
	shell_set_bypass(sh, mode == BT_UNLOCKED ? NULL : bt_bypass, NULL);
}

static void unlock(const struct shell *sh, enum pair_access access)
{
	K_SPINLOCK(&lock) {
		bt_access = access;
		nonce_valid = false;
	}
	/* An app session: no echo (as "app hello" did before). */
	shell_echo_set(sh, false);
	set_mode(BT_UNLOCKED);
}

static void handle_locked_line(const struct shell *sh)
{
	char out[160];
	const char *arg;
	uint8_t buf[SHA256_LEN];

	line[line_len] = '\0';
	switch (pair_parse_locked(line, &arg)) {
	case LOCKED_NONE:
		return;
	case LOCKED_HELLO:
		build_hello(out, sizeof(out), PAIR_ACCESS_NONE);
		break;
	case LOCKED_HELP:
		snprintf(out, sizeof(out),
			 "locked: app hello | app auth <hmac> | app pair <key> (see README)");
		break;
	case LOCKED_AUTH: {
		enum pair_access ok = PAIR_ACCESS_NONE;

		if (pair_hex_decode(arg, buf, SHA256_LEN) == 0) {
			K_SPINLOCK(&lock) {
				if (nonce_valid) {
					if (logic.owned && owner_loaded && check_mac(owner_key, buf)) {
						ok = PAIR_ACCESS_OWNER;
					} else if (dev_key_usable() && check_mac(dev_key, buf)) {
						ok = PAIR_ACCESS_DEV;
					}
				}
				/* One try per nonce. */
				nonce_valid = false;
			}
		}
		if (ok == PAIR_ACCESS_NONE) {
			snprintf(out, sizeof(out), "denied");
			break;
		}
		raw_write(sh, ok == PAIR_ACCESS_OWNER ? "ok access=owner\r\n" : "ok access=dev\r\n");
		unlock(sh, ok);
		return;
	}
	case LOCKED_PAIR: {
		bool open = false;

		if (pair_hex_decode(arg, buf, PAIR_KEY_LEN) != 0) {
			snprintf(out, sizeof(out), "denied bad key");
			break;
		}
		K_SPINLOCK(&lock) {
			open = logic.window;
			if (open) {
				memcpy(owner_key, buf, PAIR_KEY_LEN);
				owner_loaded = true;
				pair_logic_paired(&logic);
				flash_at = k_uptime_get();
				flash_green = true;
			}
		}
		if (!open) {
			snprintf(out, sizeof(out), "denied pairing closed");
			break;
		}
		(void)settings_save_one("pair/owner", buf, PAIR_KEY_LEN);
		raw_write(sh, "ok paired\r\n");
		unlock(sh, PAIR_ACCESS_OWNER);
		return;
	}
	default:
		snprintf(out, sizeof(out), "locked");
		break;
	}
	raw_write(sh, out);
	raw_write(sh, "\r\n");
}

static void bt_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(user_data);

	if (bt_mode == BT_HCI) {
		bm78_ee_rx(data, len);
		return;
	}
	for (size_t i = 0; i < len && bt_mode == BT_LOCKED; i++) {
		char c = (char)data[i];

		if (c == '\r' || c == '\n') {
			handle_locked_line(sh);
			line_len = 0;
		} else if (line_len < LINE_MAX) {
			line[line_len++] = c;
		}
	}
}

enum pair_access pairing_access(const struct shell *sh)
{
	enum pair_access a = PAIR_ACCESS_LOCAL;

	if (sh == bt_shell()) {
		K_SPINLOCK(&lock) {
			a = bt_access;
		}
	}
	return a;
}

void pairing_hello(const struct shell *sh)
{
	char out[160];

	build_hello(out, sizeof(out), pairing_access(sh));
	shell_print(sh, "%s", out);
}

const char *pairing_name(void)
{
	return star_name;
}

bool pairing_owned(void)
{
	bool owned;

	K_SPINLOCK(&lock) {
		owned = logic.owned;
	}
	return owned;
}

uint32_t pairing_window_left(void)
{
	uint32_t left;

	K_SPINLOCK(&lock) {
		left = pair_logic_window_left(&logic, k_uptime_get());
	}
	return left;
}

/* ---- Engine hooks ---------------------------------------------------------- */

void pairing_tick(int64_t now_ms, const struct imu_state *imu)
{
	bool shaking = imu->shake >= imu_get_shake_threshold();

	K_SPINLOCK(&lock) {
		(void)pair_logic_tick(&logic, now_ms, shaking);
	}
}

static inline uint8_t mix(uint8_t a, uint8_t b, uint32_t t)
{
	return (uint8_t)((a * (256U - t) + b * t) >> 8);
}

void pairing_overlay_apply(struct led_rgb *out, int64_t now_ms)
{
	struct led_rgb c;
	uint32_t t;
	bool window;
	int64_t since;
	bool green;

	K_SPINLOCK(&lock) {
		window = logic.window;
		since = now_ms - flash_at;
		green = flash_green;
	}
	if (since >= 0 && since < FLASH_MS) {
		/* Three blinks: green = paired, red = released. */
		if ((since / 200) % 2 != 0) {
			return;
		}
		c = green ? (struct led_rgb){.r = 0, .g = 220, .b = 60}
			  : (struct led_rgb){.r = 230, .g = 20, .b = 0};
		t = 230;
	} else if (window) {
		/* Gold breathing, 2 s period. */
		uint32_t ph = (uint32_t)(now_ms % 2000);

		ph = ph < 1000 ? ph : 2000 - ph;
		c = (struct led_rgb){.r = 255, .g = 170, .b = 20};
		t = 40 + ph * 170 / 1000;
	} else {
		return;
	}
	for (int i = 0; i < LED_COUNT; i++) {
		out[i].r = mix(out[i].r, c.r, t);
		out[i].g = mix(out[i].g, c.g, t);
		out[i].b = mix(out[i].b, c.b, t);
	}
}

/* ---- Bluetooth manager: link watch and renaming --------------------------- */

static void do_rename(void)
{
	char name[BM78_NAME_MAX + 1];
	int ret;

	set_mode(BT_HCI);
	ret = bm78_ee_begin();
	if (ret == 0) {
		ret = bm78_ee_rename(rename_to[0] != '\0' ? rename_to : NULL, name);
	}
	bm78_ee_end();
	if (ret == 0) {
		strcpy(star_name, name);
		(void)settings_save_one("pair/name", star_name, strlen(star_name));
	}
	rename_result = ret;
	set_mode(BT_LOCKED);
	k_sem_give(&rename_done);
}

static void bt_manager(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	bool linked = false;

	while (true) {
		int s1, s2;

		if (k_sem_take(&rename_sem, K_MSEC(LINK_POLL_MS)) == 0) {
			/* Let the "renaming" reply leave before the module restarts. */
			k_msleep(300);
			do_rename();
			linked = false;
			continue;
		}
		bm78_status(&s1, &s2);
		/* P1_5 low = a central is connected (Link). */
		if (linked && s1 != 0) {
			set_mode(BT_LOCKED);
		}
		linked = s1 == 0;
	}
}

K_THREAD_DEFINE(bt_manager_tid, 1536, bt_manager, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

int pairing_init(void)
{
	uint32_t mix[2] = {k_cycle_get_32(), (uint32_t)k_uptime_get()};
	struct sha256 s;

	sha256_init(&s);
	sha256_update(&s, seed, sizeof(seed));
	sha256_update(&s, mix, sizeof(mix));
	sha256_final(&s, seed);
	(void)settings_save_one("pair/seed", seed, sizeof(seed));

	pair_logic_init(&logic, owner_loaded, k_uptime_get());
	set_mode(BT_LOCKED);
	k_thread_start(bt_manager_tid);
	return 0;
}

/* ---- Shell commands -------------------------------------------------------- */

static int cmd_name(const struct shell *sh, size_t argc, char **argv)
{
	char name[BM78_NAME_MAX + 1] = "";
	size_t n = 0;

	if (argc < 2) {
		shell_print(sh, "name=%s", star_name);
		return 0;
	}
	if (strcmp(argv[1], "--default") != 0) {
		for (size_t i = 1; i < argc; i++) {
			int w = snprintf(name + n, sizeof(name) - n, "%s%s", i > 1 ? " " : "", argv[i]);

			if (w < 0 || (size_t)w >= sizeof(name) - n) {
				shell_error(sh, "1-%d characters", BM78_NAME_MAX);
				return -EINVAL;
			}
			n += (size_t)w;
		}
		if (bm78_name_valid(name) != 0) {
			shell_error(sh, "1-%d printable ASCII characters", BM78_NAME_MAX);
			return -EINVAL;
		}
	}
	strcpy(rename_to, name);
	k_sem_reset(&rename_done);
	shell_print(sh, "ok renaming");
	k_sem_give(&rename_sem);
	if (sh == bt_shell()) {
		/* The link drops while the module restarts; reconnect after ~3 s. */
		return 0;
	}
	if (k_sem_take(&rename_done, K_SECONDS(30)) != 0) {
		shell_error(sh, "rename timed out");
		return -ETIMEDOUT;
	}
	if (rename_result != 0) {
		shell_error(sh, "rename failed (%d)", rename_result);
		return rename_result;
	}
	shell_print(sh, "name=%s", star_name);
	return 0;
}

static int cmd_unpair(const struct shell *sh, size_t argc, char **argv)
{
	enum pair_access a = pairing_access(sh);
	bool force = argc > 1 && strcmp(argv[1], "force") == 0;

	if (a == PAIR_ACCESS_DEV && !force) {
		shell_error(sh, "developer access: use 'star unpair force'");
		return -EPERM;
	}
	K_SPINLOCK(&lock) {
		memset(owner_key, 0, sizeof(owner_key));
		owner_loaded = false;
		pair_logic_released(&logic);
		flash_at = k_uptime_get();
		flash_green = false;
	}
	(void)settings_delete("pair/owner");
	shell_print(sh, "ok released");
	return 0;
}

static int cmd_pairing(const struct shell *sh, size_t argc, char **argv)
{
	enum pair_access a = pairing_access(sh);

	if (argc > 1) {
		if (a != PAIR_ACCESS_LOCAL && a != PAIR_ACCESS_DEV) {
			shell_error(sh, "developer or J-Link only");
			return -EPERM;
		}
		if (strcmp(argv[1], "open") == 0) {
			K_SPINLOCK(&lock) {
				pair_logic_open(&logic, k_uptime_get());
			}
		} else if (strcmp(argv[1], "reset") == 0) {
			K_SPINLOCK(&lock) {
				memset(owner_key, 0, sizeof(owner_key));
				owner_loaded = false;
				pair_logic_released(&logic);
				pair_logic_open(&logic, k_uptime_get());
			}
			(void)settings_delete("pair/owner");
		} else {
			shell_error(sh, "pairing [open|reset]");
			return -EINVAL;
		}
	}
	shell_print(sh, "owner=%s pairing=%s window=%us devkey=%s name=%s",
		    pairing_owned() ? "paired" : "free", pairing_window_left() ? "open" : "closed",
		    pairing_window_left() / 1000, dev_key_usable() ? "yes" : "no", star_name);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_star,
	SHELL_CMD_ARG(name, NULL, "Show or set the star's name: name [text|--default]", cmd_name, 1,
		      4),
	SHELL_CMD_ARG(unpair, NULL, "Release the star from its owner: unpair [force]", cmd_unpair, 1,
		      1),
	SHELL_CMD_ARG(pairing, NULL, "Pairing status; open the window or reset: pairing [open|reset]",
		      cmd_pairing, 1, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(star, &sub_star, "Star name and pairing", NULL);
