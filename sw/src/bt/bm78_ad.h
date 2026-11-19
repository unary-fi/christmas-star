/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file bm78_ad.h
 * @brief Name and advertising fields of the BM78 configuration EEPROM.
 *
 * Verified on BM78SPPS5MC2 (see sw/tools/bm78_bridge.py, which implements the
 * same logic): the public address at 0x0000 (little-endian), the device name
 * at 0x000B (zero padded), the advertising data at 0x03A0 and the scan
 * response at 0x03C0 (each: total length, then AD structures, max 31 bytes).
 */

#define BM78_EE_ADDR 0x0000
#define BM78_EE_NAME 0x000B
#define BM78_EE_ADV 0x03A0
#define BM78_EE_SCAN 0x03C0
#define BM78_AD_BLOCK 32   /**< length byte + 31 bytes of AD structures */
#define BM78_NAME_MAX 16

/** @brief Check a star name: 1..16 printable ASCII characters. */
int bm78_name_valid(const char *name);

/** @brief "Star-XXYY" from the last two address bytes. */
void bm78_default_name(const uint8_t addr_le[6], char out[BM78_NAME_MAX + 1]);

/**
 * @brief Replace the complete-name AD in an advertising block.
 * @param block in/out, BM78_AD_BLOCK bytes; unused bytes are zeroed.
 * @return 0, or -EINVAL if the block is malformed or the name does not fit.
 */
int bm78_ad_set_name(uint8_t block[BM78_AD_BLOCK], const char *name);

/**
 * @brief Make sure the scan response lists the Transparent UART service.
 * @return 1 if changed, 0 if already present, -EINVAL if malformed or full.
 */
int bm78_ad_add_service(uint8_t block[BM78_AD_BLOCK]);
