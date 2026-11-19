/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

#include "imu/lsm6ds.h"

/**
 * @file shake_overlay.h
 * @brief "Shake to drop" overlay applied on top of every effect.
 *
 * Shaking the star knocks lights loose: each loose light takes the colour its
 * LED currently shows, falls in the direction of gravity through the star and
 * leaves it at the edge. The LEDs it came from stay dark and grow back after
 * a few calm seconds. Both faces behave identically (shared XY positions).
 *
 * The engine applies the overlay to a copy of the effect's frame, so effect
 * state (e.g. a manual or streamed frame) is never modified.
 */

void shake_overlay_reset(void);

/** @brief Apply the overlay to an output frame (LED_COUNT pixels). */
void shake_overlay_apply(struct led_rgb *out, uint32_t now_ms, const struct imu_state *imu);

void shake_set_enabled(bool enabled);
bool shake_is_enabled(void);
