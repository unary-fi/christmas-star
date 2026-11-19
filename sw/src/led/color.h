/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

/** Gamma 2.2 correction table. */
extern const uint8_t color_gamma[256];

/**
 * @brief Parse an HTML-style colour: "0xRRGGBB", "#RRGGBB" or "RRGGBB".
 * @return 0 on success, -EINVAL on a malformed value.
 */
int color_parse(const char *s, struct led_rgb *out);

/** @brief Hue (0..255) to a fully saturated colour at the given value. */
struct led_rgb color_hsv(uint8_t h, uint8_t s, uint8_t v);

/** @brief Scale a colour by level/255. */
static inline struct led_rgb color_scale(struct led_rgb c, uint8_t level)
{
	return (struct led_rgb){
		.r = (uint8_t)((c.r * level + 127) / 255),
		.g = (uint8_t)((c.g * level + 127) / 255),
		.b = (uint8_t)((c.b * level + 127) / 255),
	};
}

/** @brief Saturating add of two colours. */
static inline struct led_rgb color_add(struct led_rgb a, struct led_rgb b)
{
	unsigned int r = a.r + b.r, g = a.g + b.g, bl = a.b + b.b;

	return (struct led_rgb){
		.r = (uint8_t)(r > 255 ? 255 : r),
		.g = (uint8_t)(g > 255 ? 255 : g),
		.b = (uint8_t)(bl > 255 ? 255 : bl),
	};
}
