/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Direct LED control:
 *   set_led <side> <index> <color>   e.g. set_led 1 32 0xffee33  (D232)
 *   set_all <side> <color>           e.g. set_all 2 #0000ff      (whole back)
 * side: 0 = both, 1 = front (D2xx), 2 = back (D3xx); index: 1..66;
 * color: 0xRRGGBB, #RRGGBB or RRGGBB.
 */

#include "direct_parse.h"
#include "effects/effect.h"
#include "led/color.h"
#include "led/led_chain.h"

#include <zephyr/shell/shell.h>

static int cmd_set_led(const struct shell *sh, size_t argc, char **argv)
{
	int side = direct_parse_side(argv[1]);
	int index = direct_parse_index(argv[2]);
	struct led_rgb c;

	if (side < 0 || index < 0 || color_parse(argv[3], &c) != 0) {
		shell_error(sh, "usage: set_led <side 0-2> <index 1-66> <0xRRGGBB>");
		return -EINVAL;
	}
	for (int s = 1; s <= 2; s++) {
		if (side != 0 && side != s) {
			continue;
		}
		int idx = led_layout_index(s, index);

		if (led_chain_is_missing(idx)) {
			shell_warn(sh, "D%d%02d is missing", s + 1, index);
		}
		engine_manual_set(idx, idx, c);
	}
	return 0;
}

static int cmd_set_all(const struct shell *sh, size_t argc, char **argv)
{
	int side = direct_parse_side(argv[1]);
	int first, last;
	struct led_rgb c;

	if (side < 0 || color_parse(argv[2], &c) != 0) {
		shell_error(sh, "usage: set_all <side 0-2> <0xRRGGBB>");
		return -EINVAL;
	}
	direct_side_range(side, &first, &last);
	engine_manual_set(first, last, c);
	return 0;
}

SHELL_CMD_ARG_REGISTER(set_led, NULL,
		       "Set one LED: set_led <side 0=both 1=front 2=back> <index 1-66> <0xRRGGBB>",
		       cmd_set_led, 4, 0);
SHELL_CMD_ARG_REGISTER(set_all, NULL,
		       "Set all LEDs of a side: set_all <side 0=both 1=front 2=back> <0xRRGGBB>",
		       cmd_set_all, 3, 0);
