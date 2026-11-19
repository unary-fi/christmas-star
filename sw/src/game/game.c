/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "game.h"
#include "effects/effect.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

static atomic_t mode_req = ATOMIC_INIT(COMET_SINGLE);
static atomic_t turn_req;
static atomic_t flags_req;

static struct k_spinlock status_lock;
static struct game_status status;

static uint16_t best[2];

/* The effect the game replaced, and its frame if it was the manual picture. */
static const struct effect *prev_fx;
static struct led_rgb prev_frame[LED_COUNT];

static int set_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	uint16_t v;

	if (len != sizeof(v)) {
		return 0;
	}
	if (strcmp(key, "best1") == 0 && read_cb(cb_arg, &v, sizeof(v)) == sizeof(v)) {
		best[0] = v;
	} else if (strcmp(key, "best2") == 0 && read_cb(cb_arg, &v, sizeof(v)) == sizeof(v)) {
		best[1] = v;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(game, "game", NULL, set_cb, NULL, NULL);

static void save_best(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)settings_save_one("game/best1", &best[0], sizeof(best[0]));
	(void)settings_save_one("game/best2", &best[1], sizeof(best[1]));
}
static K_WORK_DEFINE(save_work, save_best);

static void stop_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	game_stop();
}
static K_WORK_DEFINE(stop_work, stop_handler);

static bool running(void)
{
	return engine_get() == &fx_cometgame;
}

void game_start(uint8_t mode)
{
	const struct effect *cur = engine_get();

	if (cur != &fx_cometgame) {
		prev_fx = cur;
		if (cur == &fx_manual) {
			engine_copy_frame(prev_frame);
		}
	}
	atomic_set(&mode_req, mode == COMET_DOUBLE ? COMET_DOUBLE : COMET_SINGLE);
	atomic_clear(&turn_req);
	atomic_clear(&flags_req);
	/* Setting the effect again restarts it: a fresh game. */
	engine_set(&fx_cometgame);
}

void game_stop(void)
{
	if (!running()) {
		return;
	}
	if (prev_fx == &fx_manual) {
		engine_manual_load(prev_frame);
	} else {
		engine_set(prev_fx != NULL && prev_fx != &fx_cometgame ? prev_fx : &fx_golden);
	}
}

void game_input(uint8_t input)
{
	if (!running()) {
		return;
	}
	if (input == COMET_FLIP) {
		atomic_or(&flags_req, GAME_IN_FLIP);
	} else if (input == COMET_LEFT || input == COMET_RIGHT) {
		atomic_set(&turn_req, input);
	}
}

void game_pause(bool pause)
{
	if (running()) {
		atomic_or(&flags_req, pause ? GAME_IN_PAUSE : GAME_IN_RESUME);
	}
}

void game_link_lost(void)
{
	game_pause(true);
}

void game_get_status(struct game_status *st)
{
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	*st = status;
	k_spin_unlock(&status_lock, key);
	st->active = running();
}

uint16_t game_best(uint8_t mode)
{
	return best[mode == COMET_DOUBLE ? 1 : 0];
}

uint8_t game_requested_mode(void)
{
	return (uint8_t)atomic_get(&mode_req);
}

void game_take_input(uint8_t *turn, uint32_t *flags)
{
	*turn = (uint8_t)atomic_clear(&turn_req);
	*flags = (uint32_t)atomic_clear(&flags_req);
}

void game_publish(const struct comet_game *s)
{
	k_spinlock_key_t key = k_spin_lock(&status_lock);

	status.state = s->state;
	status.mode = s->mode;
	status.face = (uint8_t)(comet_head(s) >= LED_FACE_COUNT ? LED_FACE_BACK : LED_FACE_FRONT);
	status.level = s->level;
	status.len = s->len;
	status.score = s->score;
	k_spin_unlock(&status_lock, key);
}

void game_finished(uint8_t mode, uint16_t score)
{
	uint16_t *b = &best[mode == COMET_DOUBLE ? 1 : 0];

	if (score > *b) {
		*b = score;
		k_work_submit(&save_work);
	}
}

void game_request_stop(void)
{
	k_work_submit(&stop_work);
}
