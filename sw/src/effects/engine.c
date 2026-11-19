/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "effect.h"
#include "shake_overlay.h"
#include "pair/pairing.h"
#include "fw/fw_update.h"
#include "config/star_settings.h"
#include "led/led_chain.h"

#include <string.h>
#include <zephyr/kernel.h>

#define FRAME_MS (1000 / CONFIG_STAR_FRAME_RATE)

/* Order = order of `effect list`. Christmas effects first. */
const struct effect *const effects_builtin[] = {
	&fx_golden, &fx_liquidgold, &fx_snowfall, &fx_shootingstars, &fx_candle,
	&fx_hearthfrost, &fx_aurora, &fx_candycane, &fx_treelights, &fx_bethlehem,
	&fx_fireworks, &fx_disco, &fx_frost, &fx_twinkle, &fx_comet, &fx_rainbow,
	&fx_solid, &fx_off, &fx_manual, &fx_player, &fx_test, NULL,
};

static K_MUTEX_DEFINE(engine_mutex);
/* Effect state (persistent between renders, e.g. the manual frame). */
static struct led_rgb frame[LED_COUNT];
/* What is sent to the LEDs: frame + shake overlay. */
static struct led_rgb out[LED_COUNT];
/* The active effect's private state. */
static uint32_t fx_state[FX_STATE_MAX / 4];
static const struct effect *active = &fx_off;
static const struct effect *pending;
static bool paused;
static uint32_t fps;
static atomic_t speed_pct = ATOMIC_INIT(100);

const struct effect *effect_find(const char *name)
{
	/* Older names. */
	if (strcmp(name, "drop") == 0) {
		name = "golden";
	} else if (strcmp(name, "flash") == 0) {
		name = "player";
	}
	for (int i = 0; effects_builtin[i] != NULL; i++) {
		if (strcmp(effects_builtin[i]->name, name) == 0) {
			return effects_builtin[i];
		}
	}
	return NULL;
}

void engine_set(const struct effect *fx)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	pending = fx;
	k_mutex_unlock(&engine_mutex);
	star_settings_state_changed();
}

const struct effect *engine_get(void)
{
	const struct effect *fx;

	k_mutex_lock(&engine_mutex, K_FOREVER);
	fx = pending != NULL ? pending : active;
	k_mutex_unlock(&engine_mutex);
	return fx;
}

int engine_play_flash(void)
{
	engine_set(&fx_player);
	return 0;
}

void engine_pause(bool pause)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	paused = pause;
	k_mutex_unlock(&engine_mutex);
}

static void enter_manual_locked(void)
{
	/* Switch immediately so the current frame becomes the manual base. */
	pending = NULL;
	active = &fx_manual;
}

void engine_manual_set(int first, int last, struct led_rgb color)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	enter_manual_locked();
	for (int i = first; i <= last && i < LED_COUNT; i++) {
		if (i >= 0) {
			frame[i] = color;
		}
	}
	k_mutex_unlock(&engine_mutex);
	star_settings_state_changed();
}

static inline struct led_rgb rgb565(const uint8_t *p)
{
	uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
	uint8_t r = (uint8_t)((v >> 11) & 0x1F);
	uint8_t g = (uint8_t)((v >> 5) & 0x3F);
	uint8_t b = (uint8_t)(v & 0x1F);

	return (struct led_rgb){
		.r = (uint8_t)((r << 3) | (r >> 2)),
		.g = (uint8_t)((g << 2) | (g >> 4)),
		.b = (uint8_t)((b << 3) | (b >> 2)),
	};
}

void engine_manual_rgb565(const uint8_t *data, int count, bool back_copies_front)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	enter_manual_locked();
	for (int i = 0; i < count && i < LED_COUNT; i++) {
		frame[i] = rgb565(&data[2 * i]);
	}
	if (back_copies_front) {
		for (int i = 0; i < LED_FACE_COUNT; i++) {
			frame[led_layout[i].partner] = frame[i];
		}
	}
	k_mutex_unlock(&engine_mutex);
	star_settings_state_changed();
}

void engine_manual_load(const struct led_rgb *pixels)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	enter_manual_locked();
	memcpy(frame, pixels, sizeof(frame));
	k_mutex_unlock(&engine_mutex);
}

void engine_copy_frame(struct led_rgb *dst)
{
	k_mutex_lock(&engine_mutex, K_FOREVER);
	memcpy(dst, frame, sizeof(frame));
	k_mutex_unlock(&engine_mutex);
}

void engine_set_speed(uint16_t pct)
{
	atomic_set(&speed_pct, CLAMP(pct, 25, 400));
	star_settings_state_changed();
}

uint16_t engine_get_speed(void)
{
	return (uint16_t)atomic_get(&speed_pct);
}

uint32_t engine_fps(void)
{
	return fps;
}

static void engine_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	int64_t next = k_uptime_get();
	int64_t last = next;
	int64_t fps_window = next;
	/* Effect time in ms * 100, advanced by real time * speed %. */
	uint64_t effect_t = 0;
	int64_t started = next;
	uint32_t frames = 0;
	struct imu_state imu;

	while (true) {
		int64_t now = k_uptime_get();

		effect_t += (uint64_t)(now - last) * (uint32_t)atomic_get(&speed_pct);
		last = now;

		k_mutex_lock(&engine_mutex, K_FOREVER);
		if (pending != NULL) {
			active = pending;
			pending = NULL;
			effect_t = 0;
			started = now;
			memset(fx_state, 0, sizeof(fx_state));
			if (active->start != NULL) {
				active->start(frame, fx_state);
			}
		}
		if (!paused) {
			imu_get(&imu);
			uint32_t t = active->realtime ? (uint32_t)(now - started)
						      : (uint32_t)(effect_t / 100);

			active->render(frame, t, &imu, fx_state);
			pairing_tick(now, &imu);
			memcpy(out, frame, sizeof(out));
		}
		k_mutex_unlock(&engine_mutex);

		if (!paused) {
			shake_overlay_apply(out, (uint32_t)now, &imu);
			pairing_overlay_apply(out, now);
			fw_progress_overlay(out, now);
			(void)led_chain_show(out);
			frames++;
		}

		now = k_uptime_get();
		if (now - fps_window >= 1000) {
			fps = frames;
			frames = 0;
			fps_window = now;
		}

		next += FRAME_MS;
		if (next < now) {
			/* Fell behind: skip ahead instead of bursting. */
			next = now;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next));
	}
}

K_THREAD_DEFINE(engine_tid, 1024, engine_thread, NULL, NULL, NULL, 5, 0, SYS_FOREVER_MS);

void engine_start(void)
{
	k_thread_start(engine_tid);
}
