/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "led/led_chain.h"

struct imu_state;
struct shell;

/**
 * @file pairing.h
 * @brief Star ownership, the locked Bluetooth shell and star renaming.
 *
 * The Bluetooth (UART) shell starts locked and locks again whenever the link
 * drops. While locked it only answers "app hello", "app auth <hmac>" and
 * "app pair <key>" (see pair_logic.h for the rules). "app hello" hands out a
 * fresh nonce; "app auth" must carry HMAC-SHA256(key, "auth" | nonce) for the
 * owner key or the developer key (sw/dev_key.txt, built in). The J-Link
 * (RTT) shell is never locked.
 */

enum pair_access {
	PAIR_ACCESS_NONE,
	PAIR_ACCESS_OWNER,
	PAIR_ACCESS_DEV,
	PAIR_ACCESS_LOCAL, /**< J-Link (RTT) shell */
};

/** @brief Start pairing (after settings are loaded): lock the BT shell. */
int pairing_init(void);

/** @brief Called by the effect engine every frame. */
void pairing_tick(int64_t now_ms, const struct imu_state *imu);

/** @brief Draw pairing feedback (window open, paired, released) over a frame. */
void pairing_overlay_apply(struct led_rgb *out, int64_t now_ms);

/** @brief Access level of a shell instance. */
enum pair_access pairing_access(const struct shell *sh);

/** @brief Write the "ok proto=2 ..." hello line with a fresh nonce. */
void pairing_hello(const struct shell *sh);

/** @brief Current star name ("" if never named through the firmware). */
const char *pairing_name(void);

bool pairing_owned(void);
uint32_t pairing_window_left(void);
