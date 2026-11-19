/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "comet_logic.h"

#include <string.h>

#define OUTLINE_LEDS 48
#define CIRCLE_LEDS 12
#define PASSAGES 6
#define PASSAGE_MIN_R 40 /* former inner-star tips are at r = 50 mm, the circle at 30 */

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

static int32_t dist2(const struct led_info *L, uint8_t a, uint8_t b)
{
	int32_t dx = L[a].x - L[b].x;
	int32_t dy = L[a].y - L[b].y;

	return dx * dx + dy * dy;
}

void comet_graph_build(struct comet_graph *g, const struct led_info *L)
{
	uint8_t outline[OUTLINE_LEDS];
	uint8_t circle[CIRCLE_LEDS];
	uint8_t tips[PASSAGES];
	int nc = 0, nt = 0;

	memset(g, 0, sizeof(*g));
	g->layout = L;
	memset(outline, 0, sizeof(outline));
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		if (L[i].group == LED_GROUP_OUTLINE) {
			g->kind[i] = COMET_OUTLINE;
			if (L[i].step < OUTLINE_LEDS) {
				outline[L[i].step] = i;
			}
		} else if (L[i].r < PASSAGE_MIN_R) {
			g->kind[i] = COMET_CIRCLE;
			if (nc < CIRCLE_LEDS) {
				circle[nc++] = i;
			}
		} else {
			g->kind[i] = COMET_PASSAGE;
			if (nt < PASSAGES) {
				tips[nt++] = i;
			}
		}
	}
	for (int s = 0; s < OUTLINE_LEDS; s++) {
		link(g, outline[s], outline[(s + 1) % OUTLINE_LEDS]);
	}
	/* The inner circle, in angle order. */
	for (int a = 1; a < nc; a++) {
		for (int b = a; b > 0 && L[circle[b]].angle < L[circle[b - 1]].angle; b--) {
			uint8_t t = circle[b];

			circle[b] = circle[b - 1];
			circle[b - 1] = t;
		}
	}
	for (int s = 0; s < nc; s++) {
		link(g, circle[s], circle[(s + 1) % nc]);
	}
	/* Passages: each tip to the concave vertex outside it and the two circle LEDs flanking it. */
	for (int t = 0; t < nt; t++) {
		uint8_t tip = tips[t];
		uint8_t v = outline[0];
		uint8_t c1 = circle[0], c2 = circle[1];

		for (int o = 0; o < OUTLINE_LEDS; o += 8) {
			if (dist2(L, outline[o], tip) < dist2(L, v, tip)) {
				v = outline[o];
			}
		}
		if (dist2(L, c2, tip) < dist2(L, c1, tip)) {
			c1 = circle[1];
			c2 = circle[0];
		}
		for (int c = 2; c < nc; c++) {
			if (dist2(L, circle[c], tip) < dist2(L, c1, tip)) {
				c2 = c1;
				c1 = circle[c];
			} else if (dist2(L, circle[c], tip) < dist2(L, c2, tip)) {
				c2 = circle[c];
			}
		}
		link(g, tip, v);
		link(g, tip, c1);
		link(g, tip, c2);
	}
	/* The back face is the same track through the partners. */
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		uint8_t p = L[i].partner;

		g->deg[p] = g->deg[i];
		g->kind[p] = g->kind[i];
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

/* Direction of travel around the centre when moving from a to b (front view). */
static int8_t rot_of(const struct comet_graph *g, uint8_t a, uint8_t b)
{
	const struct led_info *L = g->layout;
	int32_t cr = L[a].x * (L[b].y - L[a].y) - L[a].y * (L[b].x - L[a].x);

	return cr < 0 ? COMET_CW : COMET_CCW;
}

static bool linked(const struct comet_graph *g, uint8_t a, uint8_t b)
{
	for (int k = 0; k < g->deg[a]; k++) {
		if (g->nb[a][k] == b) {
			return true;
		}
	}
	return false;
}

void comet_exits_at(const struct comet_graph *g, uint8_t from, uint8_t cur, int8_t rot,
		    struct comet_exits *e)
{
	uint8_t c[2];
	int n = 0;

	e->dflt = from;
	e->alt = COMET_NONE;
	e->alt_side = 0;
	e->fork = false;
	for (int k = 0; k < g->deg[cur]; k++) {
		if (g->nb[cur][k] != from && n < 2) {
			c[n++] = g->nb[cur][k];
		}
	}
	if (n == 0) {
		return;
	}
	e->dflt = c[0];
	if (n == 1) {
		return;
	}

	uint8_t kf = g->kind[from], kc = g->kind[cur];

	if (kc == COMET_PASSAGE && kf == COMET_CIRCLE) {
		/* Into a passage from the circle: on out to the outline. */
		e->dflt = g->kind[c[0]] == COMET_OUTLINE ? c[0] : c[1];
		return;
	}
	if (kc == COMET_CIRCLE && kf == COMET_PASSAGE) {
		/* Out of a passage onto the circle: on round it, not back under the passage. */
		e->dflt = linked(g, from, c[0]) ? c[1] : c[0];
		return;
	}

	bool c0_left = side(g, from, cur, c[0]) > side(g, from, cur, c[1]);

	if (kf == kc) {
		/* On a line: follow it; the way off it is the alternative. */
		uint8_t line = g->kind[c[0]] == kc ? c[0] : c[1];

		e->dflt = line;
		e->alt = line == c[0] ? c[1] : c[0];
		e->alt_side = (e->alt == c[0]) == c0_left ? COMET_LEFT : COMET_RIGHT;
		return;
	}
	/* End of a passage: both ways leave it; keep the latest direction of travel. */
	e->fork = true;
	e->dflt = rot_of(g, cur, c[0]) == rot ? c[0] : c[1];
	e->alt = e->dflt == c[0] ? c[1] : c[0];
	e->alt_side = (e->alt == c[0]) == c0_left ? COMET_LEFT : COMET_RIGHT;
}

/*
 * @param last The side of the latest tap (0 if none yet): at the end of a
 *        passage, without a new tap, the comet turns that way again.
 */
static uint8_t pick(const struct comet_exits *e, uint8_t armed, uint8_t last, bool *used)
{
	if (used != NULL) {
		*used = false;
	}
	if (e->alt == COMET_NONE) {
		return e->dflt;
	}
	if (armed == 0) {
		return e->fork && last == e->alt_side ? e->alt : e->dflt;
	}
	if (armed == e->alt_side) {
		if (used != NULL) {
			*used = true;
		}
		return e->alt;
	}
	if (e->fork) {
		/* At a fork any tap chooses: the other side is the default way. */
		if (used != NULL) {
			*used = true;
		}
	}
	return e->dflt;
}

uint8_t comet_choose(const struct comet_graph *g, uint8_t from, uint8_t cur, int8_t rot,
		     uint8_t armed, bool *used)
{
	struct comet_exits e;

	comet_exits_at(g, from, cur, rot, &e);
	return pick(&e, armed, 0, used);
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
	return !linked(g, head, c);
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
	s->rot = rot_of(g, s->from, comet_head(s));
	s->next = comet_choose(g, s->from, comet_head(s), s->rot, 0, NULL);
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
		struct comet_exits e;
		bool used;

		if (g->kind[head] == g->kind[nh] && g->kind[nh] != COMET_PASSAGE) {
			s->rot = rot_of(g, head, nh);
		}
		s->from = head;
		comet_exits_at(g, head, nh, s->rot, &e);
		s->next = pick(&e, s->armed, s->last_tap, &used);
		if (e.alt != COMET_NONE) {
			/* A place to turn: the tap is used here or forgotten. */
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
	s->last_tap = input;
	/* A late tap: the head has just reached a place to turn and not yet left it. */
	if (s->phase < 500 && !s->flip) {
		struct comet_exits e;
		bool used;

		comet_exits_at(g, s->from, head, s->rot, &e);
		if (e.alt != COMET_NONE) {
			uint8_t n = pick(&e, input, s->last_tap, &used);

			if (used) {
				s->next = n;
				s->armed = 0;
				s->events |= COMET_EV_TURN;
			}
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

uint8_t comet_next_turn(const struct comet_game *s, const struct comet_graph *g, bool *armed)
{
	uint8_t prev = comet_head(s);
	uint8_t cur = s->next;
	int8_t rot = s->rot;

	*armed = false;
	if ((s->state != COMET_PLAY && s->state != COMET_COUNTDOWN) || s->flip) {
		return COMET_NONE;
	}
	for (int i = 0; i < 16; i++) {
		struct comet_exits e;

		comet_exits_at(g, prev, cur, rot, &e);
		if (e.alt != COMET_NONE) {
			bool used;
			uint8_t n = pick(&e, s->armed, s->last_tap, &used);

			*armed = used;
			/* With a tap: where it goes. Without: the way it would not go by itself. */
			return used ? n : (n == e.alt ? e.dflt : e.alt);
		}
		if (g->kind[cur] == g->kind[e.dflt] && g->kind[cur] != COMET_PASSAGE) {
			rot = rot_of(g, cur, e.dflt);
		}
		prev = cur;
		cur = e.dflt;
	}
	return COMET_NONE;
}
