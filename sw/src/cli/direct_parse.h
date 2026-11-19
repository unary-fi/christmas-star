/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * @file direct_parse.h
 * @brief Argument parsing for set_led / set_all.
 *
 * Side: 0 = both faces, 1 = front (D2xx), 2 = back (D3xx).
 * Index: 1..66 = designator number.
 */

/**
 * @brief Parse a side argument ("0", "1" or "2").
 * @return side, or -1 if invalid.
 */
int direct_parse_side(const char *s);

/**
 * @brief Parse an LED index argument ("1".."66").
 * @return index, or -1 if invalid.
 */
int direct_parse_index(const char *s);

/** @brief Logical range [first, last] covered by a whole side. */
void direct_side_range(int side, int *first, int *last);
