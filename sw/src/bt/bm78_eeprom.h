/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "bm78_ad.h"

/**
 * @file bm78_eeprom.h
 * @brief Read and write the BM78 configuration EEPROM from the MCU.
 *
 * The module is restarted in test mode, where it is a standard HCI controller
 * on its UART (115200 baud, H4). Vendor commands 0xFC29 (read) and 0xFC27
 * (write) access the EEPROM. Received bytes reach this module through
 * bm78_ee_rx(), which the owner of the UART shell routes to it during a
 * session. All functions block; call them from a thread that is not the UART
 * shell thread.
 */

/** @brief Feed bytes received from the module (from the shell bypass). */
void bm78_ee_rx(const uint8_t *data, size_t len);

/** @brief Restart the module in test mode and check that it answers HCI. */
int bm78_ee_begin(void);

/** @brief Restart the module in application mode. */
void bm78_ee_end(void);

int bm78_ee_read(uint16_t addr, uint8_t *out, size_t len);
int bm78_ee_write(uint16_t addr, const uint8_t *data, size_t len);

/**
 * @brief Rename the module (device name, advertising name) and make the scan
 * response list the Transparent UART service. Inside a begin/end session.
 * @param name new name, or NULL for the default "Star-XXYY".
 * @param out  the name that was written.
 */
int bm78_ee_rename(const char *name, char out[BM78_NAME_MAX + 1]);
