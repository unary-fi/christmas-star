/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Comet, the game on the LEDs. The rules are in comet_logic.c; this file
 * takes the inputs, runs the game in real time and draws it.
 */

#include "game.h"
#include "effects/effect.h"
#include "led/color.h"

#include <string.h>
#include <zephyr/kernel.h>

#define FLIP_FLASH_MS 300
#define CATCH_FLASH_MS 250
#define END_TIMEOUT_MS 60000U  /* game over or paused this long: back to the effect */
#define IDLE_TIMEOUT_MS 300000U /* no input this long */

static const struct led_rgb TRACK = {.r = 0, .g = 3, .b = 14};
static const struct led_rgb HEAD = {.r = 255, .g = 225, .b = 150};
static const struct led_rgb GOLD = {.r = 255, .g = 150, .b = 15};
static const struct led_rgb EMBER = {.r = 70, .g = 8, .b = 0};
static const struct led_rgb SPARK = {.r = 255, .g = 190, .b = 50};
static const struct led_rgb BONUS = {.r = 255, .g = 10, .b = 30};
static const struct led_rgb SWITCH = {.r = 90, .g = 140, .b = 255};
static const struct led_rgb WHITE = {.r = 255, .g = 255, .b = 255};
static const struct led_rgb RED = {.r = 200, .g = 20, .b = 0};

static struct comet_graph graph;
static bool graph_ready;

struct cometfx {
	struct comet_game g;
	uint32_t last;
	uint32_t last_input;
	uint32_t state_since;
	uint16_t flip_flash;
	uint16_t catch_flash;
	uint8_t flip_at;
	uint8_t last_state;
	bool stop_sent;
};
FX_STATE_FITS(struct cometfx);

static void cg_start(struct led_rgb *frame, void *state)
{
	struct cometfx *st = state;

	if (!graph_ready) {
		comet_graph_build(&graph, led_layout);
		graph_ready = true;
	}
	memset(frame, 0, LED_COUNT * sizeof(*frame));
	comet_reset(&st->g, &graph, game_requested_mode(), k_cycle_get_32());
	st->last_state = st->g.state;
	game_publish(&st->g);
}

static inline uint8_t face_of(uint8_t n)
{
	return n >= LED_FACE_COUNT ? LED_FACE_BACK : LED_FACE_FRONT;
}

static void add(struct led_rgb *frame, uint8_t n, struct led_rgb c)
{
	if (n < LED_COUNT) {
		frame[n] = color_add(frame[n], c);
	}
}

static struct led_rgb tail_color(uint8_t k, uint8_t len)
{
	return fx_mix(GOLD, EMBER, (uint8_t)(k * 255U / len));
}

static void draw_comet(struct led_rgb *frame, const struct comet_game *g, uint8_t p)
{
	uint8_t head = comet_head(g);

	for (int k = g->len - 1; k > 0; k--) {
		struct led_rgb c = tail_color((uint8_t)k, g->len);

		if (k == g->len - 1 && g->grow == 0) {
			/* The tail end fades out as the comet moves on. */
			c = color_scale(c, (uint8_t)(255 - p));
		}
		frame[comet_segment(g, (uint8_t)k)] = c;
	}
	frame[head] = fx_mix(HEAD, GOLD, (uint8_t)(p / 2));
	/* The glow moves on towards the next LED. */
	uint8_t target = g->flip ? graph.layout[head].partner : g->next;

	if (g->state == COMET_PLAY) {
		add(frame, target, color_scale(HEAD, p));
	}
	if (g->mode == COMET_DOUBLE) {
		/* A faint glow on the other face shows where the comet is. */
		add(frame, led_layout[head].partner, color_scale(HEAD, 40));
	}
}

static void draw_items(struct led_rgb *frame, const struct comet_game *g, uint32_t t)
{
	if (g->spark != COMET_NONE) {
		uint8_t lvl = (uint8_t)(150 + fx_tri((uint8_t)(t / 4)) * 105U / 255U);

		frame[g->spark] = color_scale(SPARK, lvl);
	}
	if (g->bonus != COMET_NONE) {
		uint8_t lvl = (uint8_t)(80 + fx_tri((uint8_t)(t / 3)) * 175U / 255U);

		if (g->bonus_ms < 1500 && ((t / 100) & 1)) {
			lvl /= 3;
		}
		frame[g->bonus] = color_scale(BONUS, lvl);
	}
	/* The next place to turn: a faint white way off the line, blue once a tap will take it. */
	bool armed;
	uint8_t turn = comet_next_turn(g, &graph, &armed);

	if (turn != COMET_NONE) {
		add(frame, turn, armed ? color_scale(SWITCH, 140) : color_scale(WHITE, 28));
	}
}

static void draw_countdown(struct led_rgb *frame, const struct comet_game *g, uint32_t t)
{
	/* 3-2-1: two more star points light up each second. */
	uint8_t lit = (uint8_t)(2 * (g->state_ms / 1000 + 1));

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].group == LED_GROUP_OUTLINE && led_layout[i].point < lit) {
			uint8_t lvl = (uint8_t)(70 + fx_tri((uint8_t)(t / 4)) * 60U / 255U);

			frame[i] = color_scale(GOLD, lvl);
		}
	}
}

static void draw_over(struct led_rgb *frame, const struct comet_game *g, uint32_t t)
{
	uint8_t head = comet_head(g);
	uint32_t ms = g->state_ms;
	const struct led_info *h = &led_layout[head];

	/* A burst from the head ... */
	if (ms < 700) {
		int radius = (int)(ms / 5);

		for (int i = 0; i < LED_COUNT; i++) {
			const struct led_info *e = &led_layout[i];
			int dx = e->x - h->x;
			int dy = e->y - h->y;
			int d = (int)fx_isqrt((uint32_t)(dx * dx + dy * dy)) - radius;

			if (face_of((uint8_t)i) == h->face && d > -14 && d < 14) {
				uint8_t lvl = (uint8_t)((14 - (d < 0 ? -d : d)) * 255 / 14);

				frame[i] = color_add(frame[i], color_scale(fx_mix(WHITE, GOLD, (uint8_t)(ms / 3)),
									    (uint8_t)(lvl * (700 - ms) / 700)));
			}
		}
	}
	/* ... and the tail falls off, segment by segment. */
	int shown = (int)g->len - (int)(ms / 60);

	for (int k = 0; k < shown; k++) {
		frame[comet_segment(g, (uint8_t)k)] = fx_mix(RED, EMBER, (uint8_t)(k * 255 / g->len));
	}
	if (shown <= 0) {
		/* Then a slow red glow until the next game. */
		uint8_t lvl = (uint8_t)(10 + fx_tri((uint8_t)(t / 12)) * 40U / 255U);

		for (int i = 0; i < LED_COUNT; i++) {
			frame[i] = color_add(frame[i], color_scale(RED, lvl));
		}
	}
}

static void draw_won(struct led_rgb *frame, uint32_t t)
{
	/* A gold sweep around both rings. */
	for (int i = 0; i < LED_COUNT; i++) {
		uint8_t a = (uint8_t)(led_layout[i].angle + t / 4);

		frame[i] = color_scale(GOLD, (uint8_t)(30 + fx_tri(a) * 225U / 255U));
	}
}

static void draw(struct led_rgb *frame, struct cometfx *st, uint32_t t)
{
	const struct comet_game *g = &st->g;
	bool mirror = g->mode == COMET_SINGLE || g->state == COMET_COUNTDOWN ||
		      g->state == COMET_WON;
	uint8_t p = g->state == COMET_PLAY ? (uint8_t)(g->phase * 255U / 1000U) : 0;

	for (int i = 0; i < LED_COUNT; i++) {
		frame[i] = TRACK;
	}
	switch (g->state) {
	case COMET_COUNTDOWN:
		draw_countdown(frame, g, t);
		draw_items(frame, g, t);
		draw_comet(frame, g, 0);
		break;
	case COMET_PLAY:
	case COMET_PAUSED:
		draw_items(frame, g, t);
		draw_comet(frame, g, p);
		break;
	case COMET_OVER:
		draw_over(frame, g, t);
		break;
	case COMET_WON:
		draw_won(frame, t);
		break;
	default:
		break;
	}

	if (st->catch_flash > 0) {
		uint8_t face = face_of(comet_head(g));
		struct led_rgb c = color_scale(GOLD, (uint8_t)(st->catch_flash * 60U / CATCH_FLASH_MS));

		for (int i = 0; i < LED_COUNT; i++) {
			if (face_of((uint8_t)i) == face) {
				frame[i] = color_add(frame[i], c);
			}
		}
	}
	if (st->flip_flash > 0 && st->flip_at != COMET_NONE) {
		uint8_t lvl = (uint8_t)(st->flip_flash * 255U / FLIP_FLASH_MS);

		frame[st->flip_at] = fx_mix(frame[st->flip_at], WHITE, lvl);
		frame[led_layout[st->flip_at].partner] =
			fx_mix(frame[led_layout[st->flip_at].partner], WHITE, lvl);
	}
	if (g->state == COMET_PAUSED) {
		/* Breathing while paused. */
		uint8_t lvl = (uint8_t)(60 + fx_tri((uint8_t)(t / 8)) * 140U / 255U);

		for (int i = 0; i < LED_COUNT; i++) {
			frame[i] = color_scale(frame[i], lvl);
		}
	}
	if (mirror) {
		for (int i = 0; i < LED_FACE_COUNT; i++) {
			frame[led_layout[i].partner] = frame[i];
		}
	}
}

static void cg_render(struct led_rgb *frame, uint32_t t, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(imu);
	struct cometfx *st = state;
	struct comet_game *g = &st->g;
	uint32_t dt = MIN(t - st->last, 100U);
	uint8_t turn;
	uint32_t flags;

	if (!graph_ready) {
		return;
	}
	st->last = t;

	game_take_input(&turn, &flags);
	if (flags & GAME_IN_PAUSE) {
		comet_pause(g, true);
	}
	if (flags & GAME_IN_RESUME) {
		comet_pause(g, false);
		st->last_input = t;
	}
	if (turn != 0) {
		comet_input(g, &graph, turn);
		st->last_input = t;
	}
	if (flags & GAME_IN_FLIP) {
		comet_input(g, &graph, COMET_FLIP);
		st->last_input = t;
	}

	comet_tick(g, &graph, dt);

	uint8_t ev = g->events;

	g->events = 0;
	if (ev & COMET_EV_FLIP) {
		st->flip_flash = FLIP_FLASH_MS;
		st->flip_at = g->flip_node;
	}
	if (ev & (COMET_EV_SPARK | COMET_EV_BONUS)) {
		st->catch_flash = CATCH_FLASH_MS;
	}
	if (ev & (COMET_EV_OVER | COMET_EV_WON)) {
		game_finished(g->mode, g->score);
	}
	st->flip_flash = (uint16_t)(st->flip_flash > dt ? st->flip_flash - dt : 0);
	st->catch_flash = (uint16_t)(st->catch_flash > dt ? st->catch_flash - dt : 0);

	if (g->state != st->last_state) {
		st->last_state = g->state;
		st->state_since = t;
	}
	bool ended = g->state == COMET_OVER || g->state == COMET_WON || g->state == COMET_PAUSED;

	if (!st->stop_sent && ((ended && t - st->state_since > END_TIMEOUT_MS) ||
			       t - st->last_input > IDLE_TIMEOUT_MS)) {
		st->stop_sent = true;
		game_request_stop();
	}
	game_publish(g);
	draw(frame, st, t);
}

const struct effect fx_cometgame = {
	.name = "cometgame",
	.help = "Comet, the game (see app game)",
	.flags = FX_HIDDEN | FX_NO_SHAKE,
	.realtime = true,
	.state_size = sizeof(struct cometfx),
	.start = cg_start,
	.render = cg_render,
};
