/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/drivers/led_strip.h>

#include "effect_format.h"

/**
 * @file effect_store.h
 * @brief The one effect stored in the SPI NAND.
 *
 * The whole NAND holds a single effect (one at a time). Bad blocks are
 * skipped. Logical block 0 holds the header page, which is written last, so
 * an interrupted upload leaves the store empty. With 2048 blocks the capacity
 * is about 655 000 frames (3.6 h at 50 fps).
 */

int effect_store_init(void);

/** @brief True if the NAND is available. */
bool effect_store_ready(void);

/** @brief Frames that fit (depends on bad blocks). */
uint32_t effect_store_capacity(void);

/** @brief Read the header. @return 0, -ENOENT if empty, or an error. */
int effect_store_info(struct csfx_header *hdr);

/** @brief Read one frame (rgb must hold LED_COUNT pixels). */
int effect_store_read_frame(uint32_t frame, uint16_t *duration_ms, struct led_rgb *rgb);

int effect_store_delete(void);

/** @brief Count of bad blocks found in the whole device. */
int effect_store_bad_blocks(void);

/** @brief True while an upload is in progress. */
bool effect_store_busy(void);

/* Streaming writer used by the XMODEM upload. */
int effect_store_write_begin(void);
/** @brief Feed file bytes; extra bytes after the last frame are ignored. */
int effect_store_write(const uint8_t *data, size_t len);
/** @brief Finish: verifies size and CRC, then writes the header if commit. */
int effect_store_write_end(bool commit);
/** @brief Bytes still expected (0 when complete, UINT32_MAX before the header). */
uint32_t effect_store_write_remaining(void);
