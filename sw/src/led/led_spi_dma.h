/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <zephyr/drivers/led_strip.h>

/**
 * @file led_spi_dma.h
 * @brief Low-level LED chain output (SPI2 TX-only + DMA, PC3).
 */

#define LED_SPI_DMA_MAX_LEDS DT_PROP(DT_NODELABEL(led_strip), chain_length)

int led_spi_dma_init(void);

/** @brief Send pixels and latch them. Blocks for about 5 ms (sleeping). */
int led_spi_dma_show(const struct led_rgb *pixels, size_t count);
