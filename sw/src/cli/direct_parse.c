/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "direct_parse.h"
#include "layout/led_layout.h"

#include <stdlib.h>

static int parse_int(const char *s, int min, int max)
{
	char *end;
	long v;

	if (s == NULL || *s == '\0') {
		return -1;
	}
	v = strtol(s, &end, 10);
	if (*end != '\0' || v < min || v > max) {
		return -1;
	}
	return (int)v;
}

int direct_parse_side(const char *s)
{
	return parse_int(s, 0, 2);
}

int direct_parse_index(const char *s)
{
	return parse_int(s, 1, LED_FACE_COUNT);
}

void direct_side_range(int side, int *first, int *last)
{
	switch (side) {
	case 1:
		*first = 0;
		*last = LED_FACE_COUNT - 1;
		break;
	case 2:
		*first = LED_FACE_COUNT;
		*last = LED_COUNT - 1;
		break;
	default:
		*first = 0;
		*last = LED_COUNT - 1;
		break;
	}
}
