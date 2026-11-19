/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "xmodem.h"

#include <string.h>

uint16_t xmodem_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0;

	while (len--) {
		crc ^= (uint16_t)(*data++) << 8;
		for (int i = 0; i < 8; i++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

static void finish(struct xmodem_rx *x, enum xmodem_result r)
{
	if (r == XMODEM_FAILED) {
		x->send(x->ctx, XMODEM_CAN);
		x->send(x->ctx, XMODEM_CAN);
	}
	x->result = r;
	x->need = 0;
}

void xmodem_rx_start(struct xmodem_rx *x, xmodem_send_t send, xmodem_sink_t sink, void *ctx)
{
	memset(x, 0, sizeof(*x));
	x->send = send;
	x->sink = sink;
	x->ctx = ctx;
	x->expected = 1;
	x->result = XMODEM_RUNNING;
	x->retries = 1;
	x->send(x->ctx, XMODEM_CRC_REQ);
}

static void nak(struct xmodem_rx *x)
{
	x->need = 0;
	if (++x->retries > XMODEM_MAX_RETRIES) {
		finish(x, XMODEM_FAILED);
		return;
	}
	x->send(x->ctx, x->started ? XMODEM_NAK : XMODEM_CRC_REQ);
}

static void handle_packet(struct xmodem_rx *x)
{
	size_t size = x->need - 5;
	uint8_t blk = x->pkt[1];
	uint16_t crc = (uint16_t)((x->pkt[3 + size] << 8) | x->pkt[4 + size]);

	x->need = 0;

	if ((uint8_t)(blk ^ x->pkt[2]) != 0xFF || xmodem_crc16(&x->pkt[3], size) != crc) {
		nak(x);
		return;
	}
	if (blk == (uint8_t)(x->expected - 1) && x->started) {
		/* Duplicate (our ACK was lost): acknowledge again. */
		x->send(x->ctx, XMODEM_ACK);
		return;
	}
	if (blk != x->expected) {
		finish(x, XMODEM_FAILED);
		return;
	}

	int err = x->sink(x->ctx, &x->pkt[3], size);

	if (err != 0) {
		x->sink_error = err;
		finish(x, XMODEM_FAILED);
		return;
	}
	x->started = 1;
	x->expected++;
	x->retries = 0;
	x->send(x->ctx, XMODEM_ACK);
}

enum xmodem_result xmodem_rx_feed(struct xmodem_rx *x, const uint8_t *data, size_t len)
{
	while (len > 0 && x->result == XMODEM_RUNNING) {
		uint8_t c = *data++;

		len--;

		if (x->need == 0) {
			switch (c) {
			case XMODEM_SOH:
			case XMODEM_STX:
				x->need = (c == XMODEM_SOH ? 128 : 1024) + 5;
				x->pkt[0] = c;
				x->pos = 1;
				x->can_count = 0;
				break;
			case XMODEM_EOT:
				x->send(x->ctx, XMODEM_ACK);
				finish(x, x->started ? XMODEM_DONE : XMODEM_FAILED);
				break;
			case XMODEM_CAN:
				if (++x->can_count >= 2) {
					finish(x, XMODEM_CANCELLED);
				}
				break;
			default:
				/* Line noise between packets. */
				x->can_count = 0;
				break;
			}
			continue;
		}

		x->pkt[x->pos++] = c;
		if (x->pos == x->need) {
			handle_packet(x);
		}
	}
	return x->result;
}

enum xmodem_result xmodem_rx_tick(struct xmodem_rx *x)
{
	if (x->result != XMODEM_RUNNING) {
		return x->result;
	}
	if (!x->started) {
		x->need = 0;
		if (++x->retries > XMODEM_START_TRIES) {
			finish(x, XMODEM_FAILED);
		} else {
			x->send(x->ctx, XMODEM_CRC_REQ);
		}
		return x->result;
	}
	nak(x);
	return x->result;
}
