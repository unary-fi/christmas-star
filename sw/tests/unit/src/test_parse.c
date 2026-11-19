/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include "cli/direct_parse.h"
#include "led/color.h"
#include "led/led_chain.h"

ZTEST_SUITE(parse, NULL, NULL, NULL, NULL, NULL);

ZTEST(parse, test_color_formats)
{
	struct led_rgb c;

	zassert_ok(color_parse("0xffee33", &c));
	zassert_true(c.r == 0xff && c.g == 0xee && c.b == 0x33);
	zassert_ok(color_parse("#0000FF", &c));
	zassert_true(c.r == 0 && c.g == 0 && c.b == 0xff);
	zassert_ok(color_parse("102030", &c));
	zassert_true(c.r == 0x10 && c.g == 0x20 && c.b == 0x30);
	zassert_ok(color_parse("0X0a0B0c", &c));
	zassert_true(c.r == 0x0a && c.g == 0x0b && c.b == 0x0c);

	zassert_equal(color_parse("0xffee3", &c), -EINVAL);
	zassert_equal(color_parse("0xffee333", &c), -EINVAL);
	zassert_equal(color_parse("zz0000", &c), -EINVAL);
	zassert_equal(color_parse("", &c), -EINVAL);
	zassert_equal(color_parse("#", &c), -EINVAL);
	zassert_equal(color_parse(NULL, &c), -EINVAL);
}

ZTEST(parse, test_side_and_index)
{
	int first, last;

	zassert_equal(direct_parse_side("0"), 0);
	zassert_equal(direct_parse_side("1"), 1);
	zassert_equal(direct_parse_side("2"), 2);
	zassert_equal(direct_parse_side("3"), -1);
	zassert_equal(direct_parse_side("-1"), -1);
	zassert_equal(direct_parse_side("a"), -1);
	zassert_equal(direct_parse_side(""), -1);

	zassert_equal(direct_parse_index("1"), 1);
	zassert_equal(direct_parse_index("32"), 32);
	zassert_equal(direct_parse_index("66"), 66);
	zassert_equal(direct_parse_index("0"), -1);
	zassert_equal(direct_parse_index("67"), -1);
	zassert_equal(direct_parse_index("3x"), -1);

	direct_side_range(1, &first, &last);
	zassert_true(first == 0 && last == 65);
	direct_side_range(2, &first, &last);
	zassert_true(first == 66 && last == 131);
	direct_side_range(0, &first, &last);
	zassert_true(first == 0 && last == 131);
}

ZTEST(parse, test_current_limit_scale)
{
	/* 132 LEDs full white, 12 mA per channel = 4752 mA. */
	uint32_t full = 132U * 3U * 255U;

	zassert_equal(led_chain_limit_scale(full, 12, 0), 256, "limit off");
	zassert_equal(led_chain_limit_scale(full, 12, 5000), 256, "under limit");
	zassert_equal(led_chain_limit_scale(full, 12, 2000), 2000U * 256U / 4752U);
	zassert_equal(led_chain_limit_scale(0, 12, 2000), 256);
}

ZTEST(parse, test_hsv)
{
	struct led_rgb c = color_hsv(0, 255, 255);

	zassert_true(c.r == 255 && c.b == 0);
	c = color_hsv(0, 0, 200);
	zassert_true(c.r == c.g && c.g == c.b);
}
