/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "shake_overlay.h"
#include "effect.h"
#include "led/color.h"

#include <stdlib.h>
#include <string.h>

#define MAX_PARTICLES 32
/* Positions in mm * 256, velocities in mm/frame * 256. */
#define FP 256
/* Slow-motion gravity: 1 g gives ~0.6 mm/frame^2 at 50 fps, so a light
 * falls from the centre to the tip in about 0.4 s.
 */
#define GRAVITY_NUM 154
#define GRAVITY_DEN 1024
#define DRAW_RADIUS_MM 11
#define REGROW_AFTER_MS 3000
#define REGROW_STEP 3
#define SHAKE_COOLDOWN_MS 120
/* Frames before a particle fades out even if it never leaves the star. */
#define MAX_AGE 150

struct particle {
	int32_t x, y;
	int32_t vx, vy;
	struct led_rgb c;
	uint8_t age;
	bool alive;
};

static struct particle parts[MAX_PARTICLES];
/* Attachment level per front LED (back follows its partner). */
static uint8_t level[LED_FACE_COUNT];
static uint32_t last_shake_ms;
static uint32_t last_drop_ms;
static bool enabled = true;
static bool primed;

void shake_overlay_reset(void)
{
	memset(parts, 0, sizeof(parts));
	memset(level, 255, sizeof(level));
	last_shake_ms = 0;
	last_drop_ms = 0;
	primed = true;
}

void shake_set_enabled(bool en)
{
	enabled = en;
	shake_overlay_reset();
}

bool shake_is_enabled(void)
{
	return enabled;
}

static void spawn(int front_idx, struct led_rgb color, const struct imu_state *imu)
{
	for (int p = 0; p < MAX_PARTICLES; p++) {
		if (parts[p].alive) {
			continue;
		}
		const struct led_info *l = &led_layout[front_idx];

		parts[p].x = l->x * FP;
		parts[p].y = l->y * FP;
		/* Small random kick plus a push along the shake direction. */
		parts[p].vx = (int32_t)(fx_rand() % 513) - 256 + (imu->ax - imu->gx) / 4;
		parts[p].vy = (int32_t)(fx_rand() % 513) - 256 + (imu->ay - imu->gy) / 4;
		parts[p].c = color;
		parts[p].age = 0;
		parts[p].alive = true;
		level[front_idx] = 0;
		return;
	}
}

static void knock_loose(const struct led_rgb *out, uint32_t now, const struct imu_state *imu)
{
	uint16_t thr = imu_get_shake_threshold();

	if (!imu->ok || imu->shake < thr) {
		return;
	}
	last_shake_ms = now;
	if (now - last_drop_ms < SHAKE_COOLDOWN_MS) {
		return;
	}
	last_drop_ms = now;

	/* Harder shakes knock more lights loose; outer lights go first. */
	int count = MIN(1 + (imu->shake - thr) / 150, 8);

	for (int tries = 0; count > 0 && tries < 40; tries++) {
		int i = (int)(fx_rand() % LED_FACE_COUNT);

		if (level[i] > 200 && (int)(fx_rand() % 120) < led_layout[i].r + 10) {
			spawn(i, out[i], imu);
			count--;
		}
	}
}

void shake_overlay_apply(struct led_rgb *out, uint32_t now, const struct imu_state *imu)
{
	if (!enabled) {
		return;
	}
	if (!primed) {
		shake_overlay_reset();
	}

	knock_loose(out, now, imu);

	/* Regrow after calm. */
	if (now - last_shake_ms > REGROW_AFTER_MS) {
		for (int i = 0; i < LED_FACE_COUNT; i++) {
			level[i] = (uint8_t)MIN(255, level[i] + REGROW_STEP);
		}
	}

	/* Dim detached lights on both faces. */
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (level[i] != 255) {
			int p = led_layout[i].partner;

			out[i] = color_scale(out[i], level[i]);
			out[p] = color_scale(out[p], level[i]);
		}
	}

	/* Gravity in the board plane. At rest the accelerometer reads +1 g
	 * pointing up (the reaction to gravity), so gravity is its negation.
	 */
	int32_t ax = imu->ok ? -imu->gx * GRAVITY_NUM / GRAVITY_DEN : 0;
	int32_t ay = imu->ok ? -imu->gy * GRAVITY_NUM / GRAVITY_DEN : -GRAVITY_NUM;

	for (int p = 0; p < MAX_PARTICLES; p++) {
		struct particle *pt = &parts[p];

		if (!pt->alive) {
			continue;
		}
		pt->vx += ax;
		pt->vy += ay;
		pt->x += pt->vx;
		pt->y += pt->vy;

		int x = pt->x / FP;
		int y = pt->y / FP;

		if (++pt->age > MAX_AGE || abs(x) > LED_STAR_RADIUS_MM + DRAW_RADIUS_MM ||
		    abs(y) > LED_STAR_RADIUS_MM + DRAW_RADIUS_MM) {
			pt->alive = false;
			continue;
		}
		int i = led_layout_nearest(x, y, LED_FACE_FRONT, DRAW_RADIUS_MM);

		if (i >= 0) {
			uint8_t fade = (uint8_t)(255 - pt->age * 255 / MAX_AGE);
			struct led_rgb c = color_add(out[i], color_scale(pt->c, fade));

			out[i] = c;
			out[led_layout[i].partner] = c;
		}
	}
}
