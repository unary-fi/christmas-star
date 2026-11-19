/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include "led/led_chain.h"

ZTEST(supply, test_no_sag_keeps_limit)
{
	struct supply_limiter s;

	supply_limiter_init(&s, 600);
	for (int i = 0; i < 5000; i++) {
		zassert_false(supply_limiter_update(&s, 3320, 550, 600));
	}
	zassert_equal(s.limit_ma, 600);
	zassert_equal(s.sags, 0);
}

ZTEST(supply, test_sag_cuts_below_drawn_current)
{
	struct supply_limiter s;

	supply_limiter_init(&s, 2000);
	/* Mild sag while drawing 800 mA: limit 7/8 of 800. */
	zassert_false(supply_limiter_update(&s, 3150, 800, 2000));
	zassert_equal(s.limit_ma, 700);
	/* Deep sag: half of what is left. */
	zassert_true(supply_limiter_update(&s, 2700, 700, 2000));
	zassert_equal(s.limit_ma, 350);
	zassert_equal(s.sags, 2);
	zassert_equal(s.deep_sags, 1);
}

ZTEST(supply, test_floor_and_configured_max)
{
	struct supply_limiter s;

	supply_limiter_init(&s, 600);
	for (int i = 0; i < 20; i++) {
		(void)supply_limiter_update(&s, 2500, 0, 600);
	}
	zassert_equal(s.limit_ma, SUPPLY_LIMIT_FLOOR_MA);
	/* Lowering the configured limit applies at once. */
	supply_limiter_init(&s, 600);
	(void)supply_limiter_update(&s, 3320, 0, 300);
	zassert_equal(s.limit_ma, 300);
	/* 0 = off: ceiling only. */
	supply_limiter_init(&s, 0);
	(void)supply_limiter_update(&s, 3320, 0, 0);
	zassert_equal(s.limit_ma, SUPPLY_LIMIT_CEILING_MA);
}

ZTEST(supply, test_slow_recovery)
{
	struct supply_limiter s;

	supply_limiter_init(&s, 600);
	(void)supply_limiter_update(&s, 3100, 600, 600);
	zassert_equal(s.limit_ma, 525);
	/* Between the thresholds the calm period does not advance. */
	for (int i = 0; i < 2 * SUPPLY_CALM_FRAMES; i++) {
		(void)supply_limiter_update(&s, 3230, 500, 600);
	}
	zassert_equal(s.calm_frames, 0);
	/* Nothing during the calm period. */
	for (int i = 0; i < SUPPLY_CALM_FRAMES; i++) {
		(void)supply_limiter_update(&s, 3320, 500, 600);
	}
	zassert_equal(s.limit_ma, 525);
	/* Then 1 mA per ~11 frames, never above the configured limit. */
	for (int i = 0; i < 110; i++) {
		(void)supply_limiter_update(&s, 3320, 500, 600);
	}
	zassert_true(s.limit_ma >= 530 && s.limit_ma <= 536, "limit %u", s.limit_ma);
	for (int i = 0; i < 5000; i++) {
		(void)supply_limiter_update(&s, 3320, 500, 600);
	}
	zassert_equal(s.limit_ma, 600);
}

ZTEST_SUITE(supply, NULL, NULL, NULL, NULL, NULL);
