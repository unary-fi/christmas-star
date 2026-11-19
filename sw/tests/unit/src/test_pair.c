/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <zephyr/ztest.h>

#include "bt/bm78_ad.h"
#include "pair/pair_logic.h"
#include "pair/sha256.h"

static void hex(const uint8_t *b, size_t n, char *out)
{
	pair_hex_encode(b, n, out);
}

ZTEST(pair, test_sha256_vectors)
{
	uint8_t d[SHA256_LEN];
	char h[2 * SHA256_LEN + 1];
	static char million[1000];

	sha256("abc", 3, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	sha256("", 0, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	/* One million 'a', fed in pieces. */
	struct sha256 s;

	memset(million, 'a', sizeof(million));
	sha256_init(&s);
	for (int i = 0; i < 1000; i++) {
		sha256_update(&s, million, sizeof(million));
	}
	sha256_final(&s, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

ZTEST(pair, test_hmac_rfc4231)
{
	uint8_t key[20];
	uint8_t d[SHA256_LEN];
	char h[2 * SHA256_LEN + 1];

	/* Test case 1, message split in two parts. */
	memset(key, 0x0b, sizeof(key));
	hmac_sha256(key, sizeof(key), "Hi ", 3, "There", 5, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

	/* Test case 2. */
	hmac_sha256((const uint8_t *)"Jefe", 4, "what do ya want ", 16, "for nothing?", 12, d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

/* Shared with the apps' tests: key 00..0f, nonce 10..1f. */
ZTEST(pair, test_auth_vector)
{
	uint8_t key[PAIR_KEY_LEN], nonce[PAIR_NONCE_LEN], d[SHA256_LEN];
	char h[2 * SHA256_LEN + 1];

	for (int i = 0; i < 16; i++) {
		key[i] = (uint8_t)i;
		nonce[i] = (uint8_t)(0x10 + i);
	}
	hmac_sha256(key, sizeof(key), "auth", 4, nonce, sizeof(nonce), d);
	hex(d, sizeof(d), h);
	zassert_str_equal(h, "8b9b4fe50baada231361d9491a71d3ef7ae699bc95c7366a34c5784d99370d9a");
}

ZTEST(pair, test_window_only_when_free)
{
	struct pair_logic p;

	pair_logic_init(&p, false, 1000);
	zassert_true(p.window);
	zassert_equal(pair_logic_window_left(&p, 1000), PAIR_WINDOW_MS);
	pair_logic_tick(&p, 1000 + PAIR_WINDOW_MS, false);
	zassert_false(p.window);

	pair_logic_init(&p, true, 1000);
	zassert_false(p.window);
}

ZTEST(pair, test_shake_opens_window)
{
	struct pair_logic p;
	bool opened = false;
	int64_t t = 0;

	pair_logic_init(&p, true, 0);
	/* 4.9 s of shaking with short pauses: not yet. */
	for (t = 0; t < 4900; t += 20) {
		bool pause = (t % 1000) > 800 && (t % 1000) < 1000;

		opened |= pair_logic_tick(&p, t, !pause);
	}
	zassert_false(opened);
	for (; t <= 5100 && !opened; t += 20) {
		opened = pair_logic_tick(&p, t, true);
	}
	zassert_true(opened);
	zassert_true(p.window);
}

ZTEST(pair, test_long_pause_restarts_shake)
{
	struct pair_logic p;
	bool opened = false;

	pair_logic_init(&p, true, 0);
	for (int64_t t = 0; t < 3000; t += 20) {
		opened |= pair_logic_tick(&p, t, true);
	}
	/* 400 ms still: the count starts again. */
	for (int64_t t = 3000; t < 3400; t += 20) {
		opened |= pair_logic_tick(&p, t, false);
	}
	for (int64_t t = 3400; t < 6000; t += 20) {
		opened |= pair_logic_tick(&p, t, true);
	}
	zassert_false(opened);
}

ZTEST(pair, test_pair_and_release)
{
	struct pair_logic p;

	pair_logic_init(&p, false, 0);
	pair_logic_paired(&p);
	zassert_true(p.owned);
	zassert_false(p.window);
	pair_logic_released(&p);
	zassert_false(p.owned);
	/* No window until the next power-on (or shaking). */
	zassert_false(p.window);
}

ZTEST(pair, test_locked_parser)
{
	char l[100];
	const char *arg;

	strcpy(l, "app hello");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_HELLO);
	strcpy(l, "  app   hello  ");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_HELLO);
	strcpy(l, "");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_NONE);
	strcpy(l, "help");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_HELP);
	strcpy(l, "led status");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_DENIED);
	strcpy(l, "app state");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_DENIED);
	strcpy(l, "app pair 000102030405060708090a0b0c0d0e0f");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_PAIR);
	zassert_str_equal(arg, "000102030405060708090a0b0c0d0e0f");
	strcpy(l, "app pair 0001");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_DENIED);
	strcpy(l, "app auth 000102030405060708090a0b0c0d0e0f000102030405060708090a0b0c0d0e0f");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_AUTH);
	strcpy(l, "app hello extra");
	zassert_equal(pair_parse_locked(l, &arg), LOCKED_DENIED);
}

ZTEST(pair, test_hex)
{
	uint8_t b[4];

	zassert_ok(pair_hex_decode("00aBff10", b, 4));
	zassert_equal(b[1], 0xab);
	zassert_equal(b[2], 0xff);
	zassert_not_ok(pair_hex_decode("00ab", b, 4));
	zassert_not_ok(pair_hex_decode("00ag", b, 2));
	zassert_true(pair_equal((const uint8_t *)"abc", (const uint8_t *)"abc", 3));
	zassert_false(pair_equal((const uint8_t *)"abc", (const uint8_t *)"abd", 3));
}

/* The factory data of the BM78 on the second prototype board. */
static const uint8_t factory_adv[BM78_AD_BLOCK] = {
	0x0d, 0x09, 0x09, 'D', 'u', 'a', 'l', '-', 'S', 'P', 'P', 0x02, 0x01, 0x02,
};
static const uint8_t factory_scan[BM78_AD_BLOCK] = {0x03, 0x02, 0x0a, 0x02};

ZTEST(pair, test_ad_rename)
{
	uint8_t b[BM78_AD_BLOCK];
	static const uint8_t want[BM78_AD_BLOCK] = {
		0x0e, 0x0a, 0x09, 'S', 't', 'a', 'r', '-', 'A', 'E', '9', '4', 0x02, 0x01, 0x02,
	};

	memcpy(b, factory_adv, sizeof(b));
	zassert_ok(bm78_ad_set_name(b, "Star-AE94"));
	zassert_mem_equal(b, want, sizeof(want));

	zassert_ok(bm78_ad_set_name(b, "Sixteen chars!!!"));
	zassert_equal(b[0], 2 + 16 + 3);
	zassert_not_ok(bm78_ad_set_name(b, "Seventeen chars!!"));
	zassert_not_ok(bm78_ad_set_name(b, ""));

	memcpy(b, factory_adv, sizeof(b));
	b[0] = 40;
	zassert_not_ok(bm78_ad_set_name(b, "x"));
}

ZTEST(pair, test_ad_service)
{
	uint8_t b[BM78_AD_BLOCK];

	memcpy(b, factory_scan, sizeof(b));
	zassert_equal(bm78_ad_add_service(b), 1);
	zassert_equal(b[0], 18 + 3);
	zassert_equal(b[1], 17);
	zassert_equal(b[2], 0x07);
	zassert_equal(b[3], 0x55);
	zassert_equal(b[18], 0x49);
	zassert_equal(b[19], 0x02);
	zassert_equal(b[20], 0x0a);
	/* Already there: unchanged. */
	zassert_equal(bm78_ad_add_service(b), 0);
	zassert_equal(b[0], 18 + 3);
}

ZTEST(pair, test_default_name)
{
	static const uint8_t addr[6] = {0x94, 0xae, 0x52, 0x32, 0x84, 0x40};
	char n[BM78_NAME_MAX + 1];

	bm78_default_name(addr, n);
	zassert_str_equal(n, "Star-AE94");
}

ZTEST_SUITE(pair, NULL, NULL, NULL, NULL, NULL);
