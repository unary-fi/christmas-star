/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "config/star_settings.h"
#include "effects/effect.h"
#include "bt/bm78.h"
#include "led/led_chain.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>

static void print_led_name(const struct shell *sh, int idx)
{
	const struct led_info *l = &led_layout[idx];

	shell_fprintf(sh, SHELL_NORMAL, "D%d%02d(%d) ", l->face + 2, l->designator, idx);
}

static int cmd_missing_list(const struct shell *sh, size_t argc, char **argv)
{
	int n = 0;

	for (int i = 0; i < LED_COUNT; i++) {
		if (led_chain_is_missing(i)) {
			print_led_name(sh, i);
			n++;
		}
	}
	shell_print(sh, "%s(%d missing, %d in chain)", n ? "" : "none ", n, LED_COUNT - n);
	return 0;
}

static int set_missing(const struct shell *sh, size_t argc, char **argv, bool missing)
{
	for (size_t i = 1; i < argc; i++) {
		int idx = led_layout_parse(argv[i]);

		if (idx < 0) {
			shell_error(sh, "unknown LED '%s' (use e.g. D259 or 0-131)", argv[i]);
			return -EINVAL;
		}
		led_chain_set_missing(idx, missing);
	}
	return star_settings_save_missing();
}

static int cmd_missing_add(const struct shell *sh, size_t argc, char **argv)
{
	return set_missing(sh, argc, argv, true);
}

static int cmd_missing_del(const struct shell *sh, size_t argc, char **argv)
{
	return set_missing(sh, argc, argv, false);
}

static int cmd_missing_clear(const struct shell *sh, size_t argc, char **argv)
{
	led_chain_clear_missing();
	return star_settings_save_missing();
}

static int cmd_test(const struct shell *sh, size_t argc, char **argv)
{
	int idx = -1;

	if (argc > 1 && strcmp(argv[1], "all") != 0) {
		idx = led_layout_parse(argv[1]);
		if (idx < 0) {
			shell_error(sh, "unknown LED '%s'", argv[1]);
			return -EINVAL;
		}
	}
	fx_test_set(idx);
	engine_set(&fx_test);
	if (idx >= 0) {
		print_led_name(sh, idx);
		shell_print(sh, "%s", led_chain_is_missing(idx) ? "(missing)" : "");
	}
	return 0;
}

static int cmd_brightness(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		int v = atoi(argv[1]);

		if (v < 0 || v > 255) {
			shell_error(sh, "brightness 0-255");
			return -EINVAL;
		}
		led_chain_set_brightness((uint8_t)v);
		star_settings_save_brightness();
	}
	shell_print(sh, "brightness %u", led_chain_get_brightness());
	return 0;
}

static int cmd_limit(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		int v = atoi(argv[1]);

		if (v < 0 || v > 10000) {
			shell_error(sh, "limit 0-10000 mA (0 = off)");
			return -EINVAL;
		}
		led_chain_set_limit_ma((uint16_t)v);
		star_settings_save_limit();
	}
	shell_print(sh, "limit %u mA, last frame ~%u mA", led_chain_get_limit_ma(),
		    led_chain_last_ma());
	return 0;
}

static int cmd_map(const struct shell *sh, size_t argc, char **argv)
{
	bool back = argc > 1 && atoi(argv[1]) == 2;

	shell_print(sh, "%s face, designator numbers (xx = missing), front view:",
		    back ? "Back (side 2)" : "Front (side 1)");
	for (int r = 0; r < LED_GRID_SIZE; r++) {
		char line[LED_GRID_SIZE * 3 + 1];
		char *p = line;

		for (int c = 0; c < LED_GRID_SIZE; c++) {
			int idx = led_grid[r][c];

			if (idx == LED_NONE) {
				*p++ = ' ';
				*p++ = ' ';
				*p++ = '.';
				continue;
			}
			if (back) {
				idx = led_layout[idx].partner;
			}
			if (led_chain_is_missing(idx)) {
				*p++ = ' ';
				*p++ = 'x';
				*p++ = 'x';
			} else {
				int d = led_layout[idx].designator;

				*p++ = ' ';
				*p++ = d >= 10 ? (char)('0' + d / 10) : ' ';
				*p++ = (char)('0' + d % 10);
			}
		}
		*p = '\0';
		shell_print(sh, "%s", line);
	}
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t errors;
	int err = led_chain_last_error(&errors);

	shell_print(sh, "effect %s, %u fps, %d LEDs in chain, brightness %u, ~%u mA (limit %u)",
		    engine_get()->name, engine_fps(), led_chain_present_count(),
		    led_chain_get_brightness(), led_chain_last_ma(), led_chain_get_limit_ma());
	struct led_supply_status s;

	led_chain_supply_status(&s);
	if (s.vdd_mv != 0) {
		shell_print(sh, "3V3 %u mV (lowest %u), supply limit %u mA, sags %u (deep %u), "
			    "BT resets %u", s.vdd_mv, s.vdd_min_mv, s.limit_ma, s.sags, s.deep_sags,
			    bm78_glitch_resets());
	}
	if (errors != 0) {
		shell_warn(sh, "%u LED update errors, last %d", errors, err);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_missing,
	SHELL_CMD(list, NULL, "List missing LEDs", cmd_missing_list),
	SHELL_CMD_ARG(add, NULL, "Mark LEDs missing: add <D259|idx>...", cmd_missing_add, 2, 10),
	SHELL_CMD_ARG(del, NULL, "Unmark LEDs: del <D259|idx>...", cmd_missing_del, 2, 10),
	SHELL_CMD(clear, NULL, "No missing LEDs", cmd_missing_clear),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_led,
	SHELL_CMD(missing, &sub_missing, "LEDs whose pads are bridged on the board", NULL),
	SHELL_CMD_ARG(test, NULL, "Light one LED: test <D232|idx>, or test all", cmd_test, 1, 1),
	SHELL_CMD_ARG(brightness, NULL, "Global brightness 0-255", cmd_brightness, 1, 1),
	SHELL_CMD_ARG(limit, NULL, "Total current limit in mA", cmd_limit, 1, 1),
	SHELL_CMD_ARG(map, NULL, "Print the LED bitmap: map [1|2]", cmd_map, 1, 1),
	SHELL_CMD(status, NULL, "Show LED status", cmd_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(led, &sub_led, "LED chain", NULL);
