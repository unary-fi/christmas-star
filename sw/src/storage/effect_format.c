/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "effect_format.h"
#include "zd35_nand.h"

#include <errno.h>
#include <string.h>

int csfx_header_check(const struct csfx_header *hdr, uint32_t max_frames)
{
	if (memcmp(hdr->magic, CSFX_MAGIC, 4) != 0 || hdr->version != CSFX_VERSION ||
	    hdr->led_count != LED_COUNT || hdr->frame_count == 0) {
		return -EINVAL;
	}
	if (hdr->frame_count > max_frames) {
		return -EFBIG;
	}
	return 0;
}

void csfx_frame_location(uint32_t frame, uint32_t *logical_block, uint32_t *page, uint16_t *col)
{
	uint32_t data_page = frame / CSFX_FRAMES_PER_PAGE;

	*logical_block = 1U + data_page / NAND_PAGES_PER_BLOCK;
	*page = data_page % NAND_PAGES_PER_BLOCK;
	*col = (uint16_t)((frame % CSFX_FRAMES_PER_PAGE) * CSFX_FRAME_SIZE);
}
