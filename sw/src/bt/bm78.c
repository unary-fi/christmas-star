/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "bm78.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define BM78_NODE DT_NODELABEL(bm78)

static const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET(BM78_NODE, reset_gpios);
static const struct gpio_dt_spec power = GPIO_DT_SPEC_GET(BM78_NODE, power_gpios);
static const struct gpio_dt_spec p20 = GPIO_DT_SPEC_GET(BM78_NODE, p20_gpios);
static const struct gpio_dt_spec p24 = GPIO_DT_SPEC_GET(BM78_NODE, p24_gpios);
static const struct gpio_dt_spec ean = GPIO_DT_SPEC_GET(BM78_NODE, ean_gpios);
static const struct gpio_dt_spec st1 = GPIO_DT_SPEC_GET(BM78_NODE, status1_gpios);
static const struct gpio_dt_spec st2 = GPIO_DT_SPEC_GET(BM78_NODE, status2_gpios);
static const struct gpio_dt_spec cts = GPIO_DT_SPEC_GET(BM78_NODE, cts_gpios);
static const struct gpio_dt_spec rts = GPIO_DT_SPEC_GET(BM78_NODE, rts_gpios);

int bm78_reset(void)
{
	return bm78_start(false);
}

int bm78_start(bool test_mode)
{
	int ret = 0;

	/* Hold the module in reset while setting the boot straps:
	 * P2_0 = 1, P2_4 = 1, EAN = 0 -> boot application from internal flash.
	 * P2_0 = 0, P2_4 = 1, EAN = 0 -> test mode (configuration over UART).
	 */
	ret |= gpio_pin_set_dt(&reset, 1);
	ret |= gpio_pin_set_dt(&p20, test_mode ? 0 : 1);
	ret |= gpio_pin_set_dt(&p24, 1);
	ret |= gpio_pin_set_dt(&ean, 0);
	ret |= gpio_pin_set_dt(&power, 1);
	k_msleep(20);
	ret |= gpio_pin_set_dt(&reset, 0);
	/* Allow the module to send to us. */
	ret |= gpio_pin_set_dt(&cts, 1);
	return ret ? -EIO : 0;
}

int bm78_init(void)
{
	int ret = 0;

	ret |= gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE);
	ret |= gpio_pin_configure_dt(&p20, GPIO_OUTPUT_ACTIVE);
	ret |= gpio_pin_configure_dt(&p24, GPIO_OUTPUT_ACTIVE);
	ret |= gpio_pin_configure_dt(&ean, GPIO_OUTPUT_INACTIVE);
	ret |= gpio_pin_configure_dt(&power, GPIO_OUTPUT_INACTIVE);
	ret |= gpio_pin_configure_dt(&cts, GPIO_OUTPUT_INACTIVE);
	ret |= gpio_pin_configure_dt(&rts, GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&st1, GPIO_INPUT);
	ret |= gpio_pin_configure_dt(&st2, GPIO_INPUT);
	if (ret) {
		return -EIO;
	}
	return bm78_reset();
}

void bm78_status(int *status1, int *status2)
{
	*status1 = gpio_pin_get_dt(&st1);
	*status2 = gpio_pin_get_dt(&st2);
}

int bm78_rts(void)
{
	return gpio_pin_get_dt(&rts);
}

static atomic_t glitch_resets;

static void glitch_reset_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)bm78_reset();
	atomic_inc(&glitch_resets);
}

static K_WORK_DELAYABLE_DEFINE(glitch_work, glitch_reset_fn);

void bm78_supply_glitch(void)
{
	(void)k_work_reschedule(&glitch_work, K_SECONDS(2));
}

uint32_t bm78_glitch_resets(void)
{
	return (uint32_t)atomic_get(&glitch_resets);
}
