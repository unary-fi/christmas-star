/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file fw_image.h
 * @brief MCUboot image header check for firmware uploads (no flash access).
 *
 * A signed firmware file (zephyr.signed.bin) starts with the MCUboot image
 * header. Checking it on the first received block rejects wrong files before
 * anything is written; MCUboot verifies the signature itself before booting.
 */

#define FW_IMAGE_MAGIC 0x96f3b83dU
#define FW_IMAGE_HEADER_LEN 32

struct fw_image_info {
	uint8_t major;
	uint8_t minor;
	uint16_t revision;
	uint32_t build;
	uint32_t hdr_size;
	uint32_t img_size;
	uint32_t protect_tlv_size;
};

/**
 * @brief Parse and sanity-check an MCUboot image header.
 * @param buf at least FW_IMAGE_HEADER_LEN bytes from the start of the file
 * @param slot_size size of the firmware slot
 * @return 0, -EINVAL (not an image) or -EFBIG (does not fit the slot)
 */
int fw_image_parse(const uint8_t *buf, size_t len, uint32_t slot_size, struct fw_image_info *out);
