/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#include "layout/led_layout.h"

/**
 * @file effect_format.h
 * @brief Christmas Star effect file (.csfx) format.
 *
 * File layout (little endian):
 *   struct csfx_header                 32 bytes
 *   frame[frame_count]:
 *     uint16_t duration_ms             how long the frame is shown
 *     uint8_t  rgb[led_count][3]       R, G, B per LED in logical order
 *                                      (D201..D266, then D301..D366)
 *
 * crc32 is the IEEE CRC-32 of all frame bytes. Produced by
 * sw/tools/make_effect.py.
 */

#define CSFX_MAGIC "CSFX"
#define CSFX_VERSION 1
#define CSFX_FLAG_LOOP BIT(0)

#define CSFX_FRAME_SIZE (2U + LED_COUNT * 3U)  /* 398 */
#define CSFX_FRAMES_PER_PAGE 5U                 /* 5 * 398 = 1990 <= 2048 */
#define CSFX_PAGE_DATA (CSFX_FRAME_SIZE * CSFX_FRAMES_PER_PAGE)

struct csfx_header {
	char magic[4];
	uint8_t version;
	uint8_t led_count;
	uint16_t flags;
	char name[16];
	uint32_t frame_count;
	uint32_t crc32;
} __packed;

BUILD_ASSERT(sizeof(struct csfx_header) == 32, "header must be 32 bytes");

/**
 * @brief Validate a header.
 * @return 0 if valid, -EINVAL if malformed, -EFBIG if it has more than max_frames frames.
 */
int csfx_header_check(const struct csfx_header *hdr, uint32_t max_frames);

/**
 * @brief Location of a frame inside a slot.
 *
 * Frames start at logical block 1 of the slot (block 0 holds the header).
 */
void csfx_frame_location(uint32_t frame, uint32_t *logical_block, uint32_t *page, uint16_t *col);
