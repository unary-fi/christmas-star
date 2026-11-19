/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "transfer/xmodem.h"

static uint8_t sent[64];
static size_t sent_len;
static uint8_t sink_buf[2048];
static size_t sink_len;
static int sink_ret;
static struct xmodem_rx x;
static uint8_t pkt[1029];

static void send(void *ctx, uint8_t c)
{
	ARG_UNUSED(ctx);
	if (sent_len < sizeof(sent)) {
		sent[sent_len++] = c;
	}
}

static int sink(void *ctx, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);
	if (sink_ret != 0) {
		return sink_ret;
	}
	zassert_true(sink_len + len <= sizeof(sink_buf));
	memcpy(&sink_buf[sink_len], data, len);
	sink_len += len;
	return 0;
}

static size_t make_packet(uint8_t blk, size_t size, uint8_t fill)
{
	pkt[0] = size == 128 ? XMODEM_SOH : XMODEM_STX;
	pkt[1] = blk;
	pkt[2] = (uint8_t)~blk;
	for (size_t i = 0; i < size; i++) {
		pkt[3 + i] = (uint8_t)(fill + i);
	}
	uint16_t crc = xmodem_crc16(&pkt[3], size);

	pkt[3 + size] = (uint8_t)(crc >> 8);
	pkt[4 + size] = (uint8_t)crc;
	return size + 5;
}

static uint8_t last_sent(void)
{
	return sent_len ? sent[sent_len - 1] : 0;
}

static void reset(void *f)
{
	ARG_UNUSED(f);
	sent_len = 0;
	sink_len = 0;
	sink_ret = 0;
	xmodem_rx_start(&x, send, sink, NULL);
}

ZTEST_SUITE(xmodem, NULL, NULL, reset, NULL, NULL);

ZTEST(xmodem, test_crc16_reference)
{
	/* CRC-16/XMODEM check value for "123456789". */
	zassert_equal(xmodem_crc16((const uint8_t *)"123456789", 9), 0x31C3);
}

ZTEST(xmodem, test_requests_crc_mode)
{
	zassert_equal(sent_len, 1);
	zassert_equal(sent[0], XMODEM_CRC_REQ);
}

ZTEST(xmodem, test_two_blocks_and_eot)
{
	size_t n = make_packet(1, 128, 0x10);

	zassert_equal(xmodem_rx_feed(&x, pkt, n), XMODEM_RUNNING);
	zassert_equal(last_sent(), XMODEM_ACK);

	n = make_packet(2, 1024, 0x40);
	/* Deliver in odd-sized pieces. */
	for (size_t i = 0; i < n; i += 37) {
		xmodem_rx_feed(&x, &pkt[i], MIN(37, n - i));
	}
	zassert_equal(last_sent(), XMODEM_ACK);
	zassert_equal(sink_len, 128 + 1024);
	zassert_equal(sink_buf[0], 0x10);
	zassert_equal(sink_buf[128], 0x40);

	uint8_t eot = XMODEM_EOT;

	zassert_equal(xmodem_rx_feed(&x, &eot, 1), XMODEM_DONE);
	zassert_equal(last_sent(), XMODEM_ACK);
}

ZTEST(xmodem, test_bad_crc_is_nakked_then_accepted)
{
	size_t n = make_packet(1, 128, 0);

	pkt[10] ^= 0xFF;
	xmodem_rx_feed(&x, pkt, n);
	/* Before the first good block, the receiver keeps asking with 'C'. */
	zassert_equal(last_sent(), XMODEM_CRC_REQ);
	zassert_equal(sink_len, 0);

	n = make_packet(1, 128, 0);
	xmodem_rx_feed(&x, pkt, n);
	zassert_equal(last_sent(), XMODEM_ACK);

	n = make_packet(2, 128, 0);
	pkt[n - 1] ^= 1;
	xmodem_rx_feed(&x, pkt, n);
	zassert_equal(last_sent(), XMODEM_NAK);
	zassert_equal(sink_len, 128);
}

ZTEST(xmodem, test_duplicate_block_not_written_twice)
{
	size_t n = make_packet(1, 128, 0);

	xmodem_rx_feed(&x, pkt, n);
	xmodem_rx_feed(&x, pkt, n);
	zassert_equal(last_sent(), XMODEM_ACK);
	zassert_equal(sink_len, 128);
}

ZTEST(xmodem, test_out_of_sequence_fails)
{
	size_t n = make_packet(3, 128, 0);

	zassert_equal(xmodem_rx_feed(&x, pkt, n), XMODEM_FAILED);
	zassert_equal(last_sent(), XMODEM_CAN);
}

ZTEST(xmodem, test_sink_error_cancels)
{
	size_t n = make_packet(1, 128, 0);

	sink_ret = -EFBIG;
	zassert_equal(xmodem_rx_feed(&x, pkt, n), XMODEM_FAILED);
	zassert_equal(x.sink_error, -EFBIG);
	zassert_equal(last_sent(), XMODEM_CAN);
}

ZTEST(xmodem, test_sender_cancel)
{
	uint8_t can[2] = {XMODEM_CAN, XMODEM_CAN};

	zassert_equal(xmodem_rx_feed(&x, can, 2), XMODEM_CANCELLED);
}

ZTEST(xmodem, test_start_timeout)
{
	enum xmodem_result r = XMODEM_RUNNING;
	int ticks = 0;

	while (r == XMODEM_RUNNING && ticks < 100) {
		r = xmodem_rx_tick(&x);
		ticks++;
	}
	zassert_equal(r, XMODEM_FAILED);
	zassert_true(ticks >= XMODEM_START_TRIES - 1);
}

ZTEST(xmodem, test_eot_before_data_fails)
{
	uint8_t eot = XMODEM_EOT;

	zassert_equal(xmodem_rx_feed(&x, &eot, 1), XMODEM_FAILED);
}
