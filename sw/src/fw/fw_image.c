/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "fw_image.h"

#include <errno.h>

/* Room kept at the end of a slot for MCUboot's swap status and trailer. */
#define TRAILER_RESERVE 4096U

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

int fw_image_parse(const uint8_t *buf, size_t len, uint32_t slot_size, struct fw_image_info *out)
{
	/* struct image_header: magic, load_addr, hdr_size(16), protect_tlv_size(16),
	 * img_size, flags, version {major, minor, revision(16), build(32)}, pad.
	 */
	if (len < FW_IMAGE_HEADER_LEN || le32(buf) != FW_IMAGE_MAGIC) {
		return -EINVAL;
	}
	out->hdr_size = le16(buf + 8);
	out->protect_tlv_size = le16(buf + 10);
	out->img_size = le32(buf + 12);
	out->major = buf[20];
	out->minor = buf[21];
	out->revision = le16(buf + 22);
	out->build = le32(buf + 24);
	if (out->hdr_size < FW_IMAGE_HEADER_LEN || out->img_size == 0) {
		return -EINVAL;
	}
	if (slot_size < TRAILER_RESERVE ||
	    (uint64_t)out->hdr_size + out->img_size + out->protect_tlv_size >
		    slot_size - TRAILER_RESERVE) {
		return -EFBIG;
	}
	return 0;
}
