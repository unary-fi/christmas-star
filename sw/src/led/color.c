/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "color.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

const uint8_t color_gamma[256] = {
	0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
	1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
	3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
	6,   7,   7,   7,   8,   8,   8,   9,   9,   9,   10,  10,  11,  11,  11,  12,
	12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
	20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
	30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
	42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
	56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
	73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
	91,  93,  94,  95,  97,  98,  99,  100, 102, 103, 105, 106, 107, 109, 110, 111,
	113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
	137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
	163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
	192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
	223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
};

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

int color_parse(const char *s, struct led_rgb *out)
{
	uint32_t v = 0;

	if (s == NULL) {
		return -EINVAL;
	}
	if (s[0] == '#') {
		s++;
	} else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}
	if (strlen(s) != 6) {
		return -EINVAL;
	}
	for (int i = 0; i < 6; i++) {
		int d = hex_digit(s[i]);

		if (d < 0) {
			return -EINVAL;
		}
		v = (v << 4) | (uint32_t)d;
	}
	out->r = (uint8_t)(v >> 16);
	out->g = (uint8_t)(v >> 8);
	out->b = (uint8_t)v;
	return 0;
}

struct led_rgb color_hsv(uint8_t h, uint8_t s, uint8_t v)
{
	uint8_t region = h / 43;
	uint8_t rem = (uint8_t)((h - region * 43) * 6);
	uint8_t p = (uint8_t)(v * (255 - s) / 255);
	uint8_t q = (uint8_t)(v * (255 - s * rem / 255) / 255);
	uint8_t t = (uint8_t)(v * (255 - s * (255 - rem) / 255) / 255);

	switch (region) {
	case 0:
		return (struct led_rgb){.r = v, .g = t, .b = p};
	case 1:
		return (struct led_rgb){.r = q, .g = v, .b = p};
	case 2:
		return (struct led_rgb){.r = p, .g = v, .b = t};
	case 3:
		return (struct led_rgb){.r = p, .g = q, .b = v};
	case 4:
		return (struct led_rgb){.r = t, .g = p, .b = v};
	default:
		return (struct led_rgb){.r = v, .g = p, .b = q};
	}
}
