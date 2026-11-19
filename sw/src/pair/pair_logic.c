/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "pair_logic.h"

#include <errno.h>
#include <string.h>

void pair_logic_init(struct pair_logic *p, bool owned, int64_t now_ms)
{
	p->owned = owned;
	p->window = false;
	p->shake_since = -1;
	p->shake_last = 0;
	if (!owned) {
		pair_logic_open(p, now_ms);
	}
}

void pair_logic_open(struct pair_logic *p, int64_t now_ms)
{
	p->window = true;
	p->window_until = now_ms + PAIR_WINDOW_MS;
}

bool pair_logic_tick(struct pair_logic *p, int64_t now_ms, bool shaking)
{
	bool opened = false;

	if (shaking) {
		if (p->shake_since < 0) {
			p->shake_since = now_ms;
		}
		p->shake_last = now_ms;
		if (now_ms - p->shake_since >= PAIR_SHAKE_MS) {
			opened = !p->window;
			pair_logic_open(p, now_ms);
			/* Keep shaking for another 5 s to extend, not every frame. */
			p->shake_since = now_ms;
		}
	} else if (p->shake_since >= 0 && now_ms - p->shake_last > PAIR_SHAKE_GAP_MS) {
		p->shake_since = -1;
	}
	if (p->window && now_ms >= p->window_until) {
		p->window = false;
	}
	return opened;
}

void pair_logic_paired(struct pair_logic *p)
{
	p->owned = true;
	p->window = false;
}

void pair_logic_released(struct pair_logic *p)
{
	/* Free again; the window opens at the next power-on (or by shaking). */
	p->owned = false;
}

uint32_t pair_logic_window_left(const struct pair_logic *p, int64_t now_ms)
{
	if (!p->window || now_ms >= p->window_until) {
		return 0;
	}
	return (uint32_t)(p->window_until - now_ms);
}

static char *skip_space(char *s)
{
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

/* Split off the next word; returns it (NUL-terminated) and advances *s. */
static char *word(char **s)
{
	char *w = skip_space(*s);
	char *e = w;

	while (*e != '\0' && *e != ' ' && *e != '\t') {
		e++;
	}
	if (*e != '\0') {
		*e++ = '\0';
	}
	*s = e;
	return w;
}

enum locked_cmd pair_parse_locked(char *line, const char **arg)
{
	char *s = line;
	char *w1 = word(&s);
	char *w2;
	char *w3;

	*arg = NULL;
	if (*w1 == '\0') {
		return LOCKED_NONE;
	}
	if (strcmp(w1, "help") == 0) {
		return LOCKED_HELP;
	}
	if (strcmp(w1, "app") != 0) {
		return LOCKED_DENIED;
	}
	w2 = word(&s);
	w3 = word(&s);
	if (*skip_space(s) != '\0') {
		return LOCKED_DENIED;
	}
	if (strcmp(w2, "hello") == 0 && *w3 == '\0') {
		return LOCKED_HELLO;
	}
	if (strcmp(w2, "auth") == 0 && strlen(w3) == 2 * 32) {
		*arg = w3;
		return LOCKED_AUTH;
	}
	if (strcmp(w2, "pair") == 0 && strlen(w3) == 2 * PAIR_KEY_LEN) {
		*arg = w3;
		return LOCKED_PAIR;
	}
	return LOCKED_DENIED;
}

static int nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

int pair_hex_decode(const char *hex, uint8_t *out, size_t len)
{
	if (strlen(hex) != 2 * len) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		int hi = nibble(hex[2 * i]);
		int lo = nibble(hex[2 * i + 1]);

		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

void pair_hex_encode(const uint8_t *in, size_t len, char *out)
{
	static const char digits[] = "0123456789abcdef";

	for (size_t i = 0; i < len; i++) {
		out[2 * i] = digits[in[i] >> 4];
		out[2 * i + 1] = digits[in[i] & 0xF];
	}
	out[2 * len] = '\0';
}

bool pair_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
	uint8_t d = 0;

	for (size_t i = 0; i < len; i++) {
		d |= a[i] ^ b[i];
	}
	return d == 0;
}
