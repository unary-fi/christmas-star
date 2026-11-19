/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "cli_hooks.h"
#include "config/star_settings.h"
#include "effects/effect.h"
#include "effects/shake_overlay.h"
#include "led/color.h"
#include "storage/effect_store.h"
#include "transfer/xmodem.h"

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

int effect_start_spec(const char *spec)
{
	const struct effect *fx = effect_find(spec);

	if (fx == NULL) {
		return -ENOENT;
	}
	engine_set(fx);
	return 0;
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	struct csfx_header hdr;

	for (int i = 0; effects_builtin[i] != NULL; i++) {
		shell_print(sh, "  %-10s %s", effects_builtin[i]->name, effects_builtin[i]->help);
	}
	if (effect_store_info(&hdr) == 0) {
		shell_print(sh, "  %-10s '%.16s', %u frames%s", "flash", hdr.name, hdr.frame_count,
			    (hdr.flags & CSFX_FLAG_LOOP) ? ", loop" : "");
	}
	return 0;
}

static int cmd_play(const struct shell *sh, size_t argc, char **argv)
{
	struct csfx_header hdr;

	if (strcmp(argv[1], "flash") == 0 && effect_store_info(&hdr) != 0) {
		shell_error(sh, "no effect stored in the flash");
		return -ENOENT;
	}
	if (effect_start_spec(argv[1]) != 0) {
		shell_error(sh, "unknown effect '%s' (see effect list)", argv[1]);
		return -ENOENT;
	}
	return 0;
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	engine_set(&fx_off);
	return 0;
}

static int cmd_color(const struct shell *sh, size_t argc, char **argv)
{
	struct led_rgb c;

	if (color_parse(argv[1], &c) != 0) {
		shell_error(sh, "colour as 0xRRGGBB");
		return -EINVAL;
	}
	fx_solid_set(c);
	engine_set(&fx_solid);
	return 0;
}

static int cmd_speed(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		int v = atoi(argv[1]);

		if (v < 25 || v > 400) {
			shell_error(sh, "speed 25-400 %%");
			return -EINVAL;
		}
		engine_set_speed((uint16_t)v);
	}
	shell_print(sh, "speed %u %%", engine_get_speed());
	return 0;
}

static int cmd_info(const struct shell *sh, size_t argc, char **argv)
{
	struct csfx_header hdr;

	if (effect_store_info(&hdr) != 0) {
		shell_print(sh, "flash: empty (capacity %u frames)", effect_store_capacity());
		return 0;
	}
	shell_print(sh, "flash: '%.16s', %u frames, %s, crc %08x, capacity %u frames", hdr.name,
		    hdr.frame_count, (hdr.flags & CSFX_FLAG_LOOP) ? "loop" : "once", hdr.crc32,
		    effect_store_capacity());
	return 0;
}

static int cmd_delete(const struct shell *sh, size_t argc, char **argv)
{
	if (engine_get() == &fx_player) {
		engine_set(&fx_golden);
	}
	return effect_store_delete();
}

static int cmd_shake(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "on") == 0) {
			shake_set_enabled(true);
		} else if (strcmp(argv[1], "off") == 0) {
			shake_set_enabled(false);
		} else {
			shell_error(sh, "shake on|off");
			return -EINVAL;
		}
		star_settings_state_changed();
	}
	shell_print(sh, "shake %s", shake_is_enabled() ? "on" : "off");
	return 0;
}

/* ---- XMODEM upload ---------------------------------------------------------- */

static struct xmodem_rx xm;
static const struct shell *up_sh;
static K_MUTEX_DEFINE(up_mutex);
static void up_timeout(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(up_timer, up_timeout);

static void up_send(void *ctx, uint8_t c)
{
	size_t cnt;

	ARG_UNUSED(ctx);
	(void)up_sh->iface->api->write(up_sh->iface, &c, 1, &cnt);
}

static int up_sink(void *ctx, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);
	return effect_store_write(data, len);
}

static void up_finish(enum xmodem_result r)
{
	int ret;

	k_work_cancel_delayable(&up_timer);
	shell_set_bypass(up_sh, NULL, NULL);
	ret = effect_store_write_end(r == XMODEM_DONE);

	/* Give the sender time to leave its transfer mode before printing. */
	k_msleep(200);
	if (r == XMODEM_DONE && ret == 0) {
		shell_print(up_sh, "\nupload OK");
	} else if (r == XMODEM_DONE) {
		shell_error(up_sh, "\nupload failed: %s (%d)",
			    ret == -EBADMSG ? "CRC mismatch" :
			    ret == -EMSGSIZE ? "file truncated" : "write error", ret);
	} else if (xm.sink_error != 0) {
		shell_error(up_sh, "\nupload failed: %s (%d)",
			    xm.sink_error == -EFBIG ? "effect too long for the flash" :
			    xm.sink_error == -EINVAL ? "not a .csfx effect file" : "write error",
			    xm.sink_error);
	} else {
		shell_error(up_sh, "\nupload %s", r == XMODEM_CANCELLED ? "cancelled" : "timed out");
	}
}

static void up_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);

	k_mutex_lock(&up_mutex, K_FOREVER);
	if (effect_store_busy()) {
		enum xmodem_result r = xmodem_rx_feed(&xm, data, len);

		if (r != XMODEM_RUNNING) {
			up_finish(r);
		} else {
			k_work_reschedule(&up_timer, K_MSEC(XMODEM_TICK_MS));
		}
	}
	k_mutex_unlock(&up_mutex);
}

static void up_timeout(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&up_mutex, K_FOREVER);
	if (effect_store_busy()) {
		enum xmodem_result r = xmodem_rx_tick(&xm);

		if (r != XMODEM_RUNNING) {
			up_finish(r);
		} else {
			k_work_reschedule(&up_timer, K_MSEC(XMODEM_TICK_MS));
		}
	}
	k_mutex_unlock(&up_mutex);
}

void effect_upload_abort(void)
{
	k_mutex_lock(&up_mutex, K_FOREVER);
	if (effect_store_busy() && up_sh != NULL) {
		up_finish(XMODEM_CANCELLED);
	}
	k_mutex_unlock(&up_mutex);
}

static int cmd_upload(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	/* The stored effect is about to be overwritten; leave the player. */
	if (engine_get() == &fx_player) {
		engine_set(&fx_golden);
		k_msleep(50);
	}
	ret = effect_store_write_begin();
	if (ret != 0) {
		shell_error(sh, "cannot write the flash (%d)", ret);
		return ret;
	}
	shell_print(sh, "Send the .csfx file with XMODEM-1K (CRC) now...");

	k_mutex_lock(&up_mutex, K_FOREVER);
	up_sh = sh;
	shell_set_bypass(sh, up_bypass, NULL);
	xmodem_rx_start(&xm, up_send, up_sink, NULL);
	k_work_reschedule(&up_timer, K_MSEC(XMODEM_TICK_MS));
	k_mutex_unlock(&up_mutex);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_effect,
	SHELL_CMD(list, NULL, "List effects (and the one stored in the flash)", cmd_list),
	SHELL_CMD_ARG(play, NULL, "Play an effect: play <name|flash>", cmd_play, 2, 0),
	SHELL_CMD(stop, NULL, "Turn the LEDs off", cmd_stop),
	SHELL_CMD_ARG(color, NULL, "Solid colour: color <0xRRGGBB>", cmd_color, 2, 0),
	SHELL_CMD_ARG(speed, NULL, "Effect speed: speed [25-400 %]", cmd_speed, 1, 1),
	SHELL_CMD(info, NULL, "Stored effect details", cmd_info),
	SHELL_CMD(delete, NULL, "Delete the stored effect", cmd_delete),
	SHELL_CMD(upload, NULL, "Receive a .csfx file via XMODEM-1K into the flash", cmd_upload),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(effect, &sub_effect, "LED effects", NULL);
SHELL_CMD_ARG_REGISTER(shake, NULL, "Shake to drop lights: shake [on|off]", cmd_shake, 1, 1);
