/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "stream_frame.h"

#include <string.h>

enum {
	ST_SYNC0,
	ST_SYNC1,
	ST_FLAGS,
	ST_COUNT,
	ST_DATA,
};

void stream_rx_init(struct stream_rx *s)
{
	memset(s, 0, sizeof(*s));
}

static uint16_t crc_frame(const struct stream_rx *s, uint16_t payload_len)
{
	uint8_t head[2] = {s->flags, s->count};
	uint16_t crc = 0;

	/* CRC-16/XMODEM over flags, count and payload, computed incrementally. */
	for (int i = 0; i < 2; i++) {
		crc ^= (uint16_t)head[i] << 8;
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	for (uint16_t i = 0; i < payload_len; i++) {
		crc ^= (uint16_t)s->payload[i] << 8;
		for (int b = 0; b < 8; b++) {
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
		}
	}
	return crc;
}

enum stream_event stream_rx_feed(struct stream_rx *s, uint8_t c)
{
	switch (s->state) {
	case ST_SYNC0:
		if (c == STREAM_SYNC0) {
			s->state = ST_SYNC1;
		}
		return STREAM_NONE;
	case ST_SYNC1:
		s->state = c == STREAM_SYNC1 ? ST_FLAGS : (c == STREAM_SYNC0 ? ST_SYNC1 : ST_SYNC0);
		return STREAM_NONE;
	case ST_FLAGS:
		s->flags = c;
		s->state = ST_COUNT;
		return STREAM_NONE;
	case ST_COUNT: {
		int max = (s->flags & STREAM_FLAG_BACK_COPY) ? LED_FACE_COUNT : LED_COUNT;

		s->count = c;
		if (c > max) {
			s->state = ST_SYNC0;
			return STREAM_BAD;
		}
		s->pos = 0;
		s->need = (uint16_t)(2 * c + 2);
		s->state = ST_DATA;
		return STREAM_NONE;
	}
	case ST_DATA:
		s->payload[s->pos++] = c;
		if (s->pos < s->need) {
			return STREAM_NONE;
		}
		s->state = ST_SYNC0;
		{
			uint16_t len = (uint16_t)(2 * s->count);
			uint16_t crc = (uint16_t)((s->payload[len] << 8) | s->payload[len + 1]);

			return crc_frame(s, len) == crc ? STREAM_FRAME : STREAM_BAD;
		}
	default:
		s->state = ST_SYNC0;
		return STREAM_NONE;
	}
}
