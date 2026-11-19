/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "effect.h"
#include "led/color.h"

#include <string.h>
#include <zephyr/sys/atomic.h>

static uint32_t rng_state = 0x12345678;

uint32_t fx_rand(void)
{
	uint32_t x = rng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rng_state = x;
	return x;
}

static void clear(struct led_rgb *frame)
{
	memset(frame, 0, sizeof(struct led_rgb) * LED_COUNT);
}

/* ---- off ------------------------------------------------------------------ */

static void off_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(t_ms);
	ARG_UNUSED(imu);
	clear(frame);
}

const struct effect fx_off = {
	.flags = FX_HIDDEN,
	.name = "off",
	.help = "All LEDs off",
	.render = off_render,
};

/* ---- solid ---------------------------------------------------------------- */

static struct led_rgb solid_color = {.r = 255, .g = 160, .b = 40};

void fx_solid_set(struct led_rgb c)
{
	solid_color = c;
}

static void solid_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(t_ms);
	ARG_UNUSED(imu);
	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = solid_color;
	}
}

const struct effect fx_solid = {
	.flags = FX_HIDDEN,
	.name = "solid",
	.help = "One colour (effect color <rgb>)",
	.render = solid_render,
};

/* ---- rainbow -------------------------------------------------------------- */

static void rainbow_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	/* Hue follows the polar angle and slowly rotates; radius adds a twist. */
	uint8_t rot = (uint8_t)(t_ms / 16);

	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		frame[i] = color_hsv((uint8_t)(l->angle + l->r / 2 - rot), 255, 255);
	}
}

const struct effect fx_rainbow = {
	.title = "Rainbow",
	.subtitle = "All the colours, turning",
	.name = "rainbow",
	.help = "Rotating rainbow around the star",
	.render = rainbow_render,
};

/* ---- twinkle -------------------------------------------------------------- */

static const struct led_rgb twinkle_base = {.r = 60, .g = 35, .b = 6};

static void twinkle_start(struct led_rgb *frame, void *state)
{
	ARG_UNUSED(state);
	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = twinkle_base;
	}
}

static void twinkle_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(t_ms);
	ARG_UNUSED(imu);

	/* Decay towards the warm base colour. */
	for (int i = 0; i < LED_COUNT; i++) {
		struct led_rgb *p = &frame[i];

		p->r = (uint8_t)(p->r - (p->r - twinkle_base.r) / 12);
		p->g = (uint8_t)(p->g - (p->g - twinkle_base.g) / 12);
		p->b = (uint8_t)(p->b - (p->b - twinkle_base.b) / 12);
	}
	/* A few new sparkles per frame. */
	for (int n = 0; n < 2; n++) {
		uint32_t r = fx_rand();

		if ((r & 0x3) == 0) {
			frame[(r >> 8) % LED_COUNT] = (struct led_rgb){.r = 255, .g = 240, .b = 200};
		}
	}
}

const struct effect fx_twinkle = {
	.title = "Twinkle",
	.subtitle = "Warm glow with sparkles",
	.name = "twinkle",
	.help = "Warm glow with random sparkles",
	.start = twinkle_start,
	.render = twinkle_render,
};

/* ---- comet ---------------------------------------------------------------- */

static void comet_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	/* Two comets chase around the outline (48 steps, 1.6 s per lap), the
	 * inner star breathes.
	 */
	uint32_t head = (t_ms * 48U / 1600U) % 48U;
	uint8_t breath = fx_tri((uint8_t)(t_ms / 12));
	struct led_rgb gold = {.r = 255, .g = 150, .b = 20};

	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		if (l->group == LED_GROUP_OUTLINE) {
			uint32_t d1 = (head + 48U - l->step) % 48U;
			uint32_t d2 = (head + 24U + 48U - l->step) % 48U;
			uint32_t d = MIN(d1, d2);
			uint8_t lvl = d < 8 ? (uint8_t)(255 - d * 32) : 0;

			frame[i] = color_scale((struct led_rgb){.r = 200, .g = 220, .b = 255}, lvl);
		} else {
			frame[i] = color_scale(gold, (uint8_t)(40 + breath * 3 / 4));
		}
	}
}

const struct effect fx_comet = {
	.title = "Comets",
	.subtitle = "Chasing around the outline",
	.name = "comet",
	.help = "Comets chasing around the outline",
	.render = comet_render,
};

/* ---- manual --------------------------------------------------------------- */

static void manual_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	/* The frame is written by set_led / set_all; keep it as is. */
	ARG_UNUSED(frame);
	ARG_UNUSED(t_ms);
	ARG_UNUSED(imu);
}

const struct effect fx_manual = {
	.flags = FX_HIDDEN,
	.name = "manual",
	.help = "Static frame set with set_led / set_all",
	.render = manual_render,
};

/* ---- test ----------------------------------------------------------------- */

static atomic_t test_idx = ATOMIC_INIT(-1);

void fx_test_set(int idx)
{
	atomic_set(&test_idx, idx);
}

static void test_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	int idx = (int)atomic_get(&test_idx);

	clear(frame);
	if (idx < 0) {
		/* Walk through the chain, 300 ms per LED. */
		idx = (int)((t_ms / 300U) % LED_COUNT);
	}
	if (idx < LED_COUNT) {
		frame[idx] = (struct led_rgb){.r = 255, .g = 255, .b = 255};
	}
}

const struct effect fx_test = {
	.flags = FX_HIDDEN,
	.name = "test",
	.help = "Light one LED (led test <led>) or walk the chain",
	.render = test_render,
};
