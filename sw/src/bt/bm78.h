/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @file bm78.h
 * @brief BM78 Bluetooth SPP module power-up and status.
 *
 * The module runs its own Bluetooth stack in transparent (auto pattern)
 * mode; the MCU only talks UART to it (USART1, shell). This module drives
 * the boot straps and reset so the module starts from its application
 * firmware, and asserts the module's CTS input so it may send.
 */

int bm78_init(void);

/** @brief Reset the module and start it again in application mode. */
int bm78_reset(void);

/**
 * @brief Reset the module into application mode, or into test mode where its
 * configuration can be read and written over the UART (Microchip tools).
 */
int bm78_start(bool test_mode);

/** @brief Raw levels of the status indicator pins (P1_5, P0_4). */
void bm78_status(int *status1, int *status2);

/** @brief Level of the module's RTS output (1 = module ready to receive). */
int bm78_rts(void);

/**
 * @brief Report that the 3V3 rail dipped below the module's supply range.
 *
 * The module is reset 2 s after the last report, so it restarts cleanly once
 * the supply has recovered. Safe to call from any thread, every frame.
 */
void bm78_supply_glitch(void);

/** @brief Number of resets done after supply glitches. */
uint32_t bm78_glitch_resets(void);
