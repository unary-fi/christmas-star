/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Commands for the Android app:
 *   app hello   echo off, prints "ok proto=2 ... nonce=<hex> name=<name>"
 *               (pair/pairing.h; while locked, pairing.c answers it)
 *   app state   prints one "state key=value ..." line
 *   app effects the effects to offer, one "fx <id> <flags> <title>|<subtitle>"
 *               line each (flags: t = follows the tilt, 2 = two different
 *               faces, - = none)
 *   app preview <id> [frames] [step_ms]
 *               a short preview clip rendered on the star: a text line
 *               "preview <id> frames=N step=MS leds=66|132", then N binary
 *               frames in the stream format (transfer/stream_frame.h; 66 LEDs
 *               with the back copying the front, or all 132), then
 *               "preview end"
 *   app bye     echo back on (for humans)
 *   stream      binary live frames (see transfer/stream_frame.h) until an
 *               end frame or 2 s without data
 */

#include "cli_hooks.h"
#include "effects/effect.h"
#include "effects/shake_overlay.h"
#include "led/led_chain.h"
#include "pair/pairing.h"
#include "storage/effect_store.h"
#include "transfer/stream_frame.h"
#include "transfer/xmodem.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#define STREAM_IDLE_MS 2000

static int cmd_hello(const struct shell *sh, size_t argc, char **argv)
{
	shell_echo_set(sh, false);
	pairing_hello(sh);
	return 0;
}

static int cmd_bye(const struct shell *sh, size_t argc, char **argv)
{
	shell_echo_set(sh, true);
	return 0;
}

static int cmd_state(const struct shell *sh, size_t argc, char **argv)
{
	struct csfx_header hdr;
	const struct effect *fx = engine_get();
	bool stored = effect_store_info(&hdr) == 0;

	shell_print(sh,
		    "state effect=%s bright=%u shake=%d speed=%u fps=%u missing=%d cap=%u "
		    "flash=%.16s frames=%u owner=%s name=%s",
		    fx == &fx_player ? "flash" : fx->name, led_chain_get_brightness(),
		    shake_is_enabled() ? 1 : 0, engine_get_speed(), engine_fps(),
		    LED_COUNT - led_chain_present_count(), effect_store_capacity(),
		    stored ? hdr.name : "-", stored ? hdr.frame_count : 0,
		    pairing_owned() ? "paired" : "free", pairing_name());
	return 0;
}

static int cmd_effects(const struct shell *sh, size_t argc, char **argv)
{
	for (int i = 0; effects_builtin[i] != NULL; i++) {
		const struct effect *fx = effects_builtin[i];
		char flags[4] = "";

		if ((fx->flags & FX_HIDDEN) || fx->title == NULL) {
			continue;
		}
		if (fx->flags & FX_TILT) {
			strcat(flags, "t");
		}
		if (fx->flags & FX_TWO_FACES) {
			strcat(flags, "2");
		}
		shell_print(sh, "fx %s %s %s|%s", fx->name, flags[0] ? flags : "-", fx->title,
			    fx->subtitle != NULL ? fx->subtitle : "");
	}
	return 0;
}

/* ---- Preview clips ---------------------------------------------------------- */

#define PREVIEW_WARMUP_MS 2000
#define PREVIEW_MAX_FRAMES 80

static uint32_t pv_state[FX_STATE_MAX / 4];
static struct led_rgb pv_frame[LED_COUNT];
static uint8_t pv_buf[4 + 2 * LED_COUNT + 2];

static void raw_write(const struct shell *sh, const uint8_t *data, size_t len)
{
	while (len > 0) {
		size_t done = 0;

		(void)sh->iface->api->write(sh->iface, data, len, &done);
		data += done;
		len -= done;
		if (done == 0) {
			k_msleep(1);
		}
	}
}

static size_t encode_frame(const struct led_rgb *f, bool all)
{
	int n = all ? LED_COUNT : LED_FACE_COUNT;
	uint8_t *p = pv_buf;

	*p++ = STREAM_SYNC0;
	*p++ = STREAM_SYNC1;
	*p++ = all ? 0 : STREAM_FLAG_BACK_COPY;
	*p++ = (uint8_t)n;
	for (int i = 0; i < n; i++) {
		uint16_t v = (uint16_t)(((f[i].r >> 3) << 11) | ((f[i].g >> 2) << 5) | (f[i].b >> 3));

		*p++ = (uint8_t)v;
		*p++ = (uint8_t)(v >> 8);
	}
	uint16_t crc = xmodem_crc16(&pv_buf[2], (size_t)(p - &pv_buf[2]));

	*p++ = (uint8_t)(crc >> 8);
	*p++ = (uint8_t)crc;
	return (size_t)(p - pv_buf);
}

/* Upright, or for tilt effects a gentle sway of about 25 degrees each way. */
static void preview_imu(const struct effect *fx, uint32_t t, struct imu_state *imu)
{
	memset(imu, 0, sizeof(*imu));
	imu->ok = true;
	imu->gy = 1000;
	if (fx->flags & FX_TILT) {
		imu->gx = (int16_t)(((int)fx_tri((uint8_t)(t * 256U / 4000U)) - 128) * 4);
	}
}

static int cmd_preview(const struct shell *sh, size_t argc, char **argv)
{
	const struct effect *fx = effect_find(argv[1]);
	int frames = argc > 2 ? atoi(argv[2]) : 40;
	int step = argc > 3 ? atoi(argv[3]) : 100;
	struct imu_state imu;
	uint32_t t;

	if (fx == NULL || (fx->flags & FX_HIDDEN) || fx->realtime) {
		shell_error(sh, "no preview for %s", argv[1]);
		return -ENOENT;
	}
	frames = CLAMP(frames, 1, PREVIEW_MAX_FRAMES);
	step = CLAMP(step, 20, 1000);
	bool all = (fx->flags & FX_TWO_FACES) != 0;

	/* A private copy of the effect: what plays on the LEDs is not touched. */
	memset(pv_state, 0, sizeof(pv_state));
	memset(pv_frame, 0, sizeof(pv_frame));
	if (fx->start != NULL) {
		fx->start(pv_frame, pv_state);
	}
	for (t = 0; t < PREVIEW_WARMUP_MS; t += (uint32_t)step) {
		preview_imu(fx, t, &imu);
		fx->render(pv_frame, t, &imu, pv_state);
	}
	shell_print(sh, "preview %s frames=%d step=%d leds=%d", fx->name, frames, step,
		    all ? LED_COUNT : LED_FACE_COUNT);
	for (int k = 0; k < frames; k++, t += (uint32_t)step) {
		preview_imu(fx, t, &imu);
		fx->render(pv_frame, t, &imu, pv_state);
		raw_write(sh, pv_buf, encode_frame(pv_frame, all));
		/* About 2.7 KB/s: what the Bluetooth link carries without loss. */
		k_msleep(all ? 100 : 50);
	}
	shell_print(sh, "\npreview end");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_app,
	SHELL_CMD(hello, NULL, "Start an app session (echo off)", cmd_hello),
	SHELL_CMD(state, NULL, "One-line machine readable state", cmd_state),
	SHELL_CMD(effects, NULL, "Effects to offer: fx <id> <flags> <title>|<subtitle>", cmd_effects),
	SHELL_CMD_ARG(preview, NULL, "Preview clip: preview <id> [frames] [step_ms]", cmd_preview, 2,
		      2),
	SHELL_CMD(bye, NULL, "End an app session (echo on)", cmd_bye),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(app, &sub_app, "Android app helpers", NULL);

/* ---- Live stream ------------------------------------------------------------- */

static struct stream_rx srx;
static const struct shell *st_sh;
static K_MUTEX_DEFINE(st_mutex);
static bool st_active;
static void st_timeout(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(st_timer, st_timeout);

static void st_send(uint8_t c)
{
	size_t cnt;

	(void)st_sh->iface->api->write(st_sh->iface, &c, 1, &cnt);
}

static void st_finish(void)
{
	st_active = false;
	k_work_cancel_delayable(&st_timer);
	shell_set_bypass(st_sh, NULL, NULL);
}

static void st_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(user_data);

	k_mutex_lock(&st_mutex, K_FOREVER);
	for (size_t i = 0; i < len && st_active; i++) {
		switch (stream_rx_feed(&srx, data[i])) {
		case STREAM_FRAME:
			if (srx.count > 0) {
				engine_manual_rgb565(srx.payload, srx.count,
						     (srx.flags & STREAM_FLAG_BACK_COPY) != 0);
			}
			st_send(STREAM_ACK);
			if (srx.flags & STREAM_FLAG_END) {
				st_finish();
			}
			break;
		case STREAM_BAD:
			st_send(STREAM_NAK);
			break;
		default:
			break;
		}
	}
	if (st_active) {
		k_work_reschedule(&st_timer, K_MSEC(STREAM_IDLE_MS));
	}
	k_mutex_unlock(&st_mutex);
}

void app_stream_abort(void)
{
	k_mutex_lock(&st_mutex, K_FOREVER);
	if (st_active) {
		st_finish();
	}
	k_mutex_unlock(&st_mutex);
}

static void st_timeout(struct k_work *work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&st_mutex, K_FOREVER);
	if (st_active) {
		st_finish();
	}
	k_mutex_unlock(&st_mutex);
}

static int cmd_stream(const struct shell *sh, size_t argc, char **argv)
{
	k_mutex_lock(&st_mutex, K_FOREVER);
	stream_rx_init(&srx);
	st_sh = sh;
	st_active = true;
	shell_set_bypass(sh, st_bypass, NULL);
	k_work_reschedule(&st_timer, K_MSEC(STREAM_IDLE_MS));
	k_mutex_unlock(&st_mutex);
	/* Tell the app the binary mode is on. */
	st_send(STREAM_ACK);
	return 0;
}

SHELL_CMD_REGISTER(stream, NULL, "Binary live frames from the app (see stream_frame.h)",
		   cmd_stream);
