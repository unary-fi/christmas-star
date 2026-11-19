/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 *
 * Stubs for hardware modules referenced by the code under test.
 */

#include "imu/lsm6ds.h"

uint16_t imu_get_shake_threshold(void)
{
	return CONFIG_STAR_DEFAULT_SHAKE_THRESHOLD_MG;
}
