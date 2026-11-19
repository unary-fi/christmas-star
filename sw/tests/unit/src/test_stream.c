/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "transfer/stream_frame.h"
#include "transfer/xmodem.h"

static struct stream_rx rx;
static uint8_t buf[4 + 2 * LED_COUNT + 2];

/* Build a frame the way the Android app does. */
static size_t make_frame(uint8_t flags, uint8_t n, uint8_t fill)
{
	size_t len = 0;

	buf[len++] = STREAM_SYNC0;
	buf[len++] = STREAM_SYNC1;
	buf[len++] = flags;
	buf[len++] = n;
	for (int i = 0; i < 2 * n; i++) {
		buf[len++] = (uint8_t)(fill + i);
	}
	uint16_t crc = xmodem_crc16(&buf[2], len - 2);

	buf[len++] = (uint8_t)(crc >> 8);
	buf[len++] = (uint8_t)crc;
	return len;
}

static enum stream_event feed_all(size_t len)
{
	enum stream_event ev = STREAM_NONE;

	for (size_t i = 0; i < len; i++) {
		enum stream_event e = stream_rx_feed(&rx, buf[i]);

		if (e != STREAM_NONE) {
			ev = e;
		}
	}
	return ev;
}

static void reset(void *f)
{
	ARG_UNUSED(f);
	stream_rx_init(&rx);
}

ZTEST_SUITE(stream, NULL, NULL, reset, NULL, NULL);

ZTEST(stream, test_front_copy_frame)
{
	size_t len = make_frame(STREAM_FLAG_BACK_COPY, LED_FACE_COUNT, 7);

	zassert_equal(feed_all(len), STREAM_FRAME);
	zassert_equal(rx.count, LED_FACE_COUNT);
	zassert_equal(rx.flags, STREAM_FLAG_BACK_COPY);
	zassert_equal(rx.payload[0], 7);
	zassert_equal(rx.payload[2 * LED_FACE_COUNT - 1], (uint8_t)(7 + 2 * LED_FACE_COUNT - 1));
}

ZTEST(stream, test_full_frame_and_resync_after_garbage)
{
	size_t len = make_frame(0, LED_COUNT, 1);
	uint8_t junk[] = {'s', 't', 'a', 'r', ':', 0xA5, 0x00};

	for (size_t i = 0; i < sizeof(junk); i++) {
		zassert_equal(stream_rx_feed(&rx, junk[i]), STREAM_NONE);
	}
	zassert_equal(feed_all(len), STREAM_FRAME);
	zassert_equal(rx.count, LED_COUNT);
}

ZTEST(stream, test_bad_crc_and_oversize)
{
	size_t len = make_frame(0, 10, 0);

	buf[6] ^= 0x40;
	zassert_equal(feed_all(len), STREAM_BAD);

	/* 67 LEDs in back-copy mode is invalid. */
	stream_rx_init(&rx);
	uint8_t hdr[] = {STREAM_SYNC0, STREAM_SYNC1, STREAM_FLAG_BACK_COPY, LED_FACE_COUNT + 1};
	enum stream_event ev = STREAM_NONE;

	for (size_t i = 0; i < sizeof(hdr); i++) {
		ev = stream_rx_feed(&rx, hdr[i]);
	}
	zassert_equal(ev, STREAM_BAD);
}

ZTEST(stream, test_end_frame_without_payload)
{
	size_t len = make_frame(STREAM_FLAG_END, 0, 0);

	zassert_equal(feed_all(len), STREAM_FRAME);
	zassert_equal(rx.count, 0);
	zassert_true(rx.flags & STREAM_FLAG_END);
}
