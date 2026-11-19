/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "layout/led_layout.h"

/**
 * @file stream_frame.h
 * @brief Binary live-stream frames (app -> star), see the `stream` command.
 *
 * Frame: A5 5A | flags | n | n x RGB565 (little endian) | CRC-16/XMODEM (big endian)
 * The CRC covers flags, n and the payload.
 *  - flags bit 0: back copies front (payload = 66 front LEDs, each back LED
 *    shows the colour of its front partner at the same XY). Otherwise the
 *    payload holds all 132 LEDs in logical order.
 *  - flags bit 7: end of stream (n may be 0).
 * The star answers every frame with ACK (0x06), or NAK (0x15) if it was bad.
 */

#define STREAM_SYNC0 0xA5
#define STREAM_SYNC1 0x5A
#define STREAM_FLAG_BACK_COPY 0x01
#define STREAM_FLAG_END 0x80
#define STREAM_ACK 0x06
#define STREAM_NAK 0x15

enum stream_event {
	STREAM_NONE = 0,  /* need more bytes */
	STREAM_FRAME,     /* valid frame: use stream_rx.flags/.count/.payload */
	STREAM_BAD,       /* CRC or length error */
};

struct stream_rx {
	uint8_t state;
	uint8_t flags;
	uint8_t count;
	uint16_t pos;
	uint16_t need;
	uint8_t payload[2 * LED_COUNT + 2];
};

void stream_rx_init(struct stream_rx *s);

/** @brief Feed one received byte. */
enum stream_event stream_rx_feed(struct stream_rx *s, uint8_t c);
