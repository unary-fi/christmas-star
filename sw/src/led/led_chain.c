/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "led_chain.h"
#include "color.h"
#include "led_spi_dma.h"
#include "supply_monitor.h"
#include "bt/bm78.h"

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

BUILD_ASSERT(LED_SPI_DMA_MAX_LEDS == LED_COUNT, "chain-length must match the layout");
BUILD_ASSERT(sizeof(struct led_rgb) == 3, "frames are stored as packed RGB triplets");

static ATOMIC_DEFINE(missing, LED_COUNT);
static atomic_t brightness = ATOMIC_INIT(CONFIG_STAR_DEFAULT_BRIGHTNESS);
static atomic_t limit_ma = ATOMIC_INIT(CONFIG_STAR_DEFAULT_CURRENT_LIMIT_MA);
static atomic_t last_ma;
static atomic_t last_err;
static atomic_t err_count;

static struct led_rgb phys[LED_COUNT];

/* 3V3 samples per frame (~21 us each); the lowest one counts. */
#define SUPPLY_SAMPLES 8

static struct k_spinlock supply_lock;
static struct supply_limiter supply;
static struct led_supply_status supply_st = {.vdd_min_mv = UINT16_MAX};
static bool supply_ok;

int led_chain_init(void)
{
	led_chain_missing_from_string(CONFIG_STAR_DEFAULT_MISSING_LEDS);
	supply_limiter_init(&supply, (uint16_t)atomic_get(&limit_ma));
	supply_ok = supply_monitor_init() == 0;
	return led_spi_dma_init();
}

/*
 * Measure the 3V3 rail (it carries the load of the frame that is showing now)
 * and return the current limit for the next frame.
 */
static uint16_t supply_limit(void)
{
	const uint16_t max_ma = (uint16_t)atomic_get(&limit_ma);
	uint16_t vdd = supply_ok ? supply_monitor_min_mv(SUPPLY_SAMPLES) : 0;
	uint16_t lim = max_ma;
	bool deep = false;

	if (vdd == 0) {
		return max_ma;
	}
	K_SPINLOCK(&supply_lock) {
		deep = supply_limiter_update(&supply, vdd, (uint32_t)atomic_get(&last_ma), max_ma);
		lim = supply.limit_ma;
		supply_st.vdd_mv = vdd;
		supply_st.vdd_min_mv = MIN(supply_st.vdd_min_mv, vdd);
		supply_st.limit_ma = supply.limit_ma;
		supply_st.sags = supply.sags;
		supply_st.deep_sags = supply.deep_sags;
	}
	if (deep) {
		/* The BM78 has probably browned out: reset it once the rail is back. */
		bm78_supply_glitch();
	}
	return lim;
}

void led_chain_supply_status(struct led_supply_status *st)
{
	K_SPINLOCK(&supply_lock) {
		*st = supply_st;
	}
	if (st->vdd_min_mv == UINT16_MAX) {
		st->vdd_min_mv = 0;
	}
}

int led_chain_show(const struct led_rgb frame[LED_COUNT])
{
	const uint8_t level = (uint8_t)atomic_get(&brightness);
	uint32_t sum = 0;
	int n = 0;

	for (int i = 0; i < LED_COUNT; i++) {
		if (atomic_test_bit(missing, i)) {
			continue;
		}
		struct led_rgb c = color_scale(frame[i], level);

		c.r = color_gamma[c.r];
		c.g = color_gamma[c.g];
		c.b = color_gamma[c.b];
		sum += c.r + c.g + c.b;
		phys[n++] = c;
	}

	uint16_t scale = led_chain_limit_scale(sum, CONFIG_STAR_LED_MA_PER_CHANNEL, supply_limit());

	if (scale < 256) {
		sum = 0;
		for (int i = 0; i < n; i++) {
			phys[i].r = (uint8_t)((phys[i].r * scale) >> 8);
			phys[i].g = (uint8_t)((phys[i].g * scale) >> 8);
			phys[i].b = (uint8_t)((phys[i].b * scale) >> 8);
			sum += phys[i].r + phys[i].g + phys[i].b;
		}
	}
	atomic_set(&last_ma, (atomic_val_t)(sum * CONFIG_STAR_LED_MA_PER_CHANNEL / 255U));

	int rc = led_spi_dma_show(phys, n);

	if (rc != 0) {
		atomic_set(&last_err, rc);
		atomic_inc(&err_count);
	}
	return rc;
}

void led_chain_set_missing(int idx, bool is_missing)
{
	if (idx < 0 || idx >= LED_COUNT) {
		return;
	}
	atomic_set_bit_to(missing, idx, is_missing);
}

bool led_chain_is_missing(int idx)
{
	return idx >= 0 && idx < LED_COUNT && atomic_test_bit(missing, idx);
}

void led_chain_clear_missing(void)
{
	for (int i = 0; i < LED_COUNT; i++) {
		atomic_clear_bit(missing, i);
	}
}

int led_chain_present_count(void)
{
	int n = 0;

	for (int i = 0; i < LED_COUNT; i++) {
		n += atomic_test_bit(missing, i) ? 0 : 1;
	}
	return n;
}

void led_chain_get_missing(uint8_t bitmap[LED_MISSING_BYTES])
{
	memset(bitmap, 0, LED_MISSING_BYTES);
	for (int i = 0; i < LED_COUNT; i++) {
		if (atomic_test_bit(missing, i)) {
			bitmap[i / 8] |= (uint8_t)BIT(i % 8);
		}
	}
}

void led_chain_set_missing_bitmap(const uint8_t bitmap[LED_MISSING_BYTES])
{
	for (int i = 0; i < LED_COUNT; i++) {
		atomic_set_bit_to(missing, i, (bitmap[i / 8] & BIT(i % 8)) != 0);
	}
}

int led_chain_missing_from_string(const char *list)
{
	char name[8];
	int errors = 0;

	while (list != NULL && *list != '\0') {
		size_t len = 0;

		while (*list == ' ' || *list == ',') {
			list++;
		}
		while (list[len] != '\0' && list[len] != ' ' && list[len] != ',') {
			len++;
		}
		if (len == 0) {
			break;
		}
		if (len < sizeof(name)) {
			memcpy(name, list, len);
			name[len] = '\0';
			int idx = led_layout_parse(name);

			if (idx >= 0) {
				led_chain_set_missing(idx, true);
			} else {
				errors++;
			}
		} else {
			errors++;
		}
		list += len;
	}
	return errors;
}

void led_chain_set_brightness(uint8_t level)
{
	atomic_set(&brightness, level);
}

uint8_t led_chain_get_brightness(void)
{
	return (uint8_t)atomic_get(&brightness);
}

void led_chain_set_limit_ma(uint16_t ma)
{
	atomic_set(&limit_ma, ma);
	/* An explicit limit applies at once; the supply limiter cuts it again on a sag. */
	K_SPINLOCK(&supply_lock) {
		struct supply_limiter s = supply;

		supply_limiter_init(&supply, ma);
		supply.sags = s.sags;
		supply.deep_sags = s.deep_sags;
	}
}

uint16_t led_chain_get_limit_ma(void)
{
	return (uint16_t)atomic_get(&limit_ma);
}

uint32_t led_chain_last_ma(void)
{
	return (uint32_t)atomic_get(&last_ma);
}

int led_chain_last_error(uint32_t *count)
{
	*count = (uint32_t)atomic_get(&err_count);
	return (int)atomic_get(&last_err);
}
