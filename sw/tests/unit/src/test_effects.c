/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "effects/effect.h"
#include "effects/shake_overlay.h"

static struct led_rgb frame[LED_COUNT];
static struct led_rgb out[LED_COUNT];

static int dark_count(const struct led_rgb *f)
{
	int n = 0;

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (f[i].r + f[i].g + f[i].b < 30) {
			n++;
		}
	}
	return n;
}

static void assert_faces_match(const struct led_rgb *f)
{
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_rgb *a = &f[i];
		const struct led_rgb *b = &f[led_layout[i].partner];

		zassert_true(a->r == b->r && a->g == b->g && a->b == b->b, "LED %d", i);
	}
}

ZTEST_SUITE(effects, NULL, NULL, NULL, NULL, NULL);

ZTEST(effects, test_shake_drops_and_regrows_any_colour)
{
	/* Upright star at rest: the accelerometer reads +1 g along board +y. */
	struct imu_state calm = {.ok = true, .gy = 1000, .ay = 1000, .shake = 0};
	struct imu_state shake = calm;
	struct led_rgb teal = {.r = 0, .g = 180, .b = 160};
	uint32_t t = 1000;

	shake.shake = 2000;
	shake.ax = 1500;

	shake_set_enabled(true);
	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = teal;
	}

	/* Shake for half a second. The effect frame must never change. */
	for (int i = 0; i < 25; i++) {
		t += 20;
		memcpy(out, frame, sizeof(out));
		shake_overlay_apply(out, t, &shake);
		assert_faces_match(out);
	}
	for (int i = 0; i < 10; i++) {
		t += 20;
		memcpy(out, frame, sizeof(out));
		shake_overlay_apply(out, t, &calm);
	}
	zassert_true(dark_count(out) > 0, "shaking knocks lights off");
	for (int i = 0; i < LED_COUNT; i++) {
		zassert_true(frame[i].g == teal.g, "effect frame untouched");
	}

	/* After a long calm period everything has grown back. */
	for (int i = 0; i < 400; i++) {
		t += 20;
		memcpy(out, frame, sizeof(out));
		shake_overlay_apply(out, t, &calm);
	}
	zassert_equal(dark_count(out), 0, "lights grow back");
	zassert_mem_equal(out, frame, sizeof(out), "no overlay left");
}

ZTEST(effects, test_shake_disabled_is_transparent)
{
	struct imu_state shake = {.ok = true, .gy = 1000, .shake = 3000, .ax = 2000};

	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = (struct led_rgb){.r = 200, .g = 10, .b = 10};
	}
	shake_set_enabled(false);
	for (int i = 0; i < 30; i++) {
		memcpy(out, frame, sizeof(out));
		shake_overlay_apply(out, 2000U + (uint32_t)i * 20U, &shake);
	}
	zassert_mem_equal(out, frame, sizeof(out));
	shake_set_enabled(true);
}

static uint32_t state_a[FX_STATE_MAX / 4];
static uint32_t state_b[FX_STATE_MAX / 4];

static void run(const struct effect *fx, struct led_rgb *f, void *state, uint32_t from,
		uint32_t to, const struct imu_state *imu)
{
	for (uint32_t t = from; t < to; t += 20) {
		fx->render(f, t, imu, state);
	}
}

ZTEST(effects, test_all_effects_render)
{
	struct imu_state imu = {.ok = true, .gy = 1000, .ay = 1000};
	int listed = 0;

	/* Every effect compiled into the tests (the engine's list minus the player). */
	static const struct effect *const all[] = {
		&fx_golden, &fx_liquidgold, &fx_snowfall, &fx_shootingstars, &fx_candle,
		&fx_hearthfrost, &fx_aurora, &fx_candycane, &fx_treelights, &fx_bethlehem,
		&fx_fireworks, &fx_disco, &fx_frost, &fx_twinkle, &fx_comet, &fx_rainbow,
		&fx_solid, &fx_off, &fx_manual, &fx_test,
	};

	for (size_t e = 0; e < ARRAY_SIZE(all); e++) {
		const struct effect *fx = all[e];
		uint32_t lit = 0;

		zassert_true(fx->help != NULL && fx->name != NULL);
		zassert_true(fx->state_size <= FX_STATE_MAX, "%s state", fx->name);
		if (fx->flags & FX_HIDDEN) {
			continue;
		}
		listed++;
		zassert_true(fx->title != NULL && fx->subtitle != NULL, "%s has a title", fx->name);
		memset(frame, 0, sizeof(frame));
		memset(state_a, 0, sizeof(state_a));
		if (fx->start != NULL) {
			fx->start(frame, state_a);
		}
		for (uint32_t t = 0; t < 6000; t += 20) {
			fx->render(frame, t, &imu, state_a);
			for (int i = 0; i < LED_COUNT; i++) {
				lit += (frame[i].r + frame[i].g + frame[i].b) > 0;
			}
		}
		zassert_true(lit > 0, "%s lights something", fx->name);
	}
	zassert_equal(listed, 16, "16 effects offered");
}

ZTEST(effects, test_two_copies_do_not_share_state)
{
	static const struct effect *const fx[] = {
		&fx_snowfall, &fx_candle, &fx_fireworks, &fx_liquidgold, &fx_shootingstars,
		&fx_hearthfrost,
	};
	struct imu_state imu = {.ok = true, .gy = 1000};
	static struct led_rgb f2[LED_COUNT];
	static uint32_t snapshot[FX_STATE_MAX / 4];

	for (size_t e = 0; e < ARRAY_SIZE(fx); e++) {
		memset(state_a, 0, sizeof(state_a));
		memset(state_b, 0, sizeof(state_b));
		if (fx[e]->start != NULL) {
			fx[e]->start(frame, state_a);
			fx[e]->start(f2, state_b);
		}
		run(fx[e], frame, state_a, 0, 3000, &imu);
		memcpy(snapshot, state_b, sizeof(snapshot));
		/* A preview renders its own copy; the playing one must not change. */
		run(fx[e], f2, state_b, 0, 3000, &imu);
		run(fx[e], frame, state_a, 3000, 4000, &imu);
		zassert_true(memcmp(snapshot, state_b, sizeof(snapshot)) != 0 ||
			     fx[e]->state_size == 0, "%s uses its state", fx[e]->name);
		memcpy(snapshot, state_b, sizeof(snapshot));
		run(fx[e], frame, state_a, 4000, 5000, &imu);
		zassert_mem_equal(snapshot, state_b, sizeof(snapshot), "%s state isolated",
				  fx[e]->name);
	}
}

static int brightness(const struct led_rgb *f, bool (*pick)(const struct led_info *))
{
	int sum = 0, n = 0;

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (pick(&led_layout[i])) {
			sum += f[i].r + f[i].g + f[i].b;
			n++;
		}
	}
	return n ? sum / n : 0;
}

static bool low(const struct led_info *l) { return l->y < -40; }
static bool high(const struct led_info *l) { return l->y > 40; }
static bool right(const struct led_info *l) { return l->x > 40; }
static bool left(const struct led_info *l) { return l->x < -40; }

ZTEST(effects, test_liquid_gold_follows_gravity)
{
	struct imu_state upright = {.ok = true, .gy = 1000};
	/* Turned on its side: "down" points to the star's right. */
	struct imu_state sideways = {.ok = true, .gx = -1000};

	memset(state_a, 0, sizeof(state_a));
	run(&fx_liquidgold, frame, state_a, 0, 3000, &upright);
	zassert_true(brightness(frame, low) > brightness(frame, high) + 100, "gold at the bottom");
	run(&fx_liquidgold, frame, state_a, 3000, 9000, &sideways);
	zassert_true(brightness(frame, right) > brightness(frame, left) + 100, "gold flowed right");
}

ZTEST(effects, test_hearth_and_frost_faces)
{
	struct imu_state imu = {.ok = true, .gy = 1000};
	int warm_front = 0, warm_back = 0;

	memset(state_a, 0, sizeof(state_a));
	run(&fx_hearthfrost, frame, state_a, 0, 10000, &imu);
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_rgb *b = &frame[led_layout[i].partner];

		warm_front += frame[i].r - frame[i].b;
		warm_back += b->r - b->b;
	}
	zassert_true(warm_front > 0 && warm_back < 0, "front hearth, back frost");

	run(&fx_hearthfrost, frame, state_a, 30000, 40000, &imu);
	warm_front = warm_back = 0;
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_rgb *b = &frame[led_layout[i].partner];

		warm_front += frame[i].r - frame[i].b;
		warm_back += b->r - b->b;
	}
	zassert_true(warm_front < 0 && warm_back > 0, "swapped after 30 s");
}

ZTEST(effects, test_disco_beat_and_faces)
{
	struct imu_state imu = {.ok = true, .gy = 1000};
	int on_beat = 0, before_beat = 0, differ = 0;

	/* 100 ms after a beat versus just before the next one (outside the strobe). */
	fx_disco.render(frame, 1100, &imu, state_a);
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].group == LED_GROUP_OUTLINE) {
			on_beat += frame[i].r + frame[i].g + frame[i].b;
		}
	}
	fx_disco.render(frame, 1490, &imu, state_a);
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_rgb *a = &frame[i];
		const struct led_rgb *b = &frame[led_layout[i].partner];

		if (led_layout[i].group == LED_GROUP_OUTLINE) {
			before_beat += a->r + a->g + a->b;
		}
		differ += (a->r != b->r || a->g != b->g || a->b != b->b);
	}
	zassert_true(on_beat > before_beat * 2, "the outline pulses with the beat");
	zassert_true(differ > LED_FACE_COUNT / 2, "the two faces show different colours");
}
