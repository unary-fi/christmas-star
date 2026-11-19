/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Christmas effects. All run on the star itself, so they keep going when the
 * app is disconnected. Time is in effect milliseconds (scaled by the global
 * speed). Coordinates come from the LED layout: x/y in mm (front view, y up),
 * r in mm, angle 0..255 counter-clockwise from the right.
 *
 * The apps get their names, descriptions and preview clips from the star
 * (`app effects`, `app preview`), so a new effect needs no app update. Older
 * app versions have Kotlin/JS ports of the first twelve for offline previews.
 */

#include "effect.h"
#include "led/color.h"

#include <stdlib.h>
#include <string.h>

static void fill(struct led_rgb *frame, struct led_rgb c)
{
	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = c;
	}
}

static void fade(struct led_rgb *frame, uint8_t keep)
{
	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = color_scale(frame[i], keep);
	}
}

/* Linear mix a..b by t/255. */
struct led_rgb fx_mix(struct led_rgb a, struct led_rgb b, uint8_t t)
{
	return (struct led_rgb){
		.r = (uint8_t)(a.r + ((b.r - a.r) * t) / 255),
		.g = (uint8_t)(a.g + ((b.g - a.g) * t) / 255),
		.b = (uint8_t)(a.b + ((b.b - a.b) * t) / 255),
	};
}

uint32_t fx_isqrt(uint32_t v)
{
	uint32_t r = 0, bit = 1UL << 30;

	while (bit > v) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return r;
}

/* Draw a soft light at (x, y) mm on both faces. */
static void splat(struct led_rgb *frame, int x, int y, struct led_rgb c, int radius_mm)
{
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		int dx = led_layout[i].x - x;
		int dy = led_layout[i].y - y;
		int d2 = dx * dx + dy * dy;
		int r2 = radius_mm * radius_mm;

		if (d2 < r2) {
			uint8_t k = (uint8_t)(255 - d2 * 255 / r2);
			struct led_rgb v = color_add(frame[i], color_scale(c, k));

			frame[i] = v;
			frame[led_layout[i].partner] = v;
		}
	}
}

/* ---- Snowfall ------------------------------------------------------------ */

#define FLAKES 14

struct flake {
	int32_t x, y; /* mm * 16 */
	uint8_t speed;
	uint8_t wobble;
};

struct snow_state {
	struct flake flakes[FLAKES];
	uint32_t last;
};
FX_STATE_FITS(struct snow_state);

static void snow_spawn(struct flake *f, int gx, int gy, bool anywhere)
{
	/* Start beyond the "upper" edge (against gravity), spread sideways. */
	int side = (int)(fx_rand() % 240) - 120;
	int back = anywhere ? (int)(fx_rand() % 240) - 120 : -125;

	f->x = (gx * back + -gy * side) * 16 / 256;
	f->y = (gy * back + gx * side) * 16 / 256;
	f->speed = (uint8_t)(6 + fx_rand() % 10);
	f->wobble = (uint8_t)fx_rand();
}

void fx_gravity_dir(const struct imu_state *imu, int *gx, int *gy)
{
	/* Unit vector (x256) pointing down along gravity in the star plane. */
	int x = imu->ok ? -imu->gx : 0;
	int y = imu->ok ? -imu->gy : -1000;
	uint32_t len = fx_isqrt((uint32_t)(x * x + y * y));

	if (len < 300) {
		x = 0;
		y = -1000;
		len = 1000;
	}
	*gx = x * 256 / (int)len;
	*gy = y * 256 / (int)len;
}

static void snow_start(struct led_rgb *frame, void *state)
{
	struct snow_state *st = state;

	ARG_UNUSED(frame);
	for (int i = 0; i < FLAKES; i++) {
		snow_spawn(&st->flakes[i], 0, -256, true);
	}
	st->last = 0;
}

static void snow_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	struct snow_state *st = state;
	int gx, gy;
	uint32_t dt = MIN(t - st->last, 100U);

	st->last = t;
	fx_gravity_dir(imu, &gx, &gy);
	fill(frame, (struct led_rgb){.r = 2, .g = 6, .b = 28});

	for (int i = 0; i < FLAKES; i++) {
		struct flake *f = &st->flakes[i];
		/* speed is mm/s * 2; move along gravity, drift sideways. */
		int step = f->speed * (int)dt / 8;
		int drift = ((int)fx_tri((uint8_t)(f->wobble + t / 12)) - 128) * (int)dt / 256;

		f->x += (gx * step + -gy * drift) / 256;
		f->y += (gy * step + gx * drift) / 256;

		int x = f->x / 16, y = f->y / 16;

		if (abs(x) > 130 || abs(y) > 130) {
			snow_spawn(f, gx, gy, false);
			continue;
		}
		splat(frame, x, y, (struct led_rgb){.r = 210, .g = 225, .b = 255}, 14);
	}
}

const struct effect fx_snowfall = {
	.title = "Snowfall",
	.subtitle = "Flakes drifting down through the night",
	.flags = FX_TILT,
	.state_size = sizeof(struct snow_state),
	.name = "snowfall",
	.help = "Snowflakes drifting down through a night-blue star",
	.start = snow_start,
	.render = snow_render,
};

/* ---- Candlelight ----------------------------------------------------------- */

struct candle_state {
	uint8_t heat[LED_FACE_COUNT];
};
FX_STATE_FITS(struct candle_state);

static void candle_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	uint8_t *heat = ((struct candle_state *)state)->heat;

	ARG_UNUSED(t);
	ARG_UNUSED(imu);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		/* Random walk towards a random target: soft, organic flicker. */
		int target = 150 + (int)(fx_rand() % 106);

		heat[i] = (uint8_t)(heat[i] + (target - heat[i]) / 5);

		/* Warmer and brighter in the inner star, embers at the tips. */
		int glow = l->group == LED_GROUP_INNER ? heat[i] : heat[i] * (140 - l->r) / 90;
		struct led_rgb c = fx_mix((struct led_rgb){.r = 255, .g = 70, .b = 0},
				       (struct led_rgb){.r = 255, .g = 170, .b = 50}, heat[i]);

		c = color_scale(c, (uint8_t)CLAMP(glow, 20, 255));
		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_candle = {
	.title = "Candlelight",
	.subtitle = "A soft flame in the heart of the star",
	.state_size = sizeof(struct candle_state),
	.name = "candle",
	.help = "Warm candlelight flickering in the heart of the star",
	.render = candle_render,
};

/* ---- Northern lights ------------------------------------------------------- */

static void aurora_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		uint8_t w1 = fx_tri((uint8_t)(l->x * 2 + t / 24));
		uint8_t w2 = fx_tri((uint8_t)(l->y * 3 - l->x + t / 37));
		uint8_t lum = (uint8_t)(w1 * w2 / 255);
		/* Green -> teal -> violet across the curtain. */
		uint8_t hue = (uint8_t)(80 + fx_tri((uint8_t)(l->y + t / 50)) * 110 / 255);
		struct led_rgb c = color_hsv(hue, 210, (uint8_t)(15 + lum * 240 / 255));

		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_aurora = {
	.title = "Northern Lights",
	.subtitle = "Green and violet curtains",
	.name = "aurora",
	.help = "Northern lights: green, teal and violet curtains",
	.render = aurora_render,
};

/* ---- Candy cane ------------------------------------------------------------ */

static void candy_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	static const struct led_rgb red = {.r = 230, .g = 10, .b = 20};
	static const struct led_rgb white = {.r = 255, .g = 235, .b = 220};

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		/* Spiral stripes: angle plus radius, rotating slowly. */
		uint8_t phase = (uint8_t)((l->angle + l->r * 2 - t / 10) * 4);
		uint8_t k = fx_tri(phase);
		/* Sharpen into stripes with a soft edge. */
		int s = CLAMP(((int)k - 96) * 4, 0, 255);
		struct led_rgb c = fx_mix(red, white, (uint8_t)s);

		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_candycane = {
	.title = "Candy Cane",
	.subtitle = "Red and white swirls",
	.name = "candycane",
	.help = "Red and white candy cane stripes swirling around",
	.render = candy_render,
};

/* ---- Tree lights ----------------------------------------------------------- */

static void tree_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	static const struct led_rgb bulbs[4] = {
		{.r = 230, .g = 20, .b = 10},   /* red */
		{.r = 20, .g = 200, .b = 40},   /* green */
		{.r = 255, .g = 160, .b = 20},  /* gold */
		{.r = 30, .g = 60, .b = 255},   /* blue */
	};

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		struct led_rgb c;

		if (l->group == LED_GROUP_OUTLINE) {
			int colour = l->step % 4;
			/* Each colour fades in its own rhythm, like old bulb strings. */
			uint8_t b = fx_tri((uint8_t)(t / (7 + colour) + colour * 64));

			c = color_scale(bulbs[colour], (uint8_t)(50 + b * 205 / 255));
		} else {
			uint8_t b = fx_tri((uint8_t)(t / 20 + l->angle));

			c = color_scale((struct led_rgb){.r = 255, .g = 200, .b = 120},
					(uint8_t)(60 + b / 3));
		}
		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_treelights = {
	.title = "Tree Lights",
	.subtitle = "Old-fashioned coloured bulbs",
	.name = "treelights",
	.help = "Old-fashioned red, green, gold and blue tree lights",
	.render = tree_render,
};

/* ---- Star of Bethlehem ----------------------------------------------------- */

static void bethlehem_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		/* Waves of light running from the centre out to the tips. */
		uint8_t wave = fx_tri((uint8_t)(l->r * 3 - t / 5));
		/* The tips (step 4 of each outline point) and the core shine most. */
		bool tip = l->group == LED_GROUP_OUTLINE && (l->step % 8) == 4;
		int core = l->r < 40 ? 120 : 0;
		int lum = 40 + wave * 160 / 255 + core + (tip ? 60 : 0);
		struct led_rgb c = fx_mix((struct led_rgb){.r = 255, .g = 150, .b = 30},
				       (struct led_rgb){.r = 255, .g = 245, .b = 220},
				       (uint8_t)CLAMP(255 - l->r * 2, 0, 255));

		c = color_scale(c, (uint8_t)CLAMP(lum, 0, 255));
		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_bethlehem = {
	.title = "Star of Bethlehem",
	.subtitle = "Radiant light from the centre",
	.name = "bethlehem",
	.help = "The Star of Bethlehem: radiant gold light pulsing outwards",
	.render = bethlehem_render,
};

/* ---- Fireworks ------------------------------------------------------------- */

#define BURSTS 3

struct burst {
	int8_t x, y;
	uint8_t hue;
	uint32_t start;
	uint16_t life;
};

struct fireworks_state {
	struct burst bursts[BURSTS];
};
FX_STATE_FITS(struct fireworks_state);

static void fireworks_start(struct led_rgb *frame, void *state)
{
	memset(frame, 0, sizeof(struct led_rgb) * LED_COUNT);
	memset(state, 0, sizeof(struct fireworks_state));
}

static void fireworks_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	struct burst *bursts = ((struct fireworks_state *)state)->bursts;

	ARG_UNUSED(imu);

	fade(frame, 200);
	for (int b = 0; b < BURSTS; b++) {
		struct burst *bu = &bursts[b];
		uint32_t age = t - bu->start;

		if (bu->life == 0 || age > bu->life) {
			/* Launch a new one after a short random pause. */
			if (fx_rand() % 40 == 0) {
				bu->x = (int8_t)((int)(fx_rand() % 120) - 60);
				bu->y = (int8_t)((int)(fx_rand() % 120) - 60);
				bu->hue = (uint8_t)fx_rand();
				bu->start = t;
				bu->life = (uint16_t)(900 + fx_rand() % 600);
			}
			continue;
		}
		int radius = (int)(age * 70 / bu->life);
		uint8_t lum = (uint8_t)(255 - age * 255 / bu->life);

		for (int i = 0; i < LED_FACE_COUNT; i++) {
			int dx = led_layout[i].x - bu->x;
			int dy = led_layout[i].y - bu->y;
			int d = (int)fx_isqrt((uint32_t)(dx * dx + dy * dy));

			if (abs(d - radius) < 9) {
				uint8_t sat = (uint8_t)(fx_rand() % 4 == 0 ? 60 : 230);
				struct led_rgb c = color_add(frame[i], color_hsv(bu->hue, sat, lum));

				frame[i] = c;
				frame[led_layout[i].partner] = c;
			}
		}
	}
}

const struct effect fx_fireworks = {
	.title = "Fireworks",
	.subtitle = "Bursts for the New Year",
	.state_size = sizeof(struct fireworks_state),
	.name = "fireworks",
	.help = "New Year's fireworks bursting across the star",
	.start = fireworks_start,
	.render = fireworks_render,
};

/* ---- Frost ------------------------------------------------------------------ */

static void frost_start(struct led_rgb *frame, void *state)
{
	ARG_UNUSED(state);
	memset(frame, 0, sizeof(struct led_rgb) * LED_COUNT);
}

static void frost_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	/* Ice creeps in from the tips over 4 s, holds, then thaws (8 s cycle). */
	uint32_t cycle = t % 8000U;
	int front = cycle < 4000 ? 120 - (int)(cycle * 120 / 4000)
		  : cycle < 6000 ? 0 : (int)((cycle - 6000) * 120 / 2000);

	fade(frame, 225);
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		struct led_rgb base = {.r = 4, .g = 10, .b = 30};

		if (l->r >= front) {
			uint8_t shimmer = fx_tri((uint8_t)(t / 9 + l->angle * 3 + l->r));

			base = fx_mix((struct led_rgb){.r = 40, .g = 110, .b = 220},
				   (struct led_rgb){.r = 170, .g = 220, .b = 255}, shimmer);
		}
		struct led_rgb c = color_add(frame[i], color_scale(base, 60));

		if (l->r >= front && fx_rand() % 90 == 0) {
			c = (struct led_rgb){.r = 255, .g = 255, .b = 255};
		}
		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_frost = {
	.title = "Frost",
	.subtitle = "Ice crystals creeping in",
	.name = "frost",
	.help = "Ice crystals creeping in from the tips, sparkling, then thawing",
	.start = frost_start,
	.render = frost_render,
};
