/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "star_settings.h"
#include "effects/effect.h"
#include "effects/shake_overlay.h"
#include "imu/lsm6ds.h"
#include "led/led_chain.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#define FX_NAME_LEN 12
#define SAVE_DELAY K_SECONDS(3)

static char saved_fx[FX_NAME_LEN] = "golden";
static bool settings_loaded;
/* Manual frame read at boot, applied by star_settings_restore_effect(). */
static bool manual_valid;

static void save_state(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(save_work, save_state);

/* Scratch for the manual frame: used once at boot and by the save work. */
static struct led_rgb frame_buf[LED_COUNT];

static int set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(key, "missing") == 0 && len == LED_MISSING_BYTES) {
		uint8_t bm[LED_MISSING_BYTES];

		if (read_cb(cb_arg, bm, sizeof(bm)) == sizeof(bm)) {
			led_chain_set_missing_bitmap(bm);
		}
	} else if (strcmp(key, "bright") == 0 && len == 1) {
		uint8_t v;

		if (read_cb(cb_arg, &v, 1) == 1) {
			led_chain_set_brightness(v);
		}
	} else if (strcmp(key, "limit") == 0 && len == 2) {
		uint16_t v;

		if (read_cb(cb_arg, &v, 2) == 2) {
			led_chain_set_limit_ma(v);
		}
	} else if (strcmp(key, "axis") == 0 && len == sizeof(struct imu_axis_map)) {
		struct imu_axis_map m;

		if (read_cb(cb_arg, &m, sizeof(m)) == sizeof(m)) {
			imu_set_axis_map(&m);
		}
	} else if (strcmp(key, "shake") == 0 && len == 2) {
		uint16_t v;

		if (read_cb(cb_arg, &v, 2) == 2) {
			imu_set_shake_threshold(v);
		}
	} else if (strcmp(key, "shakeon") == 0 && len == 1) {
		uint8_t v;

		if (read_cb(cb_arg, &v, 1) == 1) {
			shake_set_enabled(v != 0);
		}
	} else if (strcmp(key, "speed") == 0 && len == 2) {
		uint16_t v;

		if (read_cb(cb_arg, &v, 2) == 2) {
			engine_set_speed(v);
		}
	} else if (strcmp(key, "fx") == 0 && len < FX_NAME_LEN) {
		char buf[FX_NAME_LEN] = {0};

		if (read_cb(cb_arg, buf, len) == (ssize_t)len) {
			strcpy(saved_fx, buf);
		}
	} else if (strcmp(key, "manual") == 0 && len == sizeof(frame_buf)) {
		manual_valid = read_cb(cb_arg, frame_buf, sizeof(frame_buf)) == sizeof(frame_buf);
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(star, "star", NULL, set_cb, NULL, NULL);

int star_settings_init(void)
{
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_load();
	}
	settings_loaded = true;
	/* Loading itself calls the engine setters; drop the save they scheduled. */
	k_work_cancel_delayable(&save_work);
	return ret;
}

void star_settings_restore_effect(void)
{
	if (strcmp(saved_fx, "manual") == 0 && manual_valid) {
		engine_manual_load(frame_buf);
	} else if (effect_start_spec(saved_fx) != 0) {
		engine_set(&fx_golden);
	}
	k_work_cancel_delayable(&save_work);
}

void star_settings_state_changed(void)
{
	if (settings_loaded) {
		k_work_reschedule(&save_work, SAVE_DELAY);
	}
}

static void save_state(struct k_work *work)
{
	ARG_UNUSED(work);
	const struct effect *fx = engine_get();
	const char *name = fx == &fx_player ? "flash" : fx->name;
	uint16_t speed = engine_get_speed();
	uint8_t shake = shake_is_enabled() ? 1 : 0;

	if (fx == &fx_test || fx == &fx_cometgame) {
		/* The LED test and the game are temporary; keep resuming the previous state. */
		return;
	}
	if (strcmp(name, saved_fx) != 0 && strlen(name) < FX_NAME_LEN) {
		strcpy(saved_fx, name);
		(void)settings_save_one("star/fx", saved_fx, strlen(saved_fx));
	}
	if (fx == &fx_manual) {
		engine_copy_frame(frame_buf);
		(void)settings_save_one("star/manual", frame_buf, sizeof(frame_buf));
	}
	(void)settings_save_one("star/speed", &speed, sizeof(speed));
	(void)settings_save_one("star/shakeon", &shake, sizeof(shake));
}

int star_settings_save_missing(void)
{
	uint8_t bm[LED_MISSING_BYTES];

	led_chain_get_missing(bm);
	return settings_save_one("star/missing", bm, sizeof(bm));
}

int star_settings_save_brightness(void)
{
	uint8_t v = led_chain_get_brightness();

	return settings_save_one("star/bright", &v, 1);
}

int star_settings_save_limit(void)
{
	uint16_t v = led_chain_get_limit_ma();

	return settings_save_one("star/limit", &v, 2);
}

int star_settings_save_imu(void)
{
	struct imu_axis_map m;
	uint16_t thr = imu_get_shake_threshold();
	int ret;

	imu_get_axis_map(&m);
	ret = settings_save_one("star/axis", &m, sizeof(m));
	return ret ? ret : settings_save_one("star/shake", &thr, 2);
}
