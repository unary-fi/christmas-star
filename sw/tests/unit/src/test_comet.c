/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "game/comet_logic.h"
#include "layout/led_layout.h"

static struct comet_graph g;

static uint8_t outline(uint8_t step)
{
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].group == LED_GROUP_OUTLINE && led_layout[i].step == step % 48) {
			return i;
		}
	}
	return COMET_NONE;
}

static uint8_t partner(uint8_t n)
{
	return led_layout[n].partner;
}

static bool linked(uint8_t a, uint8_t b)
{
	for (int k = 0; k < g.deg[a]; k++) {
		if (g.nb[a][k] == b) {
			return true;
		}
	}
	return false;
}

/* The neighbour of a node of the given kind (the first one). */
static uint8_t neighbour(uint8_t n, uint8_t kind, uint8_t skip)
{
	for (int k = 0; k < g.deg[n]; k++) {
		if (g.kind[g.nb[n][k]] == kind && g.nb[n][k] != skip) {
			return g.nb[n][k];
		}
	}
	return COMET_NONE;
}

/* Clockwise in the front view going from a to b. */
static bool clockwise(uint8_t a, uint8_t b)
{
	const struct led_info *L = led_layout;

	return L[a].x * (L[b].y - L[a].y) - L[a].y * (L[b].x - L[a].x) < 0;
}

static void *setup(void)
{
	comet_graph_build(&g, led_layout);
	return NULL;
}

/* Run the countdown, then play. */
static void start(struct comet_game *s, uint8_t mode, uint32_t seed)
{
	comet_reset(s, &g, mode, seed);
	for (int i = 0; i < COMET_COUNTDOWN_MS / 100; i++) {
		comet_tick(s, &g, 100);
	}
	zassert_equal(s->state, COMET_PLAY);
}

/* Advance until the head moves one step (or the game ends). */
static void step_once(struct comet_game *s)
{
	uint8_t head_i = s->head_i;

	for (int i = 0; i < 1000 && s->head_i == head_i && s->state == COMET_PLAY; i++) {
		comet_tick(s, &g, 10);
	}
}

/* Keep sparks out of the way of a test. */
static void no_items(struct comet_game *s)
{
	s->spark = COMET_NONE;
	s->bonus = COMET_NONE;
}

ZTEST(comet, test_graph)
{
	for (int face = 0; face < 2; face++) {
		int junctions = 0, kinds[3] = {0};

		for (int i = face * LED_FACE_COUNT; i < (face + 1) * LED_FACE_COUNT; i++) {
			zassert_true(g.deg[i] == 2 || g.deg[i] == 3, "node %d deg %d", i, g.deg[i]);
			junctions += g.deg[i] == 3;
			kinds[g.kind[i]]++;
			for (int k = 0; k < g.deg[i]; k++) {
				uint8_t n = g.nb[i][k];

				zassert_true(linked(n, (uint8_t)i), "edge %d-%d one way", i, n);
				zassert_equal(n / LED_FACE_COUNT, face, "edge %d-%d crosses faces", i, n);
			}
		}
		zassert_equal(kinds[COMET_OUTLINE], 48);
		zassert_equal(kinds[COMET_CIRCLE], 12);
		zassert_equal(kinds[COMET_PASSAGE], 6);
		/* Concave vertices, passages and every circle LED. */
		zassert_equal(junctions, 6 + 6 + 12, "face %d: %d junctions", face, junctions);
	}
	/* Each passage: one concave vertex outside, two neighbouring circle LEDs inside. */
	for (int o = 0; o < 48; o += 8) {
		uint8_t v = outline((uint8_t)o);
		uint8_t p = neighbour(v, COMET_PASSAGE, COMET_NONE);

		zassert_not_equal(p, COMET_NONE);
		zassert_equal(g.deg[p], 3);
		uint8_t c1 = neighbour(p, COMET_CIRCLE, COMET_NONE);
		uint8_t c2 = neighbour(p, COMET_CIRCLE, c1);

		zassert_not_equal(c2, COMET_NONE);
		zassert_true(linked(c1, c2), "the circle runs under the passage");
		zassert_equal(neighbour(p, COMET_OUTLINE, COMET_NONE), v);
	}
	/* The circle is closed: walking along it comes back after 12 steps. */
	uint8_t first = neighbour(neighbour(outline(0), COMET_PASSAGE, COMET_NONE), COMET_CIRCLE,
				  COMET_NONE);
	uint8_t prev = first, cur = neighbour(first, COMET_CIRCLE, COMET_NONE);
	int n = 1;

	while (cur != first && n < 20) {
		uint8_t nx = neighbour(cur, COMET_CIRCLE, prev);

		prev = cur;
		cur = nx;
		n++;
	}
	zassert_equal(n, 12);
	/* The back is the same track through the partners. */
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		zassert_equal(g.deg[partner(i)], g.deg[i]);
		zassert_equal(g.kind[partner(i)], g.kind[i]);
		for (int k = 0; k < g.deg[i]; k++) {
			zassert_true(linked(partner(i), partner(g.nb[i][k])));
		}
	}
}

ZTEST(comet, test_outer_junction)
{
	/* Clockwise along the outline into a concave vertex: the centre is on the right. */
	uint8_t from = outline(7), v = outline(8), line = outline(9);
	uint8_t p = neighbour(v, COMET_PASSAGE, COMET_NONE);
	bool used;

	zassert_equal(comet_choose(&g, from, v, COMET_CW, 0, &used), line);
	zassert_false(used);
	zassert_equal(comet_choose(&g, from, v, COMET_CW, COMET_RIGHT, &used), p);
	zassert_true(used);
	/* No left turn off the line here: follow it, the tap is not used. */
	zassert_equal(comet_choose(&g, from, v, COMET_CW, COMET_LEFT, &used), line);
	zassert_false(used);

	/* Seen from behind, the same move is mirrored: the passage is on the left. */
	zassert_equal(comet_choose(&g, partner(from), partner(v), COMET_CW, COMET_LEFT, &used),
		      partner(p));
	zassert_true(used);
	zassert_equal(comet_choose(&g, partner(from), partner(v), COMET_CW, COMET_RIGHT, &used),
		      partner(line));
	zassert_false(used);
}

ZTEST(comet, test_passage)
{
	uint8_t v = outline(8);
	uint8_t p = neighbour(v, COMET_PASSAGE, COMET_NONE);
	uint8_t c1 = neighbour(p, COMET_CIRCLE, COMET_NONE);
	uint8_t c2 = neighbour(p, COMET_CIRCLE, c1);
	uint8_t cw = clockwise(p, c1) ? c1 : c2;
	uint8_t ccw = cw == c1 ? c2 : c1;
	bool used;

	/* Down the passage: the comet keeps its direction of travel ... */
	zassert_equal(comet_choose(&g, v, p, COMET_CW, 0, &used), cw);
	zassert_false(used);
	zassert_equal(comet_choose(&g, v, p, COMET_CCW, 0, NULL), ccw);
	/* ... or goes the way of a tap, whichever side. */
	uint8_t l = comet_choose(&g, v, p, COMET_CW, COMET_LEFT, &used);

	zassert_true(used);
	uint8_t r = comet_choose(&g, v, p, COMET_CW, COMET_RIGHT, &used);

	zassert_true(used);
	zassert_not_equal(l, r);
	zassert_true((l == c1 && r == c2) || (l == c2 && r == c1));

	/* Onto the circle: on round it, not back under the passage. */
	uint8_t on = comet_choose(&g, p, cw, COMET_CW, 0, NULL);

	zassert_equal(g.kind[on], COMET_CIRCLE);
	zassert_not_equal(on, ccw);
	zassert_false(linked(on, p));

	/* From the circle into the passage: straight out to the outline. */
	zassert_equal(comet_choose(&g, c1, p, COMET_CW, COMET_LEFT, NULL), v);
	zassert_equal(comet_choose(&g, c2, p, COMET_CCW, COMET_RIGHT, NULL), v);

	/* Along the circle past a passage: follow the circle unless turning out. */
	uint8_t before = neighbour(c1, COMET_CIRCLE, c2);
	struct comet_exits e;

	comet_exits_at(&g, before, c1, COMET_CW, &e);
	zassert_equal(e.dflt, c2);
	zassert_equal(e.alt, p);
	zassert_false(e.fork);
	zassert_equal(comet_choose(&g, before, c1, COMET_CW, e.alt_side, &used), p);
	zassert_true(used);
	zassert_equal(comet_choose(&g, before, c1, COMET_CW, 3 - e.alt_side, &used), c2);
	zassert_false(used);
}

ZTEST(comet, test_turn_at_next_junction)
{
	struct comet_game s;
	bool armed;

	start(&s, COMET_SINGLE, 1);
	no_items(&s);
	/* Starts at the top tip (outline 4) going clockwise; the next place to turn is outline 8. */
	zassert_equal(comet_head(&s), outline(4));
	uint8_t p = neighbour(outline(8), COMET_PASSAGE, COMET_NONE);

	zassert_equal(comet_next_turn(&s, &g, &armed), p, "shows the way in");
	zassert_false(armed);
	comet_input(&s, &g, COMET_RIGHT);
	zassert_equal(s.armed, COMET_RIGHT);
	zassert_equal(comet_next_turn(&s, &g, &armed), p);
	zassert_true(armed);
	while (comet_head(&s) != outline(8)) {
		step_once(&s);
		zassert_equal(s.state, COMET_PLAY);
	}
	zassert_equal(s.next, p);
	zassert_equal(s.armed, 0, "used at the junction");
	/* At the end of the passage it turns right again: the latest tap. */
	step_once(&s);
	zassert_equal(comet_head(&s), p);
	struct comet_exits e;

	comet_exits_at(&g, outline(8), p, s.rot, &e);
	zassert_true(e.fork);
	zassert_equal(s.next, e.alt_side == COMET_RIGHT ? e.alt : e.dflt, "turns right");

	/* Without any tap yet, the end of a passage keeps the direction of travel. */
	start(&s, COMET_SINGLE, 1);
	no_items(&s);
	while (comet_head(&s) != outline(7)) {
		step_once(&s);
	}
	s.next = outline(8);
	s.from = outline(6);
	while (comet_head(&s) != outline(8)) {
		step_once(&s);
	}
	s.next = p; /* into the passage without a tap */
	step_once(&s);
	zassert_equal(comet_head(&s), p);
	zassert_true(clockwise(p, s.next));

	/* A left tap there is dropped: no left turn off the line. */
	start(&s, COMET_SINGLE, 1);
	no_items(&s);
	comet_input(&s, &g, COMET_LEFT);
	zassert_equal(comet_next_turn(&s, &g, &armed), p);
	zassert_false(armed);
	while (comet_head(&s) != outline(8)) {
		step_once(&s);
	}
	zassert_equal(s.next, outline(9));
	zassert_equal(s.armed, 0, "dropped at the junction");
}

ZTEST(comet, test_flip)
{
	struct comet_game s;

	start(&s, COMET_DOUBLE, 2);
	no_items(&s);
	step_once(&s);
	uint8_t head = comet_head(&s), next = s.next;

	comet_input(&s, &g, COMET_FLIP);
	step_once(&s);
	zassert_equal(comet_head(&s), partner(head), "through the star");
	zassert_equal(s.next, partner(next), "same direction on the other side");
	zassert_true(comet_head(&s) >= LED_FACE_COUNT);
	step_once(&s);
	zassert_equal(comet_head(&s), partner(next));

	/* Flipping onto the tail ends the game. */
	start(&s, COMET_DOUBLE, 3);
	no_items(&s);
	head = comet_head(&s);
	s.body[(uint8_t)(s.head_i - 1) % COMET_RING] = partner(head);
	comet_input(&s, &g, COMET_FLIP);
	step_once(&s);
	zassert_equal(s.state, COMET_OVER);

	/* One-sided games ignore flips. */
	start(&s, COMET_SINGLE, 4);
	no_items(&s);
	comet_input(&s, &g, COMET_FLIP);
	zassert_false(s.flip);
	step_once(&s);
	zassert_true(comet_head(&s) < LED_FACE_COUNT);
}

ZTEST(comet, test_sparks_and_growth)
{
	struct comet_game s;
	bool back = false;

	for (uint32_t seed = 1; seed < 200; seed++) {
		comet_reset(&s, &g, COMET_SINGLE, seed);
		zassert_true(s.spark < LED_FACE_COUNT, "front only");
		zassert_false(comet_on_body(&s, s.spark));
		comet_reset(&s, &g, COMET_DOUBLE, seed);
		zassert_false(comet_on_body(&s, s.spark));
		back |= s.spark >= LED_FACE_COUNT;
	}
	zassert_true(back, "double-sided sparks appear on the back too");

	start(&s, COMET_SINGLE, 5);
	s.bonus = COMET_NONE;
	s.spark = s.next;
	step_once(&s);
	zassert_equal(s.score, 1);
	zassert_equal(s.grow, 2);
	zassert_not_equal(s.spark, COMET_NONE);
	zassert_false(comet_on_body(&s, s.spark));
	step_once(&s);
	step_once(&s);
	zassert_equal(s.len, COMET_START_LEN + 2);

	/* Double-sided sparks count twice. */
	start(&s, COMET_DOUBLE, 6);
	s.bonus = COMET_NONE;
	s.spark = s.next;
	step_once(&s);
	zassert_equal(s.score, 2);
}

ZTEST(comet, test_levels_and_tail)
{
	struct comet_game s;

	zassert_equal(comet_speed(1), COMET_SPEED_START);
	zassert_equal(comet_speed(2), COMET_SPEED_START + 1);
	zassert_equal(comet_speed(100), COMET_SPEED_MAX);

	start(&s, COMET_SINGLE, 7);
	for (int i = 0; i < COMET_SPARKS_PER_LEVEL; i++) {
		s.bonus = COMET_NONE;
		s.spark = s.next;
		step_once(&s);
	}
	zassert_equal(s.level, 2);

	/* Running into the tail ends the game. */
	start(&s, COMET_SINGLE, 8);
	no_items(&s);
	s.body[(uint8_t)(s.head_i - 1) % COMET_RING] = s.next;
	s.len = 3;
	s.grow = 0;
	step_once(&s);
	zassert_equal(s.state, COMET_OVER);
}

ZTEST(comet, test_replay)
{
	struct comet_game a, b;
	static const uint8_t inputs[] = {COMET_RIGHT, COMET_LEFT, COMET_FLIP, COMET_RIGHT, COMET_FLIP};

	start(&a, COMET_DOUBLE, 1234);
	start(&b, COMET_DOUBLE, 1234);
	for (int t = 0; t < 60000 && a.state == COMET_PLAY; t += 20) {
		if (t % 700 == 0) {
			uint8_t in = inputs[(t / 700) % ARRAY_SIZE(inputs)];

			comet_input(&a, &g, in);
			comet_input(&b, &g, in);
		}
		comet_tick(&a, &g, 20);
		comet_tick(&b, &g, 20);
	}
	zassert_mem_equal(&a, &b, sizeof(a));
}

ZTEST(comet, test_pause)
{
	struct comet_game s;

	start(&s, COMET_SINGLE, 9);
	uint8_t head = comet_head(&s);

	comet_pause(&s, true);
	zassert_equal(s.state, COMET_PAUSED);
	for (int i = 0; i < 50; i++) {
		comet_tick(&s, &g, 100);
	}
	zassert_equal(comet_head(&s), head);
	comet_pause(&s, false);
	zassert_equal(s.state, COMET_COUNTDOWN, "resume counts down first");
}

ZTEST_SUITE(comet, NULL, setup, NULL, NULL, NULL);
