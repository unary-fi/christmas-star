/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file zd35_nand.h
 * @brief Minimal driver for the Zetta ZD35 SPI NAND (U403).
 *
 * Geometry (ZD35Q2GC-IB datasheet): 2048-byte pages (+128 spare), 64 pages
 * per 128 KB block, 2048 blocks in two planes (plane = block address LSB,
 * sent as column address bit 12). JEDEC ID 2Ch 24h.
 * On-die ECC is enabled; uncorrectable pages return -EIO.
 *
 * Programming is done by loading data into the chip's page cache in pieces
 * (nand_load) and then committing it (nand_program), so no RAM page buffer
 * is needed. Hold nand_lock() across a load/program sequence.
 */

#define NAND_PAGE_SIZE 2048U
#define NAND_SPARE_SIZE 128U
#define NAND_PAGES_PER_BLOCK 64U
#define NAND_BLOCK_SIZE (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK)

int nand_init(void);

/** @brief Number of erase blocks (0 if the chip was not detected). */
uint32_t nand_block_count(void);

/** @brief Raw ID bytes (manufacturer, device, device2). */
void nand_get_id(uint8_t id[3]);

/** @brief Recursive lock for multi-step sequences. */
void nand_lock(void);
void nand_unlock(void);

/** @brief Read len bytes from a page at a column offset. */
int nand_read(uint32_t block, uint32_t page, uint16_t col, void *buf, size_t len);

/**
 * @brief Load data into the page cache.
 * @param first true for the first load of a page (clears the rest of the cache to 0xFF).
 */
int nand_load(uint32_t block, uint16_t col, const void *data, size_t len, bool first);

/** @brief Program the page cache into a page. */
int nand_program(uint32_t block, uint32_t page);

int nand_erase(uint32_t block);

/** @brief Check the factory/runtime bad-block marker. */
bool nand_block_is_bad(uint32_t block);
int nand_mark_bad(uint32_t block);
