/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/ztest.h>

#include "layout/led_layout.h"

ZTEST_SUITE(layout, NULL, NULL, NULL, NULL, NULL);

ZTEST(layout, test_faces_and_designators)
{
	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];

		zassert_equal(l->face, i < LED_FACE_COUNT ? LED_FACE_FRONT : LED_FACE_BACK, "LED %d", i);
		zassert_equal(l->designator, (i % LED_FACE_COUNT) + 1, "LED %d", i);
		zassert_true(l->r <= LED_STAR_RADIUS_MM, "LED %d radius %d", i, l->r);
	}
}

ZTEST(layout, test_partners_are_mirrored)
{
	for (int i = 0; i < LED_COUNT; i++) {
		const struct led_info *l = &led_layout[i];
		const struct led_info *p = &led_layout[l->partner];

		zassert_not_equal(l->face, p->face, "LED %d", i);
		zassert_equal(p->partner, i, "LED %d", i);
		zassert_equal(l->x, p->x, "LED %d", i);
		zassert_equal(l->y, p->y, "LED %d", i);
		zassert_equal(l->col, p->col, "LED %d", i);
		zassert_equal(l->row, p->row, "LED %d", i);
	}
}

ZTEST(layout, test_groups)
{
	int outline = 0, inner = 0;

	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].group == LED_GROUP_OUTLINE) {
			outline++;
			zassert_true(led_layout[i].step < 48);
		} else {
			inner++;
			zassert_true(led_layout[i].step < 18);
		}
		zassert_true(led_layout[i].point < 6);
	}
	zassert_equal(outline, 48);
	zassert_equal(inner, 18);
}

ZTEST(layout, test_grid_has_no_collisions)
{
	int cells = 0;

	for (int r = 0; r < LED_GRID_SIZE; r++) {
		for (int c = 0; c < LED_GRID_SIZE; c++) {
			uint8_t idx = led_grid[r][c];

			if (idx == LED_NONE) {
				continue;
			}
			cells++;
			zassert_true(idx < LED_FACE_COUNT);
			zassert_equal(led_layout[idx].col, c);
			zassert_equal(led_layout[idx].row, r);
		}
	}
	zassert_equal(cells, LED_FACE_COUNT, "every front LED has its own cell");
}

ZTEST(layout, test_index_and_parse)
{
	zassert_equal(led_layout_index(1, 1), 0);
	zassert_equal(led_layout_index(1, 32), 31);
	zassert_equal(led_layout_index(2, 5), 70);
	zassert_equal(led_layout_index(2, 66), 131);
	zassert_equal(led_layout_index(0, 1), -1);
	zassert_equal(led_layout_index(3, 1), -1);
	zassert_equal(led_layout_index(1, 0), -1);
	zassert_equal(led_layout_index(1, 67), -1);

	zassert_equal(led_layout_parse("D259"), 58);
	zassert_equal(led_layout_parse("d301"), 66);
	zassert_equal(led_layout_parse("D366"), 131);
	zassert_equal(led_layout_parse("131"), 131);
	zassert_equal(led_layout_parse("0"), 0);
	zassert_equal(led_layout_parse("D267"), -1);
	zassert_equal(led_layout_parse("D159"), -1);
	zassert_equal(led_layout_parse("132"), -1);
	zassert_equal(led_layout_parse("D25x"), -1);
	zassert_equal(led_layout_parse(""), -1);
}

ZTEST(layout, test_nearest)
{
	const struct led_info *tip = NULL;

	/* The top tip is at (0, 110). */
	for (int i = 0; i < LED_FACE_COUNT; i++) {
		if (led_layout[i].y == 110) {
			tip = &led_layout[i];
		}
	}
	zassert_not_null(tip);
	zassert_equal(led_layout_nearest(0, 108, LED_FACE_FRONT, 10), tip - led_layout);
	zassert_equal(led_layout_nearest(0, 108, LED_FACE_BACK, 10), tip->partner);
	zassert_equal(led_layout_nearest(0, 200, LED_FACE_FRONT, 10), -1);
}
