/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file sha256.h
 * @brief Compact SHA-256 and HMAC-SHA256 (FIPS 180-4, RFC 2104) for pairing.
 */

#define SHA256_LEN 32
#define SHA256_BLOCK 64

struct sha256 {
	uint32_t h[8];
	uint64_t bits;
	uint8_t buf[SHA256_BLOCK];
	size_t fill;
};

void sha256_init(struct sha256 *s);
void sha256_update(struct sha256 *s, const void *data, size_t len);
void sha256_final(struct sha256 *s, uint8_t out[SHA256_LEN]);

void sha256(const void *data, size_t len, uint8_t out[SHA256_LEN]);

/** @brief HMAC-SHA256 over the concatenation of two message parts. */
void hmac_sha256(const uint8_t *key, size_t key_len, const void *m1, size_t m1_len,
		 const void *m2, size_t m2_len, uint8_t out[SHA256_LEN]);
