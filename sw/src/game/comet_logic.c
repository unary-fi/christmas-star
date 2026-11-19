/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "comet_logic.h"

#include <string.h>

#define OUTLINE_LEDS 48
#define INNER_LEDS 18
#define INNER_TIP_MIN_R 40 /* inner-star tips are at r = 50 mm, the flanks at 30 */

static void link(struct comet_graph *g, uint8_t a, uint8_t b)
{
	for (int k = 0; k < g->deg[a]; k++) {
		if (g->nb[a][k] == b) {
			return;
		}
	}
	if (g->deg[a] < 3 && g->deg[b] < 3) {
		g->nb[a][g->deg[a]++] = b;
		g->nb[b][g->deg[b]++] = a;
	}
}

void comet_graph_build(struct comet_graph *g, const struct led_info *L)
{
	uint8_t outline[OUTLINE_LEDS];
	uint8_t inner[INNER_LEDS];

	memset(g, 0, sizeof(*g));
	g->layout = L;
	memset(outline, COMET_NONE, sizeof(outline));
	memset(inner, COMET_NONE, sizeof(inner));
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		if (L[i].group == LED_GROUP_OUTLINE && L[i].step < OUTLINE_LEDS) {
			outline[L[i].step] = i;
		} else if (L[i].group == LED_GROUP_INNER && L[i].step < INNER_LEDS) {
			inner[L[i].step] = i;
		}
	}
	for (int s = 0; s < OUTLINE_LEDS; s++) {
		link(g, outline[s], outline[(s + 1) % OUTLINE_LEDS]);
	}
	for (int s = 0; s < INNER_LEDS; s++) {
		link(g, inner[s], inner[(s + 1) % INNER_LEDS]);
	}
	/* Spokes: each inner tip to the nearest concave vertex of the outline. */
	for (int s = 0; s < INNER_LEDS; s++) {
		uint8_t t = inner[s];
		uint8_t best = COMET_NONE;
		int32_t best_d = INT32_MAX;

		if (L[t].r < INNER_TIP_MIN_R) {
			continue;
		}
		for (int o = 0; o < OUTLINE_LEDS; o += 8) {
			uint8_t v = outline[o];
			int32_t dx = L[v].x - L[t].x;
			int32_t dy = L[v].y - L[t].y;
			int32_t d = dx * dx + dy * dy;

			if (d < best_d) {
				best_d = d;
				best = v;
			}
		}
		link(g, t, best);
	}
	/* The back face is the same track through the partners. */
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		uint8_t p = L[i].partner;

		g->deg[p] = g->deg[i];
		for (int k = 0; k < g->deg[i]; k++) {
			g->nb[p][k] = L[g->nb[i][k]].partner;
		}
	}
}

/* > 0 if going from cur to c is a left turn for a viewer of cur's face. */
static int32_t side(const struct comet_graph *g, uint8_t from, uint8_t cur, uint8_t c)
{
	const struct led_info *L = g->layout;
	int32_t ix = L[cur].x - L[from].x;
	int32_t iy = L[cur].y - L[from].y;
	int32_t ox = L[c].x - L[cur].x;
	int32_t oy = L[c].y - L[cur].y;
	int32_t cr = ix * oy - iy * ox;

	/* Seen from behind, the front-view picture is mirrored. */
	return L[cur].face == LED_FACE_BACK ? -cr : cr;
}

uint8_t comet_choose(const struct comet_graph *g, uint8_t from, uint8_t cur, uint8_t armed,
		     bool *used)
{
	const struct led_info *L = g->layout;
	uint8_t c[2];
	int n = 0;

	if (used != NULL) {
		*used = false;
	}
	for (int k = 0; k < g->deg[cur]; k++) {
		if (g->nb[cur][k] != from && n < 2) {
			c[n++] = g->nb[cur][k];
		}
	}
	if (n == 0) {
		return from;
	}
	if (n == 1) {
		return c[0];
	}

	bool c0_left = side(g, from, cur, c[0]) > side(g, from, cur, c[1]);
	uint8_t left = c0_left ? c[0] : c[1];
	uint8_t right = c0_left ? c[1] : c[0];

	if (L[from].group == L[cur].group) {
		/* On a line: follow it unless the turn off it is to the armed side. */
		uint8_t line = L[c[0]].group == L[cur].group ? c[0] : c[1];
		uint8_t off = line == c[0] ? c[1] : c[0];

		if ((armed == COMET_LEFT && off == left) || (armed == COMET_RIGHT && off == right)) {
			if (used != NULL) {
				*used = true;
			}
			return off;
		}
		return line;
	}
	/* Arrived on a spoke: both ways leave the line. */
	if (armed == COMET_LEFT || armed == COMET_RIGHT) {
		if (used != NULL) {
			*used = true;
		}
		return armed == COMET_LEFT ? left : right;
	}
	/* Clockwise in the front view: position x direction < 0. */
	int32_t cw = L[cur].x * (L[c[0]].y - L[cur].y) - L[cur].y * (L[c[0]].x - L[cur].x);

	return cw < 0 ? c[0] : c[1];
}

static uint32_t rnd(struct comet_game *s)
{
	uint32_t x = s->rng;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	s->rng = x;
	return x;
}

uint8_t comet_speed(uint8_t level)
{
	int v = COMET_SPEED_START + (level > 0 ? level - 1 : 0);

	return (uint8_t)(v > COMET_SPEED_MAX ? COMET_SPEED_MAX : v);
}

bool comet_on_body(const struct comet_game *s, uint8_t node)
{
	for (uint8_t k = 0; k < s->len; k++) {
		if (comet_segment(s, k) == node) {
			return true;
		}
	}
	return false;
}

static bool free_spot(const struct comet_game *s, const struct comet_graph *g, uint8_t c)
{
	uint8_t head = comet_head(s);

	if (c == s->spark || c == s->bonus || c == s->next || comet_on_body(s, c)) {
		return false;
	}
	for (int k = 0; k < g->deg[head]; k++) {
		if (g->nb[head][k] == c) {
			return false;
		}
	}
	return true;
}

static uint8_t spawn(struct comet_game *s, const struct comet_graph *g)
{
	uint8_t n = s->mode == COMET_DOUBLE ? LED_COUNT : LED_FACE_COUNT;

	for (int tries = 0; tries < 64; tries++) {
		uint8_t c = (uint8_t)(rnd(s) % n);

		if (free_spot(s, g, c)) {
			return c;
		}
	}
	for (uint8_t c = 0; c < n; c++) {
		if (free_spot(s, g, c)) {
			return c;
		}
	}
	return COMET_NONE;
}

static uint8_t outline_node(const struct comet_graph *g, uint8_t step)
{
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		if (g->layout[i].group == LED_GROUP_OUTLINE && g->layout[i].step == step) {
			return i;
		}
	}
	return 0;
}

void comet_reset(struct comet_game *s, const struct comet_graph *g, uint8_t mode, uint32_t seed)
{
	memset(s, 0, sizeof(*s));
	s->mode = mode == COMET_DOUBLE ? COMET_DOUBLE : COMET_SINGLE;
	s->state = COMET_COUNTDOWN;
	s->rng = seed != 0 ? seed : 0x2545F491u;
	s->level = 1;
	s->spark = COMET_NONE;
	s->bonus = COMET_NONE;
	s->flip_node = COMET_NONE;
	/* Head at the top tip (outline step 4), going clockwise. */
	for (uint8_t k = 0; k < COMET_START_LEN; k++) {
		s->body[k] = outline_node(g, (uint8_t)(4 - (COMET_START_LEN - 1) + k));
	}
	s->head_i = COMET_START_LEN - 1;
	s->len = COMET_START_LEN;
	s->from = comet_segment(s, 1);
	s->next = comet_choose(g, s->from, comet_head(s), 0, NULL);
	s->spark = spawn(s, g);
}

static bool hits_body(const struct comet_game *s, uint8_t node)
{
	/* The tail end moves away this step unless the comet grows. */
	uint8_t n = s->grow > 0 ? s->len : (uint8_t)(s->len - 1);

	for (uint8_t k = 0; k < n; k++) {
		if (comet_segment(s, k) == node) {
			return true;
		}
	}
	return false;
}

static void step(struct comet_game *s, const struct comet_graph *g)
{
	const struct led_info *L = g->layout;
	uint8_t head = comet_head(s);
	bool flip = s->flip && s->mode == COMET_DOUBLE;
	uint8_t nh = flip ? L[head].partner : s->next;
	uint8_t mult = s->mode == COMET_DOUBLE ? 2 : 1;

	s->flip = false;
	if (hits_body(s, nh)) {
		s->state = COMET_OVER;
		s->state_ms = 0;
		s->events |= COMET_EV_OVER;
		return;
	}
	if (s->grow > 0) {
		s->len++;
		s->grow--;
	}
	s->head_i = (uint8_t)((s->head_i + 1) % COMET_RING);
	s->body[s->head_i] = nh;

	if (flip) {
		/* Through the star: keep going the same way on the other face. */
		s->flip_node = head;
		s->from = L[s->from].partner;
		s->next = L[s->next].partner;
		s->events |= COMET_EV_FLIP;
	} else {
		bool used;

		s->from = head;
		s->next = comet_choose(g, head, nh, s->armed, &used);
		if (comet_is_junction(g, nh)) {
			if (used) {
				s->events |= COMET_EV_TURN;
			}
			s->armed = 0;
		}
	}

	if (nh == s->spark) {
		s->score += mult;
		s->grow += 2;
		s->eaten++;
		s->events |= COMET_EV_SPARK;
		if (s->eaten % COMET_SPARKS_PER_LEVEL == 0 && comet_speed(s->level) < COMET_SPEED_MAX) {
			s->level++;
			s->events |= COMET_EV_LEVEL;
		}
		s->spark = COMET_NONE;
		s->spark = spawn(s, g);
		if (s->bonus == COMET_NONE && rnd(s) % 3 == 0) {
			s->bonus = spawn(s, g);
			s->bonus_ms = COMET_BONUS_MS;
		}
	} else if (nh == s->bonus) {
		s->score += 5 * mult;
		s->grow += 2;
		s->bonus = COMET_NONE;
		s->bonus_ms = 0;
		s->events |= COMET_EV_BONUS;
	}
	if (s->len >= COMET_WIN_LEN) {
		s->state = COMET_WON;
		s->state_ms = 0;
		s->events |= COMET_EV_WON;
	}
}

void comet_tick(struct comet_game *s, const struct comet_graph *g, uint32_t dt_ms)
{
	if (dt_ms > 100) {
		dt_ms = 100;
	}
	if (s->state == COMET_PAUSED) {
		return;
	}
	s->state_ms += dt_ms;
	if (s->state == COMET_COUNTDOWN) {
		if (s->state_ms >= COMET_COUNTDOWN_MS) {
			s->state = COMET_PLAY;
			s->state_ms = 0;
		}
		return;
	}
	if (s->state != COMET_PLAY) {
		return;
	}
	if (s->bonus != COMET_NONE) {
		if (s->bonus_ms <= dt_ms) {
			s->bonus = COMET_NONE;
			s->bonus_ms = 0;
		} else {
			s->bonus_ms = (uint16_t)(s->bonus_ms - dt_ms);
		}
	}
	s->phase = (uint16_t)(s->phase + dt_ms * comet_speed(s->level));
	while (s->phase >= 1000 && s->state == COMET_PLAY) {
		s->phase = (uint16_t)(s->phase - 1000);
		step(s, g);
	}
	if (s->state != COMET_PLAY) {
		s->phase = 0;
	}
}

void comet_input(struct comet_game *s, const struct comet_graph *g, uint8_t input)
{
	uint8_t head = comet_head(s);

	if (s->state != COMET_PLAY && s->state != COMET_COUNTDOWN) {
		return;
	}
	if (input == COMET_FLIP) {
		if (s->mode == COMET_DOUBLE) {
			s->flip = true;
		}
		return;
	}
	if (input != COMET_LEFT && input != COMET_RIGHT) {
		return;
	}
	s->armed = input;
	/* A late tap: the head has just reached a junction and not yet left it. */
	if (comet_is_junction(g, head) && s->phase < 500 && !s->flip) {
		bool used;
		uint8_t n = comet_choose(g, s->from, head, input, &used);

		if (used) {
			s->next = n;
			s->armed = 0;
			s->events |= COMET_EV_TURN;
		}
	}
}

void comet_pause(struct comet_game *s, bool pause)
{
	if (pause && (s->state == COMET_PLAY || s->state == COMET_COUNTDOWN)) {
		s->paused_from = s->state;
		s->state = COMET_PAUSED;
	} else if (!pause && s->state == COMET_PAUSED) {
		/* Resume with a fresh countdown so the player is ready. */
		s->state = COMET_COUNTDOWN;
		s->state_ms = 0;
	}
}

uint8_t comet_switch_light(const struct comet_game *s, const struct comet_graph *g)
{
	uint8_t prev = comet_head(s);
	uint8_t cur = s->next;

	if (s->armed == 0 || s->state != COMET_PLAY || s->flip) {
		return COMET_NONE;
	}
	for (int i = 0; i < 12; i++) {
		if (comet_is_junction(g, cur)) {
			bool used;
			uint8_t n = comet_choose(g, prev, cur, s->armed, &used);

			return used ? n : COMET_NONE;
		}
		uint8_t n = comet_choose(g, prev, cur, 0, NULL);

		prev = cur;
		cur = n;
	}
	return COMET_NONE;
}
