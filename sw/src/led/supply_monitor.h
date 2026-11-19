/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/**
 * @file supply_monitor.h
 * @brief 3V3 rail measurement with the ADC's internal reference (VREFINT).
 *
 * VDDA is tied to the 3V3 rail, so VDDA = 3.3 V * VREFINT_CAL / VREFINT_DATA.
 * One conversion takes about 21 us (ADC clock PCLK/4 = 12 MHz, 239.5-cycle
 * sampling as VREFINT requires).
 */

/** @brief Calibrate and enable the ADC. @return 0 or -EIO. */
int supply_monitor_init(void);

/**
 * @brief Lowest 3V3 voltage of @p samples back-to-back conversions, in mV.
 * @return 0 if the monitor is not running.
 */
uint16_t supply_monitor_min_mv(int samples);
