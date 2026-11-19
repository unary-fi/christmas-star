/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "bm78_ad.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AD_MAX 31
#define AD_NAME_SHORT 0x08
#define AD_NAME_COMPLETE 0x09
#define AD_UUID128_SOME 0x06
#define AD_UUID128_ALL 0x07

/* 49535343-fe7d-4ae5-8fa9-9fafd205e455, little-endian. */
static const uint8_t service_ad[18] = {
	17, AD_UUID128_ALL, 0x55, 0xe4, 0x05, 0xd2, 0xaf, 0x9f, 0xa9, 0x8f,
	0xe5, 0x4a, 0x7d, 0xfe, 0x43, 0x53, 0x53, 0x49,
};

int bm78_name_valid(const char *name)
{
	size_t n = strlen(name);

	if (n < 1 || n > BM78_NAME_MAX) {
		return -EINVAL;
	}
	for (size_t i = 0; i < n; i++) {
		if (name[i] < 32 || name[i] > 126) {
			return -EINVAL;
		}
	}
	return 0;
}

void bm78_default_name(const uint8_t addr_le[6], char out[BM78_NAME_MAX + 1])
{
	snprintf(out, BM78_NAME_MAX + 1, "Star-%02X%02X", addr_le[1], addr_le[0]);
}

/* Check the AD structure chain; returns the number of structures or -EINVAL. */
static int check(const uint8_t *block)
{
	int total = block[0];
	int i = 1;
	int count = 0;

	if (total > AD_MAX) {
		return -EINVAL;
	}
	while (i < 1 + total) {
		int n = block[i];

		if (n == 0 || i + 1 + n > 1 + total) {
			return -EINVAL;
		}
		i += 1 + n;
		count++;
	}
	return count;
}

/*
 * Rebuild a block from its AD structures: `skip(type)` drops structures,
 * `first` (may be NULL) is put in front.
 */
static int rebuild(uint8_t block[BM78_AD_BLOCK], const uint8_t *first, size_t first_len,
		   bool (*skip)(uint8_t type))
{
	uint8_t out[BM78_AD_BLOCK] = {0};
	size_t len = 0;
	int total = block[0];
	int i = 1;

	if (first != NULL) {
		memcpy(out + 1, first, first_len);
		len = first_len;
	}
	while (i < 1 + total) {
		int n = block[i];

		if (!skip(block[i + 1])) {
			if (len + 1 + n > AD_MAX) {
				return -EINVAL;
			}
			memcpy(out + 1 + len, block + i, 1 + n);
			len += 1 + n;
		}
		i += 1 + n;
	}
	out[0] = (uint8_t)len;
	memcpy(block, out, BM78_AD_BLOCK);
	return 0;
}

static bool is_name(uint8_t type)
{
	return type == AD_NAME_SHORT || type == AD_NAME_COMPLETE;
}

static bool is_uuid128(uint8_t type)
{
	return type == AD_UUID128_SOME || type == AD_UUID128_ALL;
}

int bm78_ad_set_name(uint8_t block[BM78_AD_BLOCK], const char *name)
{
	uint8_t ad[2 + BM78_NAME_MAX];
	size_t n = strlen(name);

	if (check(block) < 1 || bm78_name_valid(name) != 0) {
		return -EINVAL;
	}
	ad[0] = (uint8_t)(1 + n);
	ad[1] = AD_NAME_COMPLETE;
	memcpy(ad + 2, name, n);
	/* The name goes first, as in the module's factory data. */
	return rebuild(block, ad, 2 + n, is_name);
}

int bm78_ad_add_service(uint8_t block[BM78_AD_BLOCK])
{
	int total = block[0];

	if (check(block) < 0) {
		return -EINVAL;
	}
	for (int i = 1; i < 1 + total; i += 1 + block[i]) {
		if (block[i] == sizeof(service_ad) - 1 &&
		    memcmp(block + i, service_ad, sizeof(service_ad)) == 0) {
			return 0;
		}
	}
	return rebuild(block, service_ad, sizeof(service_ad), is_uuid128) == 0 ? 1 : -EINVAL;
}
