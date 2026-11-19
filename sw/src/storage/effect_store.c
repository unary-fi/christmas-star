/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "effect_store.h"
#include "zd35_nand.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#define MAX_BLOCKS 2048

static uint8_t bad_map[MAX_BLOCKS / 8];
static uint32_t blocks;
static uint32_t good_blocks;

/* Logical -> physical lookup cache (frames are read sequentially). */
static uint32_t cache_lb = UINT32_MAX;
static uint32_t cache_pb;

static struct {
	bool active;
	struct csfx_header hdr;
	uint32_t received;     /* file bytes consumed so far */
	uint32_t total;        /* expected file size, 0 until header parsed */
	uint32_t crc;
	uint32_t data_page;    /* current data page index */
	uint16_t col;          /* fill level of the current page cache */
	uint32_t block;        /* physical block of the current page */
} wr;

static bool is_bad(uint32_t b)
{
	return (bad_map[b / 8] & BIT(b % 8)) != 0;
}

/* Physical block of a logical block, or -1 if beyond the end. */
static int32_t phys_block(uint32_t logical)
{
	uint32_t lb = 0, pb = 0;

	if (cache_lb != UINT32_MAX && logical >= cache_lb) {
		lb = cache_lb;
		pb = cache_pb;
	} else {
		while (pb < blocks && is_bad(pb)) {
			pb++;
		}
	}
	while (lb < logical && pb < blocks) {
		pb++;
		while (pb < blocks && is_bad(pb)) {
			pb++;
		}
		lb++;
	}
	if (pb >= blocks) {
		return -1;
	}
	cache_lb = lb;
	cache_pb = pb;
	return (int32_t)pb;
}

static void mark_bad(uint32_t block)
{
	(void)nand_mark_bad(block);
	bad_map[block / 8] |= (uint8_t)BIT(block % 8);
	good_blocks--;
	cache_lb = UINT32_MAX;
}

int effect_store_init(void)
{
	blocks = MIN(nand_block_count(), (uint32_t)MAX_BLOCKS);
	good_blocks = 0;
	memset(bad_map, 0, sizeof(bad_map));
	for (uint32_t b = 0; b < blocks; b++) {
		if (nand_block_is_bad(b)) {
			bad_map[b / 8] |= (uint8_t)BIT(b % 8);
		} else {
			good_blocks++;
		}
	}
	cache_lb = UINT32_MAX;
	return good_blocks > 1 ? 0 : -ENODEV;
}

bool effect_store_ready(void)
{
	return good_blocks > 1;
}

uint32_t effect_store_capacity(void)
{
	return good_blocks > 1 ? (good_blocks - 1) * NAND_PAGES_PER_BLOCK * CSFX_FRAMES_PER_PAGE : 0;
}

int effect_store_bad_blocks(void)
{
	return (int)(blocks - good_blocks);
}

bool effect_store_busy(void)
{
	return wr.active;
}

int effect_store_info(struct csfx_header *hdr)
{
	int32_t blk;
	int ret;

	if (!effect_store_ready()) {
		return -ENODEV;
	}
	if (wr.active) {
		return -EBUSY;
	}
	blk = phys_block(0);
	if (blk < 0) {
		return -ENOSPC;
	}
	ret = nand_read((uint32_t)blk, 0, 0, hdr, sizeof(*hdr));
	if (ret != 0) {
		return ret;
	}
	return csfx_header_check(hdr, effect_store_capacity()) == 0 ? 0 : -ENOENT;
}

int effect_store_read_frame(uint32_t frame, uint16_t *duration_ms, struct led_rgb *rgb)
{
	uint32_t lb, page;
	uint16_t col, dur;
	int32_t blk;
	int ret;

	if (wr.active) {
		return -EBUSY;
	}
	csfx_frame_location(frame, &lb, &page, &col);
	blk = phys_block(lb);
	if (blk < 0) {
		return -EINVAL;
	}

	nand_lock();
	ret = nand_read((uint32_t)blk, page, col, &dur, sizeof(dur));
	if (ret == 0) {
		ret = nand_read((uint32_t)blk, page, col + 2, rgb, LED_COUNT * 3);
	}
	nand_unlock();

	if (ret == 0) {
		*duration_ms = sys_le16_to_cpu(dur);
	}
	return ret;
}

int effect_store_delete(void)
{
	int32_t blk;
	int ret;

	if (wr.active) {
		return -EBUSY;
	}
	blk = phys_block(0);
	if (blk < 0) {
		return -ENOSPC;
	}
	ret = nand_erase((uint32_t)blk);
	if (ret == -EIO) {
		mark_bad((uint32_t)blk);
	}
	return ret;
}

int effect_store_write_begin(void)
{
	int32_t blk;
	int ret;

	if (!effect_store_ready()) {
		return -ENODEV;
	}
	if (wr.active) {
		return -EBUSY;
	}
	blk = phys_block(0);
	if (blk < 0) {
		return -ENOSPC;
	}
	/* Invalidate the old effect first. */
	ret = nand_erase((uint32_t)blk);
	if (ret != 0) {
		if (ret == -EIO) {
			mark_bad((uint32_t)blk);
		}
		return ret;
	}
	memset(&wr, 0, sizeof(wr));
	wr.active = true;
	return 0;
}

uint32_t effect_store_write_remaining(void)
{
	if (!wr.active || wr.total == 0) {
		return UINT32_MAX;
	}
	return wr.total - wr.received;
}

static int flush_page(void)
{
	int ret = nand_program(wr.block, wr.data_page % NAND_PAGES_PER_BLOCK);

	if (ret == -EIO) {
		mark_bad(wr.block);
	}
	wr.data_page++;
	wr.col = 0;
	return ret;
}

static int write_frames(const uint8_t *data, size_t len)
{
	while (len > 0) {
		if (wr.col == 0) {
			uint32_t lb = 1U + wr.data_page / NAND_PAGES_PER_BLOCK;
			int32_t blk = phys_block(lb);

			if (blk < 0) {
				return -ENOSPC;
			}
			wr.block = (uint32_t)blk;
			if (wr.data_page % NAND_PAGES_PER_BLOCK == 0) {
				int ret = nand_erase(wr.block);

				if (ret != 0) {
					if (ret == -EIO) {
						mark_bad(wr.block);
					}
					return ret;
				}
			}
		}

		size_t chunk = MIN(len, (size_t)(CSFX_PAGE_DATA - wr.col));
		int ret = nand_load(wr.block, wr.col, data, chunk, wr.col == 0);

		if (ret != 0) {
			return ret;
		}
		wr.crc = crc32_ieee_update(wr.crc, data, chunk);
		wr.col += (uint16_t)chunk;
		wr.received += (uint32_t)chunk;
		data += chunk;
		len -= chunk;

		if (wr.col == CSFX_PAGE_DATA || wr.received == wr.total) {
			ret = flush_page();
			if (ret != 0) {
				return ret;
			}
		}
	}
	return 0;
}

int effect_store_write(const uint8_t *data, size_t len)
{
	if (!wr.active) {
		return -EINVAL;
	}

	/* Collect the header. */
	while (len > 0 && wr.received < sizeof(wr.hdr)) {
		((uint8_t *)&wr.hdr)[wr.received++] = *data++;
		len--;
		if (wr.received == sizeof(wr.hdr)) {
			int ret = csfx_header_check(&wr.hdr, effect_store_capacity());

			if (ret != 0) {
				return ret;
			}
			wr.total = sizeof(wr.hdr) + sys_le32_to_cpu(wr.hdr.frame_count) * CSFX_FRAME_SIZE;
		}
	}
	if (wr.total == 0) {
		return 0;
	}
	/* Ignore padding after the last frame (XMODEM pads with 0x1A). */
	len = MIN(len, (size_t)(wr.total - wr.received));
	return write_frames(data, len);
}

int effect_store_write_end(bool commit)
{
	int ret = 0;

	if (!wr.active) {
		return -EINVAL;
	}
	if (commit) {
		if (wr.total == 0 || wr.received != wr.total) {
			ret = -EMSGSIZE;
		} else if (wr.crc != sys_le32_to_cpu(wr.hdr.crc32)) {
			ret = -EBADMSG;
		} else {
			int32_t blk = phys_block(0);

			ret = nand_load((uint32_t)blk, 0, &wr.hdr, sizeof(wr.hdr), true);
			if (ret == 0) {
				ret = nand_program((uint32_t)blk, 0);
			}
		}
	}
	wr.active = false;
	return ret;
}
