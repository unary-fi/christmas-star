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

static uint8_t inner(uint8_t step)
{
	for (uint8_t i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].group == LED_GROUP_INNER && led_layout[i].step == step % 18) {
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

/* The inner-star tip joined to an outline node, or COMET_NONE. */
static uint8_t spoke_of(uint8_t v)
{
	for (int k = 0; k < g.deg[v]; k++) {
		if (led_layout[g.nb[v][k]].group != led_layout[v].group) {
			return g.nb[v][k];
		}
	}
	return COMET_NONE;
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
		int junctions = 0;

		for (int i = face * LED_FACE_COUNT; i < (face + 1) * LED_FACE_COUNT; i++) {
			zassert_true(g.deg[i] == 2 || g.deg[i] == 3, "node %d deg %d", i, g.deg[i]);
			junctions += g.deg[i] == 3;
			for (int k = 0; k < g.deg[i]; k++) {
				uint8_t n = g.nb[i][k];

				zassert_true(linked(n, (uint8_t)i), "edge %d-%d one way", i, n);
				zassert_equal(n / LED_FACE_COUNT, face, "edge %d-%d crosses faces", i, n);
			}
		}
		zassert_equal(junctions, 12, "face %d: %d junctions", face, junctions);
	}
	/* Concave vertices and inner tips are the junctions, joined by spokes. */
	for (int o = 0; o < 48; o += 8) {
		uint8_t v = outline((uint8_t)o);
		uint8_t t = spoke_of(v);

		zassert_true(comet_is_junction(&g, v));
		zassert_not_equal(t, COMET_NONE);
		zassert_true(led_layout[t].r > 40, "spoke to a flank");
	}
	/* The back is the same track through the partners. */
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		zassert_equal(g.deg[partner(i)], g.deg[i]);
		for (int k = 0; k < g.deg[i]; k++) {
			zassert_true(linked(partner(i), partner(g.nb[i][k])));
		}
	}
}

ZTEST(comet, test_outer_junction)
{
	/* Clockwise along the outline into a concave vertex: the centre is on the right. */
	uint8_t from = outline(7), v = outline(8), line = outline(9);
	uint8_t tip = spoke_of(v);
	bool used;

	zassert_equal(comet_choose(&g, from, v, 0, &used), line);
	zassert_false(used);
	zassert_equal(comet_choose(&g, from, v, COMET_RIGHT, &used), tip);
	zassert_true(used);
	/* No left turn off the line here: follow it, the tap is not used. */
	zassert_equal(comet_choose(&g, from, v, COMET_LEFT, &used), line);
	zassert_false(used);

	/* Seen from behind, the same move is mirrored: the spoke is on the left. */
	zassert_equal(comet_choose(&g, partner(from), partner(v), COMET_LEFT, &used), partner(tip));
	zassert_true(used);
	zassert_equal(comet_choose(&g, partner(from), partner(v), COMET_RIGHT, &used),
		      partner(line));
	zassert_false(used);
}

ZTEST(comet, test_inner_tip)
{
	/* Find an inner tip and walk into it clockwise along the inner star. */
	uint8_t tip = COMET_NONE, before = COMET_NONE, after = COMET_NONE;

	for (uint8_t s = 0; s < 18; s++) {
		if (led_layout[inner(s)].r > 40) {
			tip = inner(s);
			before = inner((uint8_t)(s + 17));
			after = inner((uint8_t)(s + 1));
			break;
		}
	}
	zassert_not_equal(tip, COMET_NONE);
	uint8_t v = spoke_of(tip);
	bool used;

	zassert_equal(comet_choose(&g, before, tip, 0, NULL), after);
	/* Clockwise on the inner star, the way out is on the left. */
	zassert_equal(comet_choose(&g, before, tip, COMET_LEFT, &used), v);
	zassert_true(used);
	zassert_equal(comet_choose(&g, before, tip, COMET_RIGHT, &used), after);
	zassert_false(used);

	/* In on the spoke: clockwise by default, either way when armed. */
	zassert_equal(comet_choose(&g, v, tip, 0, NULL), after);
	uint8_t l = comet_choose(&g, v, tip, COMET_LEFT, &used);

	zassert_true(used);
	uint8_t r = comet_choose(&g, v, tip, COMET_RIGHT, &used);

	zassert_true(used);
	zassert_true((l == before && r == after) || (l == after && r == before));
	zassert_not_equal(l, r);
}

ZTEST(comet, test_turn_at_next_junction)
{
	struct comet_game s;

	start(&s, COMET_SINGLE, 1);
	no_items(&s);
	/* Starts at the top tip (outline 4) going clockwise; the next junction is outline 8. */
	zassert_equal(comet_head(&s), outline(4));
	comet_input(&s, &g, COMET_RIGHT);
	zassert_equal(s.armed, COMET_RIGHT);
	while (comet_head(&s) != outline(8)) {
		step_once(&s);
		zassert_equal(s.state, COMET_PLAY);
	}
	zassert_equal(s.next, spoke_of(outline(8)));
	zassert_equal(s.armed, 0, "used at the junction");

	/* A left tap there is dropped: no left turn off the line. */
	start(&s, COMET_SINGLE, 1);
	no_items(&s);
	comet_input(&s, &g, COMET_LEFT);
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
