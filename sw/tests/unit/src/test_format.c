/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "storage/effect_format.h"
#include "storage/zd35_nand.h"

static struct csfx_header valid(void)
{
	struct csfx_header h;

	memset(&h, 0, sizeof(h));
	memcpy(h.magic, CSFX_MAGIC, 4);
	h.version = CSFX_VERSION;
	h.led_count = LED_COUNT;
	h.frame_count = 10;
	return h;
}

ZTEST_SUITE(format, NULL, NULL, NULL, NULL, NULL);

ZTEST(format, test_sizes)
{
	zassert_equal(CSFX_FRAME_SIZE, 398);
	zassert_true(CSFX_PAGE_DATA <= NAND_PAGE_SIZE);
}

ZTEST(format, test_header_check)
{
	struct csfx_header h = valid();

	zassert_ok(csfx_header_check(&h, 100));

	h.frame_count = 101;
	zassert_equal(csfx_header_check(&h, 100), -EFBIG);

	h = valid();
	h.frame_count = 0;
	zassert_equal(csfx_header_check(&h, 100), -EINVAL);

	h = valid();
	h.magic[0] = 'X';
	zassert_equal(csfx_header_check(&h, 100), -EINVAL);

	h = valid();
	h.led_count = 131;
	zassert_equal(csfx_header_check(&h, 100), -EINVAL);

	h = valid();
	h.version = 2;
	zassert_equal(csfx_header_check(&h, 100), -EINVAL);
}

ZTEST(format, test_frame_location)
{
	uint32_t lb, page;
	uint16_t col;

	csfx_frame_location(0, &lb, &page, &col);
	zassert_true(lb == 1 && page == 0 && col == 0);
	csfx_frame_location(4, &lb, &page, &col);
	zassert_true(lb == 1 && page == 0 && col == 4 * 398);
	csfx_frame_location(5, &lb, &page, &col);
	zassert_true(lb == 1 && page == 1 && col == 0);
	csfx_frame_location(7, &lb, &page, &col);
	zassert_true(lb == 1 && page == 1 && col == 2 * 398);
	/* 64 pages * 5 frames per block. */
	csfx_frame_location(320, &lb, &page, &col);
	zassert_true(lb == 2 && page == 0 && col == 0);
}
