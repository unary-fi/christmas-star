/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game/comet_logic.h"

/**
 * @file game.h
 * @brief A game session on the star: the apps start it, steer it and read
 * its status; the game itself runs as the effect fx_cometgame.
 */

/* Inputs other than turns, collected for the engine thread. */
#define GAME_IN_FLIP (1u << 0)
#define GAME_IN_PAUSE (1u << 1)
#define GAME_IN_RESUME (1u << 2)

struct game_status {
	bool active;    /* the game is on the LEDs */
	uint8_t state;  /* enum comet_state */
	uint8_t mode;   /* enum comet_mode */
	uint8_t face;   /* face of the head, enum led_face */
	uint8_t level;
	uint8_t len;
	uint16_t score;
};

/** @brief Remember the current effect and start a new game. */
void game_start(uint8_t mode);

/** @brief Leave the game and go back to the effect it replaced. */
void game_stop(void);

/** @brief COMET_LEFT, COMET_RIGHT or COMET_FLIP. */
void game_input(uint8_t input);
void game_pause(bool pause);

/** @brief The Bluetooth link dropped: pause a running game. */
void game_link_lost(void);

void game_get_status(struct game_status *st);
uint16_t game_best(uint8_t mode);

/* ---- For the game effect (engine thread) ---------------------------------- */

uint8_t game_requested_mode(void);
/** @brief Take the pending inputs: the last turn (0 if none) and GAME_IN_* flags. */
void game_take_input(uint8_t *turn, uint32_t *flags);
void game_publish(const struct comet_game *s);
/** @brief A game ended with this score; keeps the best per mode. */
void game_finished(uint8_t mode, uint16_t score);
/** @brief Go back to the previous effect (from the engine thread). */
void game_request_stop(void);
