/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file xmodem.h
 * @brief XMODEM-CRC / XMODEM-1K receiver as a byte-driven state machine.
 *
 * Feed received bytes with xmodem_rx_feed() and call xmodem_rx_tick() when no
 * byte has arrived for XMODEM_TICK_MS. The receiver answers through the send
 * callback and passes block payloads to the sink callback, in order and
 * without duplicates. A non-zero sink return value cancels the transfer.
 */

#define XMODEM_SOH 0x01
#define XMODEM_STX 0x02
#define XMODEM_EOT 0x04
#define XMODEM_ACK 0x06
#define XMODEM_NAK 0x15
#define XMODEM_CAN 0x18
#define XMODEM_CRC_REQ 'C'

#define XMODEM_TICK_MS 3000
#define XMODEM_START_TRIES 20  /* 'C' requests before giving up (~60 s) */
#define XMODEM_MAX_RETRIES 10

enum xmodem_result {
	XMODEM_RUNNING = 0,
	XMODEM_DONE,
	XMODEM_FAILED,
	XMODEM_CANCELLED,
};

typedef void (*xmodem_send_t)(void *ctx, uint8_t c);
typedef int (*xmodem_sink_t)(void *ctx, const uint8_t *data, size_t len);

struct xmodem_rx {
	xmodem_send_t send;
	xmodem_sink_t sink;
	void *ctx;
	uint8_t pkt[3 + 1024 + 2];
	size_t pos;
	size_t need;          /* total packet length being collected, 0 = idle */
	uint8_t expected;     /* next block number */
	uint8_t retries;
	uint8_t can_count;
	uint8_t started;      /* first block received */
	enum xmodem_result result;
	int sink_error;       /* error returned by the sink, if any */
};

void xmodem_rx_start(struct xmodem_rx *x, xmodem_send_t send, xmodem_sink_t sink, void *ctx);
enum xmodem_result xmodem_rx_feed(struct xmodem_rx *x, const uint8_t *data, size_t len);
enum xmodem_result xmodem_rx_tick(struct xmodem_rx *x);

/** @brief CRC-16/XMODEM (poly 0x1021, init 0). */
uint16_t xmodem_crc16(const uint8_t *data, size_t len);
