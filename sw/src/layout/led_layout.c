/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "led_layout.h"

#include <stdlib.h>

int led_layout_nearest(int x_mm, int y_mm, enum led_face face, int max_dist_mm)
{
	int best = -1;
	int best_d2 = max_dist_mm * max_dist_mm + 1;
	int base = (face == LED_FACE_BACK) ? LED_FACE_COUNT : 0;

	for (int i = base; i < base + LED_FACE_COUNT; i++) {
		int dx = x_mm - led_layout[i].x;
		int dy = y_mm - led_layout[i].y;
		int d2 = dx * dx + dy * dy;

		if (d2 < best_d2) {
			best_d2 = d2;
			best = i;
		}
	}
	return best;
}

int led_layout_parse(const char *name)
{
	char *end;
	long v;

	if (name == NULL || *name == '\0') {
		return -1;
	}
	if (name[0] == 'D' || name[0] == 'd') {
		v = strtol(name + 1, &end, 10);
		if (*end != '\0') {
			return -1;
		}
		return led_layout_index((int)(v / 100) - 1, (int)(v % 100));
	}
	v = strtol(name, &end, 10);
	if (*end != '\0' || v < 0 || v >= LED_COUNT) {
		return -1;
	}
	return (int)v;
}
