/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "effect.h"
#include "storage/effect_store.h"

#include <string.h>

static struct csfx_header hdr;
static uint32_t frame_no;
static uint32_t next_ms;
static bool ok;

static void player_start(struct led_rgb *frame, void *state)
{
	ARG_UNUSED(state);
	memset(frame, 0, sizeof(struct led_rgb) * LED_COUNT);
	frame_no = 0;
	next_ms = 0;
	ok = effect_store_info(&hdr) == 0;
}

static void player_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);
	uint16_t dur;

	if (!ok || t_ms < next_ms) {
		return;
	}
	if (frame_no >= hdr.frame_count) {
		if (!(hdr.flags & CSFX_FLAG_LOOP)) {
			return;
		}
		frame_no = 0;
	}
	if (effect_store_read_frame(frame_no, &dur, frame) != 0) {
		ok = false;
		memset(frame, 0, sizeof(struct led_rgb) * LED_COUNT);
		return;
	}
	frame_no++;
	next_ms += MAX(dur, 1U);
	if (t_ms > next_ms + 1000U) {
		/* More than a second behind (e.g. paused): resynchronise. */
		next_ms = t_ms;
	}
}

const struct effect fx_player = {
	.flags = FX_HIDDEN,
	.name = "player",
	.help = "Plays the effect stored in the flash (effect play flash)",
	.realtime = true,
	.start = player_start,
	.render = player_render,
};
