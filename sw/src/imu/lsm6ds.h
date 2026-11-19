/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @file lsm6ds.h
 * @brief Minimal polled accelerometer driver for LSM6DS3 / LSM6DSL.
 *
 * The interrupt pins are not wired on hardware 1.0, so the sensor is polled
 * at 100 Hz. Values are converted to the board frame (front view: x right,
 * y up, z out of the front face) using a configurable axis map.
 */

struct imu_state {
	int16_t ax, ay, az;   /**< acceleration in board frame, mg */
	int16_t gx, gy, gz;   /**< low-passed gravity estimate, mg */
	uint16_t shake;       /**< high-passed motion energy, mg (decaying peak) */
	bool ok;              /**< sensor detected and running */
};

/** Axis map: for each board axis, the sensor axis (0..2) and sign. */
struct imu_axis_map {
	int8_t src[3];
	int8_t sign[3];
};

int imu_init(void);

/** @brief Copy the latest state (thread safe). */
void imu_get(struct imu_state *out);

/** @brief WHO_AM_I value read at init, 0 if not detected. */
uint8_t imu_who_am_i(void);

void imu_set_axis_map(const struct imu_axis_map *map);
void imu_get_axis_map(struct imu_axis_map *map);

/**
 * @brief Parse an axis map such as "+x+y+z" or "-y+x+z".
 * @return 0 on success, -EINVAL otherwise.
 */
int imu_axis_map_parse(const char *s, struct imu_axis_map *map);

/** @brief Format an axis map as "+x+y+z" (buf >= 7 bytes). */
void imu_axis_map_format(const struct imu_axis_map *map, char *buf);

void imu_set_shake_threshold(uint16_t mg);
uint16_t imu_get_shake_threshold(void);
