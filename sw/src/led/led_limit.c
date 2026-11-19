/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "led_chain.h"

#include <zephyr/sys/util.h>

uint16_t led_chain_limit_scale(uint32_t channel_sum, uint16_t ma_per_channel, uint16_t limit_ma)
{
	uint32_t est = channel_sum * ma_per_channel / 255U;

	if (limit_ma == 0 || est <= limit_ma) {
		return 256;
	}
	return (uint16_t)((uint32_t)limit_ma * 256U / est);
}

void supply_limiter_init(struct supply_limiter *s, uint16_t max_ma)
{
	s->limit_ma = (max_ma == 0 || max_ma > SUPPLY_LIMIT_CEILING_MA) ? SUPPLY_LIMIT_CEILING_MA
									 : max_ma;
	s->calm_frames = 0;
	s->sags = 0;
	s->deep_sags = 0;
}

bool supply_limiter_update(struct supply_limiter *s, uint16_t vdd_mv, uint32_t last_ma,
			   uint16_t max_ma)
{
	bool deep = false;

	if (max_ma == 0 || max_ma > SUPPLY_LIMIT_CEILING_MA) {
		max_ma = SUPPLY_LIMIT_CEILING_MA;
	}
	if (s->limit_ma > max_ma) {
		s->limit_ma = max_ma;
	}

	if (vdd_mv < SUPPLY_SAG_MV) {
		/* Cut below what was actually drawn: 1/2 on a deep sag, 7/8 otherwise. */
		uint32_t base = MIN(last_ma, (uint32_t)s->limit_ma);

		deep = vdd_mv < SUPPLY_DEEP_SAG_MV;
		base = deep ? base / 2U : base * 7U / 8U;
		s->limit_ma = (uint16_t)MAX(base, (uint32_t)SUPPLY_LIMIT_FLOOR_MA);
		s->calm_frames = 0;
		s->sags++;
		if (deep) {
			s->deep_sags++;
		}
	} else if (vdd_mv >= SUPPLY_OK_MV) {
		/* Creep back up slowly once the rail has been calm for a while. */
		if (s->calm_frames < SUPPLY_CALM_FRAMES) {
			s->calm_frames++;
		} else if (s->limit_ma < max_ma) {
			s->limit_ma++;
			s->calm_frames -= SUPPLY_RAISE_EVERY;
		}
	}
	return deep;
}
