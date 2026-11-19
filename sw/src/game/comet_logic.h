/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "layout/led_layout.h"

/**
 * @file comet_logic.h
 * @brief Comet, a snake game on the star's LED lines (hardware independent).
 *
 * The track: each face has two closed rings, the 48-LED outline and the
 * 18-LED inner star. Each inner-star tip points at a concave vertex of the
 * outline; joining them ("spokes") gives 12 junctions per face. Every LED is
 * also linked to its partner at the same XY on the other face, which is how
 * the comet flips through the star in double-sided mode.
 *
 * Steering: a Left or Right tap is used at the next junction, if a turn to
 * that side exists there; otherwise it is dropped. Without a tap the comet
 * follows its own line (and goes clockwise when it arrives on a spoke).
 * Left and right are as seen by a viewer of the face the head is on.
 */

#define COMET_NODES LED_COUNT
#define COMET_NONE 0xFF
#define COMET_RING 64
#define COMET_START_LEN 3
#define COMET_WIN_LEN 40
#define COMET_COUNTDOWN_MS 3000
#define COMET_BONUS_MS 5000
#define COMET_SPARKS_PER_LEVEL 5
#define COMET_SPEED_START 6 /* steps per second at level 1 */
#define COMET_SPEED_MAX 14

enum comet_mode {
	COMET_SINGLE = 1, /* front only, mirrored to the back */
	COMET_DOUBLE = 2, /* both faces, the comet can flip */
};

enum comet_state {
	COMET_COUNTDOWN,
	COMET_PLAY,
	COMET_PAUSED,
	COMET_OVER,
	COMET_WON,
};

enum comet_input {
	COMET_LEFT = 1,
	COMET_RIGHT = 2,
	COMET_FLIP = 3,
};

/* Events for the renderer and the session, latched until cleared. */
#define COMET_EV_SPARK (1u << 0)
#define COMET_EV_BONUS (1u << 1)
#define COMET_EV_FLIP (1u << 2)
#define COMET_EV_OVER (1u << 3)
#define COMET_EV_WON (1u << 4)
#define COMET_EV_TURN (1u << 5)
#define COMET_EV_LEVEL (1u << 6)


struct comet_graph {
	const struct led_info *layout;
	uint8_t nb[COMET_NODES][3];
	uint8_t deg[COMET_NODES];
};

struct comet_game {
	uint8_t body[COMET_RING]; /* ring buffer, body[head_i] is the head */
	uint8_t head_i;
	uint8_t len;
	uint8_t grow;      /* segments still to add */
	uint8_t from;      /* node the head came from (direction) */
	uint8_t next;      /* node the head moves to on the next step */
	uint8_t mode;      /* enum comet_mode */
	uint8_t state;     /* enum comet_state */
	uint8_t paused_from;
	uint8_t armed;     /* 0, COMET_LEFT or COMET_RIGHT */
	bool flip;         /* flip on the next step */
	uint8_t spark;
	uint8_t bonus;     /* COMET_NONE if none */
	uint8_t level;     /* 1.. */
	uint8_t eaten;     /* sparks caught */
	uint8_t events;    /* COMET_EV_* */
	uint8_t flip_node; /* where the last flip left the face */
	uint16_t score;
	uint16_t phase;    /* 0..999 progress towards the next step */
	uint16_t bonus_ms; /* time left for the bonus */
	uint32_t state_ms; /* time in the current state */
	uint32_t rng;
};

/** @brief Build the track for both faces from the LED layout. */
void comet_graph_build(struct comet_graph *g, const struct led_info *layout);

/** @brief True if the node has three track neighbours. */
static inline bool comet_is_junction(const struct comet_graph *g, uint8_t n)
{
	return g->deg[n] == 3;
}

/**
 * @brief Where the comet goes from @p cur, arriving from @p from.
 * @param armed 0, COMET_LEFT or COMET_RIGHT.
 * @param used Set to true if the armed turn was taken (may be NULL).
 */
uint8_t comet_choose(const struct comet_graph *g, uint8_t from, uint8_t cur, uint8_t armed,
		     bool *used);

/** @brief Start a new game (countdown). */
void comet_reset(struct comet_game *s, const struct comet_graph *g, uint8_t mode, uint32_t seed);

void comet_input(struct comet_game *s, const struct comet_graph *g, uint8_t input);
void comet_pause(struct comet_game *s, bool pause);

/** @brief Advance the game by @p dt_ms. */
void comet_tick(struct comet_game *s, const struct comet_graph *g, uint32_t dt_ms);

/** @brief Steps per second at a level. */
uint8_t comet_speed(uint8_t level);

/** @brief The k-th segment from the head (k = 0 is the head). */
static inline uint8_t comet_segment(const struct comet_game *s, uint8_t k)
{
	return s->body[(uint8_t)(s->head_i - k) % COMET_RING];
}

static inline uint8_t comet_head(const struct comet_game *s)
{
	return comet_segment(s, 0);
}

bool comet_on_body(const struct comet_game *s, uint8_t node);

/**
 * @brief The exit an armed turn will take at the next junction ahead, or
 * COMET_NONE if there is no armed turn or it cannot be used there.
 */
uint8_t comet_switch_light(const struct comet_game *s, const struct comet_graph *g);
