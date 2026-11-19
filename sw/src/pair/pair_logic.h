/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file pair_logic.h
 * @brief Hardware-independent pairing rules and the locked-shell parser.
 *
 * Rules:
 *  - A star without an owner (never paired, or released by its owner) opens
 *    a pairing window for PAIR_WINDOW_MS after power-on. An owned star does not.
 *  - Shaking the star for PAIR_SHAKE_MS (short pauses up to PAIR_SHAKE_GAP_MS
 *    allowed) opens the window in any case.
 *  - Pairing closes the window.
 */

#define PAIR_KEY_LEN 16
#define PAIR_NONCE_LEN 16
#define PAIR_WINDOW_MS 60000
#define PAIR_SHAKE_MS 5000
#define PAIR_SHAKE_GAP_MS 300

struct pair_logic {
	bool owned;
	bool window;
	int64_t window_until;
	int64_t shake_since; /**< -1 = not shaking */
	int64_t shake_last;
};

void pair_logic_init(struct pair_logic *p, bool owned, int64_t now_ms);

/**
 * @brief Advance by one frame.
 * @return true when this tick opened the window by shaking.
 */
bool pair_logic_tick(struct pair_logic *p, int64_t now_ms, bool shaking);

void pair_logic_open(struct pair_logic *p, int64_t now_ms);
void pair_logic_paired(struct pair_logic *p);
void pair_logic_released(struct pair_logic *p);

/** @brief Milliseconds left in the pairing window (0 = closed). */
uint32_t pair_logic_window_left(const struct pair_logic *p, int64_t now_ms);

enum locked_cmd {
	LOCKED_NONE,    /**< empty line */
	LOCKED_HELLO,
	LOCKED_AUTH,    /**< arg: 64 hex digits */
	LOCKED_PAIR,    /**< arg: 32 hex digits */
	LOCKED_HELP,
	LOCKED_DENIED,  /**< anything else */
};

/**
 * @brief Classify a line received while the Bluetooth shell is locked.
 * @param line NUL-terminated line without CR/LF; may be modified.
 * @param arg  set to the argument (hex text) for AUTH and PAIR.
 */
enum locked_cmd pair_parse_locked(char *line, const char **arg);

/** @brief Parse exactly @p len bytes of hex text. @return 0 or -EINVAL. */
int pair_hex_decode(const char *hex, uint8_t *out, size_t len);

void pair_hex_encode(const uint8_t *in, size_t len, char *out);

/** @brief Constant-time compare. */
bool pair_equal(const uint8_t *a, const uint8_t *b, size_t len);
