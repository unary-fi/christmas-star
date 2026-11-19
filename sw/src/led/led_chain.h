/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

#include "layout/led_layout.h"

/**
 * @file led_chain.h
 * @brief Logical 132-LED frame -> physical LED chain.
 *
 * Effects render into a logical frame indexed by the layout's logical index.
 * The chain applies gamma, global brightness and a current limit, drops LEDs
 * that are missing from the board (their pads are bridged so the data skips
 * them), and sends the result to the strip.
 */

#define LED_MISSING_BYTES ((LED_COUNT + 7) / 8)

int led_chain_init(void);

/** @brief Send a logical frame to the LEDs. Not thread safe; one caller. */
int led_chain_show(const struct led_rgb frame[LED_COUNT]);

void led_chain_set_missing(int idx, bool missing);
bool led_chain_is_missing(int idx);
void led_chain_clear_missing(void);
int led_chain_present_count(void);
void led_chain_get_missing(uint8_t bitmap[LED_MISSING_BYTES]);
void led_chain_set_missing_bitmap(const uint8_t bitmap[LED_MISSING_BYTES]);

/**
 * @brief Mark LEDs from a space-separated list of names as missing.
 * @return Number of names that could not be parsed.
 */
int led_chain_missing_from_string(const char *list);

void led_chain_set_brightness(uint8_t level);
uint8_t led_chain_get_brightness(void);

void led_chain_set_limit_ma(uint16_t ma);
uint16_t led_chain_get_limit_ma(void);

/** @brief Last error from the LED strip driver (0 = none) and error count. */
int led_chain_last_error(uint32_t *count);

/** @brief Estimated current of the last shown frame in mA (after limiting). */
uint32_t led_chain_last_ma(void);

/**
 * @brief Pure helper: scale factor (0..256) that keeps the given channel sum
 * under the limit. 256 = no scaling. Exposed for unit tests.
 */
uint16_t led_chain_limit_scale(uint32_t channel_sum, uint16_t ma_per_channel, uint16_t limit_ma);

/* ---- Supply-aware current limit ------------------------------------------------ */

/** 3V3 below this (mV) counts as a sag: the LED current limit is cut. */
#define SUPPLY_SAG_MV 3200
/** Below this the BM78 has probably browned out: cut harder and reset it. */
#define SUPPLY_DEEP_SAG_MV 3000
/** The limit only rises again while the rail is at least this high. */
#define SUPPLY_OK_MV 3270
/** Calm frames (50 fps) before the limit starts rising: 30 s. */
#define SUPPLY_CALM_FRAMES 1500
/** Then +1 mA every this many frames (5 mA/s). */
#define SUPPLY_RAISE_EVERY 10
#define SUPPLY_LIMIT_FLOOR_MA 100
/** Used when the configured limit is 0 (off). */
#define SUPPLY_LIMIT_CEILING_MA 10000

/**
 * Adaptive current limit learnt from the 3V3 rail. On hardware 1.0 the +5V
 * input sags under LED load (connector, SS38 diode, supply), the TPS62056
 * drops out and the BM78 (3.3 V minimum) browns out while the MCU keeps
 * running. Each frame the limiter sees the rail voltage measured during the
 * previous frame and the current that frame drew.
 */
struct supply_limiter {
	uint16_t limit_ma;    /**< current adaptive limit */
	uint16_t calm_frames; /**< frames since the last sag (saturating) */
	uint32_t sags;        /**< number of sags seen */
	uint32_t deep_sags;   /**< number of deep sags seen */
};

/** @brief Start with the configured limit. */
void supply_limiter_init(struct supply_limiter *s, uint16_t max_ma);

/**
 * @brief Pure helper: update the adaptive limit with one measurement.
 * @param vdd_mv  lowest 3V3 rail voltage seen during the last frame
 * @param last_ma estimated current of the last frame
 * @param max_ma  configured limit (0 = off)
 * @return true on a deep sag (BM78 probably browned out).
 */
bool supply_limiter_update(struct supply_limiter *s, uint16_t vdd_mv, uint32_t last_ma,
			   uint16_t max_ma);

/** @brief Supply limiter state for status output. */
struct led_supply_status {
	uint16_t vdd_mv;      /**< last measured 3V3 (0 = no measurement) */
	uint16_t vdd_min_mv;  /**< lowest 3V3 since boot */
	uint16_t limit_ma;    /**< adaptive limit */
	uint32_t sags;
	uint32_t deep_sags;
};

void led_chain_supply_status(struct led_supply_status *st);
