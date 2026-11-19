/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>
#include <zephyr/ztest.h>

#include "fw/fw_image.h"

#define SLOT (108 * 1024)

static void header(uint8_t *h, uint32_t img_size)
{
	memset(h, 0, FW_IMAGE_HEADER_LEN);
	/* magic 0x96f3b83d, little-endian */
	h[0] = 0x3d; h[1] = 0xb8; h[2] = 0xf3; h[3] = 0x96;
	h[8] = 0x00; h[9] = 0x02;          /* hdr_size 0x200 */
	h[10] = 0x00; h[11] = 0x00;        /* protect_tlv_size */
	h[12] = (uint8_t)img_size;
	h[13] = (uint8_t)(img_size >> 8);
	h[14] = (uint8_t)(img_size >> 16);
	h[15] = (uint8_t)(img_size >> 24);
	h[20] = 1; h[21] = 3; h[22] = 2; h[23] = 0; /* 1.3.2 */
	h[24] = 7;                         /* build 7 */
}

ZTEST(fw, test_valid_header)
{
	uint8_t h[FW_IMAGE_HEADER_LEN];
	struct fw_image_info i;

	header(h, 82000);
	zassert_ok(fw_image_parse(h, sizeof(h), SLOT, &i));
	zassert_equal(i.major, 1);
	zassert_equal(i.minor, 3);
	zassert_equal(i.revision, 2);
	zassert_equal(i.build, 7);
	zassert_equal(i.hdr_size, 0x200);
	zassert_equal(i.img_size, 82000);
}

ZTEST(fw, test_rejects_other_files)
{
	uint8_t h[FW_IMAGE_HEADER_LEN];
	struct fw_image_info i;

	header(h, 82000);
	h[0] = 'C'; /* e.g. a .csfx effect file */
	zassert_equal(fw_image_parse(h, sizeof(h), SLOT, &i), -EINVAL);
	header(h, 82000);
	zassert_equal(fw_image_parse(h, 16, SLOT, &i), -EINVAL);
	header(h, 0);
	zassert_equal(fw_image_parse(h, sizeof(h), SLOT, &i), -EINVAL);
}

ZTEST(fw, test_rejects_too_big)
{
	uint8_t h[FW_IMAGE_HEADER_LEN];
	struct fw_image_info i;

	header(h, SLOT - 0x200 - 4096);
	zassert_ok(fw_image_parse(h, sizeof(h), SLOT, &i));
	header(h, SLOT - 0x200 - 4096 + 1);
	zassert_equal(fw_image_parse(h, sizeof(h), SLOT, &i), -EFBIG);
}

ZTEST_SUITE(fw, NULL, NULL, NULL, NULL, NULL);
