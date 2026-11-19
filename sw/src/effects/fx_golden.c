/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * "Golden Star": a warm gold glow with a slow shimmer travelling around the
 * star. (Shaking lights off works with every effect through the shake
 * overlay, see shake_overlay.c.)
 */

#include "effect.h"
#include "led/color.h"

static void golden_render(struct led_rgb *frame, uint32_t t_ms, const struct imu_state *imu, void *state)
{
	ARG_UNUSED(state);
	ARG_UNUSED(imu);

	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		uint8_t shimmer = fx_tri((uint8_t)(t_ms / 10 + l->angle + l->r));

		frame[i] = color_scale((struct led_rgb){.r = 255, .g = 165, .b = 35},
				       (uint8_t)(170 + shimmer * 85 / 255));
	}
}

const struct effect fx_golden = {
	.title = "Golden Star",
	.subtitle = "Warm gold, gently shimmering",
	.name = "golden",
	.help = "Warm gold glow with a gentle shimmer",
	.render = golden_render,
};
