/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "supply_monitor.h"

#include <errno.h>
#include <stdbool.h>
#include <stm32_ll_adc.h>
#include <stm32_ll_bus.h>
#include <zephyr/kernel.h>

/* Give up on a stuck ADC instead of hanging the LED engine. */
#define ADC_SPIN_LIMIT 20000

static bool running;

static bool wait_for(bool (*done)(void))
{
	for (int i = 0; i < ADC_SPIN_LIMIT; i++) {
		if (done()) {
			return true;
		}
	}
	return false;
}

static bool cal_done(void)
{
	return LL_ADC_IsCalibrationOnGoing(ADC1) == 0U;
}

static bool ready(void)
{
	return LL_ADC_IsActiveFlag_ADRDY(ADC1) != 0U;
}

static bool converted(void)
{
	return LL_ADC_IsActiveFlag_EOC(ADC1) != 0U;
}

int supply_monitor_init(void)
{
	LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_ADC1);

	if (LL_ADC_IsEnabled(ADC1) == 0U) {
		LL_ADC_SetClock(ADC1, LL_ADC_CLOCK_SYNC_PCLK_DIV4);
		LL_ADC_StartCalibration(ADC1);
		if (!wait_for(cal_done)) {
			return -EIO;
		}
		/* ADEN must not be set within 4 ADC clocks of the end of calibration. */
		k_busy_wait(2);
		LL_ADC_ClearFlag_ADRDY(ADC1);
		LL_ADC_Enable(ADC1);
		if (!wait_for(ready)) {
			return -EIO;
		}
	}
	LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_VREFINT);
	LL_ADC_SetSamplingTimeCommonChannels(ADC1, LL_ADC_SAMPLINGTIME_239CYCLES_5);
	LL_ADC_REG_SetSequencerChannels(ADC1, LL_ADC_CHANNEL_VREFINT);
	/* VREFINT start-up time (10 us max). */
	k_busy_wait(10);
	running = true;
	return 0;
}

uint16_t supply_monitor_min_mv(int samples)
{
	uint32_t max_raw = 0;

	if (!running) {
		return 0;
	}
	for (int i = 0; i < samples; i++) {
		LL_ADC_REG_StartConversion(ADC1);
		if (!wait_for(converted)) {
			return 0;
		}
		/* Lowest voltage = highest VREFINT reading. */
		max_raw = MAX(max_raw, LL_ADC_REG_ReadConversionData12(ADC1));
	}
	if (max_raw == 0U) {
		return 0;
	}
	return (uint16_t)(3300U * (uint32_t)*VREFINT_CAL_ADDR / max_raw);
}
