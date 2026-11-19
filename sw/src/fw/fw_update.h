/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * @file fw_update.h
 * @brief Firmware update over the command line (MCUboot), see cmd_fw.c.
 */

#include <stdint.h>

#include "led/led_chain.h"

/** @brief At start-up: a new firmware on trial confirms itself after a while. */
void fw_boot_check(void);

/**
 * @brief Show the update on the LEDs (called by the effect engine every frame):
 * a gold ring fills around the outline while the image is received, and the
 * whole star turns gold while it installs. Does nothing when no update runs.
 */
void fw_progress_overlay(struct led_rgb *out, int64_t now_ms);
