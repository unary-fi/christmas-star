/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

#include "imu/lsm6ds.h"
#include "layout/led_layout.h"

/**
 * @file effect.h
 * @brief Effect interface and engine.
 *
 * The engine thread calls the active effect's render() at
 * CONFIG_STAR_FRAME_RATE. render() updates the logical frame in place; the
 * frame keeps its contents between calls.
 */

/** Not offered by the apps (internal modes such as off, manual, test). */
#define FX_HIDDEN BIT(0)
/** Follows how the star is tilted (accelerometer). */
#define FX_TILT BIT(1)
/** The two faces show different pictures. */
#define FX_TWO_FACES BIT(2)
/** The shake overlay stays off (e.g. games). */
#define FX_NO_SHAKE BIT(3)

/** Largest per-effect state (bytes); see struct effect.state_size. */
#define FX_STATE_MAX 256

struct effect {
	const char *name;
	const char *help;
	/** Name and one-line description shown in the apps (protocol 3). */
	const char *title;
	const char *subtitle;
	uint8_t flags;
	/** Run in real time, ignoring the effect speed (e.g. video playback). */
	bool realtime;
	/**
	 * Size of the effect's private state. The caller provides a zeroed,
	 * 4-byte aligned buffer of FX_STATE_MAX bytes, so the effect can run in
	 * two places at once (on the LEDs and for an app preview).
	 */
	uint16_t state_size;
	/** Called when the effect becomes active (optional). */
	void (*start)(struct led_rgb *frame, void *state);
	/** Render one frame. t_ms is the time since start. */
	void (*render)(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu,
		       void *state);
};

/** Check at build time that an effect's state fits FX_STATE_MAX. */
#define FX_STATE_FITS(type) BUILD_ASSERT(sizeof(type) <= FX_STATE_MAX, #type " too big")

/** Built-in effects, NULL terminated. */
extern const struct effect *const effects_builtin[];

/* Effects defined in fx_*.c */
extern const struct effect fx_off;
extern const struct effect fx_solid;
extern const struct effect fx_rainbow;
extern const struct effect fx_twinkle;
extern const struct effect fx_comet;
extern const struct effect fx_golden;
extern const struct effect fx_snowfall;
extern const struct effect fx_candle;
extern const struct effect fx_aurora;
extern const struct effect fx_candycane;
extern const struct effect fx_treelights;
extern const struct effect fx_bethlehem;
extern const struct effect fx_fireworks;
extern const struct effect fx_frost;
extern const struct effect fx_liquidgold;
extern const struct effect fx_shootingstars;
extern const struct effect fx_hearthfrost;
extern const struct effect fx_disco;
extern const struct effect fx_manual;
extern const struct effect fx_player;
extern const struct effect fx_test;
extern const struct effect fx_cometgame;

const struct effect *effect_find(const char *name);

/* ---- Engine -------------------------------------------------------------- */

void engine_start(void);

/** @brief Switch to an effect (thread safe). */
void engine_set(const struct effect *fx);
const struct effect *engine_get(void);

/**
 * @brief Start an effect by name ("flash" plays the stored effect).
 * @return 0 or -ENOENT.
 */
int effect_start_spec(const char *spec);

/** @brief Play the effect stored in the NAND. */
int engine_play_flash(void);

/** @brief Pause rendering (frame stays as is) e.g. during uploads. */
void engine_pause(bool pause);

/**
 * @brief Set LEDs directly (manual mode).
 *
 * Switches to the manual effect, which keeps the last rendered frame as its
 * starting point, and writes the colour to all logical LEDs in [first, last].
 */
void engine_manual_set(int first, int last, struct led_rgb color);

/**
 * @brief Set the manual frame from RGB565 data (little endian, logical order).
 * @param back_copies_front If true, data holds the 66 front LEDs and each back
 *        LED copies its front partner (same XY).
 */
void engine_manual_rgb565(const uint8_t *data, int count, bool back_copies_front);

/** @brief Switch to manual mode with a full frame (e.g. restored from settings). */
void engine_manual_load(const struct led_rgb *pixels);

/** @brief Copy the current effect frame (e.g. to persist the manual frame). */
void engine_copy_frame(struct led_rgb *dst);

/** @brief Effect speed in percent (25..400). */
void engine_set_speed(uint16_t pct);
uint16_t engine_get_speed(void);

/** @brief Measured frames per second over the last second. */
uint32_t engine_fps(void);

/** @brief Fast xorshift32 PRNG for effects (not thread safe; engine thread only). */
uint32_t fx_rand(void);

/** @brief Mix a..b by t/255. */
struct led_rgb fx_mix(struct led_rgb a, struct led_rgb b, uint8_t t);

uint32_t fx_isqrt(uint32_t v);

/** @brief Unit vector (x256) along gravity in the star's plane (down if unknown). */
void fx_gravity_dir(const struct imu_state *imu, int *gx, int *gy);

/** @brief Triangle wave 0..255..0 over one 0..255 phase. */
static inline uint8_t fx_tri(uint8_t phase)
{
	return (uint8_t)(phase < 128 ? phase * 2 : (255 - phase) * 2);
}

/* Parameters shared by effects (simple, set from the shell). */
void fx_solid_set(struct led_rgb c);
void fx_test_set(int idx);
