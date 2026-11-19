/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * More Christmas effects: Liquid Gold (follows the tilt), Shooting Stars and
 * Hearth & Frost (the two faces differ). Same conventions as fx_xmas.c:
 * effect time in ms, layout coordinates in mm (front view, y up).
 */

#include "effect.h"
#include "led/color.h"

#include <stdlib.h>
#include <string.h>

/* Signed "cosine" and "sine" (x256) of an angle 0..255 from the triangle wave:
 * not exact, but smooth enough for directions once normalised.
 */
static void direction(uint8_t a, int *dx, int *dy)
{
	int c = ((int)fx_tri((uint8_t)(a + 64)) - 128) * 2;
	int s = ((int)fx_tri(a) - 128) * 2;
	int len = (int)fx_isqrt((uint32_t)(c * c + s * s));

	if (len == 0) {
		len = 1;
	}
	*dx = c * 256 / len;
	*dy = s * 256 / len;
}

/* ---- Liquid Gold ------------------------------------------------------------ */

#define LG_ONE 4096   /* unit vector length in the slosh state */
#define LG_LEVEL 25   /* mm below the centre: the gold fills the lower part */

struct liquid_state {
	int32_t sx, sy;   /* smoothed "down" direction, LG_ONE = unit */
	int32_t vx, vy;   /* its velocity (sloshing) */
	uint32_t last;
	bool init;
};
FX_STATE_FITS(struct liquid_state);

static void liquid_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu,
			  void *state)
{
	struct liquid_state *st = state;
	int gx, gy;
	uint32_t dt = MIN(t - st->last, 100U);

	st->last = t;
	fx_gravity_dir(imu, &gx, &gy);
	if (!st->init) {
		st->sx = gx * (LG_ONE / 256);
		st->sy = gy * (LG_ONE / 256);
		st->init = true;
	}
	/* A damped spring pulls the surface towards the real "down": the gold
	 * sloshes for a moment when the star is turned, then settles.
	 */
	st->vx += (gx * (LG_ONE / 256) - st->sx) * (int32_t)dt / 160;
	st->vy += (gy * (LG_ONE / 256) - st->sy) * (int32_t)dt / 160;
	st->vx = st->vx * (1000 - 5 * (int32_t)dt) / 1000;
	st->vy = st->vy * (1000 - 5 * (int32_t)dt) / 1000;
	st->sx += st->vx * (int32_t)dt / 160;
	st->sy += st->vy * (int32_t)dt / 160;

	int len = (int)fx_isqrt((uint32_t)(st->sx * st->sx + st->sy * st->sy));

	if (len < LG_ONE / 8) {
		len = LG_ONE / 8;
	}
	/* Waves grow with the sloshing speed. */
	int speed = (int)fx_isqrt((uint32_t)(st->vx * st->vx + st->vy * st->vy));
	int amp = CLAMP(3 + speed / 40, 3, 14);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		/* depth: mm below the centre along "down"; along: position on the surface */
		int depth = (l->x * st->sx + l->y * st->sy) / len;
		int along = (l->x * -st->sy + l->y * st->sx) / len;
		int level = LG_LEVEL + ((int)fx_tri((uint8_t)(along * 3 + t / 8)) - 128) * amp / 128;
		int d = depth - level;
		struct led_rgb c;

		if (d > 0) {
			/* In the gold: molten shimmer, a little brighter deeper down. */
			uint8_t sh = fx_tri((uint8_t)(t / 7 + l->x * 3 + l->y * 2));
			struct led_rgb base = fx_mix((struct led_rgb){.r = 255, .g = 130, .b = 10},
						     (struct led_rgb){.r = 255, .g = 215, .b = 110}, sh);

			c = color_scale(base, (uint8_t)CLAMP(170 + d, 170, 255));
		} else if (d > -5) {
			/* The bright rim of the surface. */
			c = color_scale((struct led_rgb){.r = 255, .g = 235, .b = 170},
					(uint8_t)(255 + d * 30));
		} else {
			/* Above: warm reflected light, fading upwards. */
			int k = CLAMP(90 + d * 2, 12, 90);

			c = color_scale((struct led_rgb){.r = 255, .g = 120, .b = 20}, (uint8_t)k);
		}
		frame[i] = c;
		frame[l->partner] = c;
	}
}

const struct effect fx_liquidgold = {
	.name = "liquidgold",
	.help = "Molten gold pooling at the lowest point; tilt the star to move it",
	.title = "Liquid Gold",
	.subtitle = "Tilt the star and the gold flows",
	.flags = FX_TILT,
	.state_size = sizeof(struct liquid_state),
	.render = liquid_render,
};

/* ---- Shooting Stars ----------------------------------------------------------- */

#define METEORS 2
#define METEOR_PATH 280 /* mm travelled across the star */

struct meteor {
	int16_t x0, y0;   /* start (mm) */
	int16_t dx, dy;   /* direction x256 */
	uint32_t start;
	uint16_t dur;     /* ms; 0 = idle */
};

struct shooting_state {
	struct meteor m[METEORS];
	uint32_t next;
};
FX_STATE_FITS(struct shooting_state);

static void shooting_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu,
			    void *state)
{
	struct shooting_state *st = state;

	ARG_UNUSED(imu);

	/* Night sky with a few faint, slowly twinkling stars. */
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		struct led_rgb c = {.r = 1, .g = 3, .b = 16};

		if ((i * 37) % 9 == 0) {
			uint8_t tw = fx_tri((uint8_t)(t / 23 + i * 29));

			c = color_add(c, color_scale((struct led_rgb){.r = 180, .g = 190, .b = 255},
						     (uint8_t)(12 + tw / 9)));
		}
		frame[i] = c;
	}

	/* Now and then a meteor. */
	if (t >= st->next) {
		for (int k = 0; k < METEORS; k++) {
			struct meteor *m = &st->m[k];

			if (m->dur != 0) {
				continue;
			}
			int dx, dy;
			int off = (int)(fx_rand() % 120) - 60;

			direction((uint8_t)fx_rand(), &dx, &dy);
			m->dx = (int16_t)dx;
			m->dy = (int16_t)dy;
			m->x0 = (int16_t)((-dx * (METEOR_PATH / 2) - dy * off) / 256);
			m->y0 = (int16_t)((-dy * (METEOR_PATH / 2) + dx * off) / 256);
			m->start = t;
			m->dur = (uint16_t)(1100 + fx_rand() % 600);
			break;
		}
		st->next = t + 1500U + fx_rand() % 3500U;
	}

	for (int k = 0; k < METEORS; k++) {
		struct meteor *m = &st->m[k];
		uint32_t age = t - m->start;

		if (m->dur == 0) {
			continue;
		}
		if (age >= m->dur) {
			m->dur = 0;
			continue;
		}
		int dist = (int)(METEOR_PATH * age / m->dur);
		int hx = m->x0 + m->dx * dist / 256;
		int hy = m->y0 + m->dy * dist / 256;
		/* Fade in and out at the ends of the path. */
		int env = CLAMP((int)MIN(age, m->dur - age) * 255 / 250, 0, 255);

		for (int i = 0; i < LED_FACE_COUNT; i++) {
			int rx = led_layout[i].x - hx;
			int ry = led_layout[i].y - hy;
			int behind = -(rx * m->dx + ry * m->dy) / 256;
			int perp = abs(rx * -m->dy + ry * m->dx) / 256;

			if (behind < -6 || behind > 80 || perp >= 9) {
				continue;
			}
			int k_len = behind < 0 ? 255 + behind * 40 : 255 - behind * 255 / 80;
			int lum = k_len * (255 - perp * 255 / 9) / 255 * env / 255;
			struct led_rgb c = fx_mix((struct led_rgb){.r = 255, .g = 240, .b = 200},
						  (struct led_rgb){.r = 120, .g = 160, .b = 255},
						  (uint8_t)CLAMP(behind * 4, 0, 255));

			if (behind > 12 && fx_rand() % 8 == 0) {
				lum = MIN(255, lum * 2); /* sparkles in the tail */
			}
			frame[i] = color_add(frame[i], color_scale(c, (uint8_t)CLAMP(lum, 0, 255)));
		}
	}
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		frame[led_layout[i].partner] = frame[i];
	}
}

const struct effect fx_shootingstars = {
	.name = "shootingstars",
	.help = "A quiet night sky; now and then a shooting star streaks across",
	.title = "Shooting Stars",
	.subtitle = "Make a wish",
	.state_size = sizeof(struct shooting_state),
	.render = shooting_render,
};

/* ---- Hearth & Frost ------------------------------------------------------------ */

#define HF_CYCLE 60000U  /* ms: hold 25 s, change over 5 s, hold, change back */

struct hearth_state {
	uint8_t heat[LED_FACE_COUNT];
};
FX_STATE_FITS(struct hearth_state);

/* 0 = front hearth / back frost, 255 = swapped. */
static uint8_t hf_swap(uint32_t t)
{
	uint32_t p = t % HF_CYCLE;

	if (p < 25000U) {
		return 0;
	}
	if (p < 30000U) {
		return (uint8_t)((p - 25000U) * 255U / 5000U);
	}
	if (p < 55000U) {
		return 255;
	}
	return (uint8_t)(255U - (p - 55000U) * 255U / 5000U);
}

static void hearth_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu,
			  void *state)
{
	uint8_t *heat = ((struct hearth_state *)state)->heat;
	uint8_t w = hf_swap(t);
	/* Violet dusk while the two moods trade places. */
	uint8_t dusk = fx_tri((uint8_t)(w / 2));

	ARG_UNUSED(imu);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		/* Hearth: embers flicker; the fire is strongest low down. */
		int target = 110 + (int)(fx_rand() % 146);

		heat[i] = (uint8_t)(heat[i] + (target - heat[i]) / 4);
		int low = CLAMP(110 - l->y, 0, 220);
		struct led_rgb hearth = fx_mix((struct led_rgb){.r = 170, .g = 18, .b = 0},
					       (struct led_rgb){.r = 255, .g = 150, .b = 30}, heat[i]);

		hearth = color_scale(hearth, (uint8_t)(heat[i] * (70 + low) / 290));

		/* Frost: icy shimmer with the odd glint. */
		uint8_t sh = fx_tri((uint8_t)(t / 9 + l->angle * 3 + l->r));
		struct led_rgb frost = color_scale(
			fx_mix((struct led_rgb){.r = 20, .g = 60, .b = 170},
			       (struct led_rgb){.r = 170, .g = 220, .b = 255}, sh), 190);

		if (fx_rand() % 150 == 0) {
			frost = (struct led_rgb){.r = 255, .g = 255, .b = 255};
		}

		struct led_rgb violet = color_scale((struct led_rgb){.r = 90, .g = 30, .b = 120},
						    (uint8_t)(dusk / 2));

		frame[i] = color_add(fx_mix(hearth, frost, w), violet);
		frame[l->partner] = color_add(fx_mix(frost, hearth, w), violet);
	}
}

const struct effect fx_hearthfrost = {
	.name = "hearthfrost",
	.help = "A warm hearth on one face, frost on the other; they trade places",
	.title = "Hearth & Frost",
	.subtitle = "Warm and frosty faces",
	.flags = FX_TWO_FACES,
	.state_size = sizeof(struct hearth_state),
	.render = hearth_render,
};

/* ---- Christmas Disco ------------------------------------------------------------ */

#define DISCO_BEAT_MS 500U    /* 120 beats per minute */
#define DISCO_BAR_MS 2000U    /* 4 beats */
#define DISCO_STROBE_MS 4000U /* one short flash per 8 beats */
#define DISCO_SPOTS 5
#define DISCO_SPOT_MM 26

static const struct led_rgb disco_palette[6] = {
	{.r = 230, .g = 20, .b = 30},   /* red */
	{.r = 20, .g = 210, .b = 60},   /* green */
	{.r = 255, .g = 170, .b = 20},  /* gold */
	{.r = 255, .g = 235, .b = 210}, /* warm white */
	{.r = 220, .g = 30, .b = 170},  /* magenta */
	{.r = 60, .g = 140, .b = 255},  /* ice blue */
};

/* Where the spots are and what colour they have, for one face. */
struct disco_face {
	int x[DISCO_SPOTS], y[DISCO_SPOTS];
	struct led_rgb spot[DISCO_SPOTS];
	struct led_rgb base, beat, beat_inner;
};

/* Mirror-ball spots and the beat colour of one face. The back face turns the
 * other way in the complementary colours (shift 3). Computed once per frame.
 */
static void disco_face(struct disco_face *f, uint32_t t, int dir, int shift, uint8_t pulse,
		       uint32_t bar)
{
	static const uint8_t radius[DISCO_SPOTS] = {28, 52, 74, 92, 62};
	static const uint8_t speed[DISCO_SPOTS] = {9, 7, 5, 4, 6};

	for (int k = 0; k < DISCO_SPOTS; k++) {
		int sdx, sdy;
		int turn = (k % 2 ? -1 : 1) * dir;

		direction((uint8_t)(k * 51 + turn * (int)(t * speed[k] / 100U)), &sdx, &sdy);
		f->x[k] = sdx * radius[k] / 256;
		f->y[k] = sdy * radius[k] / 256;
		f->spot[k] = disco_palette[(bar + k + shift) % 6];
	}
	f->base = color_scale(disco_palette[(bar + shift + 2) % 6], 14);
	/* The outline flashes on every beat and fades until the next. */
	f->beat = color_scale(disco_palette[(bar + shift) % 6], pulse);
	/* The inner star joins in, softer. */
	f->beat_inner = color_scale(disco_palette[(bar + shift + 1) % 6], (uint8_t)(pulse / 2));
}

static struct led_rgb disco_led(const struct led_info *l, const struct disco_face *f)
{
	struct led_rgb c = f->base;

	for (int k = 0; k < DISCO_SPOTS; k++) {
		int dx = l->x - f->x[k];
		int dy = l->y - f->y[k];
		int d2 = dx * dx + dy * dy;

		if (d2 < DISCO_SPOT_MM * DISCO_SPOT_MM) {
			uint8_t lum = (uint8_t)(255 - d2 * 255 / (DISCO_SPOT_MM * DISCO_SPOT_MM));

			c = color_add(c, color_scale(f->spot[k], lum));
		}
	}
	c = color_add(c, l->group == LED_GROUP_OUTLINE ? f->beat : f->beat_inner);
	return c;
}

static void disco_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	uint32_t beat_phase = t % DISCO_BEAT_MS;
	uint32_t bar = t / DISCO_BAR_MS;
	/* Sharp attack, quick decay (squared). */
	uint32_t p = 255U - beat_phase * 255U / DISCO_BEAT_MS;
	uint8_t pulse = (uint8_t)(p * p / 255U);
	struct disco_face front, back;

	ARG_UNUSED(imu);
	ARG_UNUSED(state);
	disco_face(&front, t, 1, 0, pulse, bar);
	disco_face(&back, t, -1, 3, pulse, bar);

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		frame[i] = disco_led(l, &front);
		frame[l->partner] = disco_led(l, &back);
	}
	/* Plenty of glitter right after each beat, a little in between. */
	int glitter = beat_phase < 100U ? 8 : (fx_rand() % 3 == 0 ? 1 : 0);

	for (int n = 0; n < glitter; n++) {
		frame[fx_rand() % LED_COUNT] = (struct led_rgb){.r = 255, .g = 250, .b = 235};
	}
	/* One short white flash at the start of every phrase (well below the rates
	 * that trouble people sensitive to flashing light).
	 */
	if (t % DISCO_STROBE_MS < 70U) {
		for (int i = 0; i < LED_COUNT; i++) {
			frame[i] = color_add(frame[i], (struct led_rgb){.r = 255, .g = 255, .b = 255});
		}
	}
}

const struct effect fx_disco = {
	.name = "disco",
	.help = "Christmas disco: mirror-ball spots, lights on the beat, glitter",
	.title = "Christmas Disco",
	.subtitle = "Dance under the mirror ball",
	.flags = FX_TWO_FACES,
	.render = disco_render,
};
