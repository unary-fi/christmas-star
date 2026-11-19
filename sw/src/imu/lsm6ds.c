/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#include "lsm6ds.h"

#include <errno.h>
#include <stdlib.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define IMU_NODE DT_NODELABEL(imu)

#define REG_WHO_AM_I  0x0F
#define REG_CTRL1_XL  0x10
#define REG_CTRL3_C   0x12
#define REG_OUTX_L_XL 0x28

#define WHO_AM_I_LSM6DS3  0x69
#define WHO_AM_I_LSM6DSL  0x6A

/* 104 Hz, +-4 g */
#define CTRL1_XL_104HZ_4G 0x48
/* BDU + IF_INC */
#define CTRL3_C_BDU_INC 0x44
/* +-4 g sensitivity: 0.122 mg/LSB */
#define RAW_TO_MG(v) ((int16_t)(((int32_t)(v) * 122) / 1000))

#define POLL_MS 10

static const struct i2c_dt_spec bus = I2C_DT_SPEC_GET(IMU_NODE);

static struct k_spinlock lock;
static struct imu_state state;
static struct imu_axis_map axis_map = {.src = {0, 1, 2}, .sign = {1, 1, 1}};
static uint8_t who_am_i;
static uint16_t shake_threshold = CONFIG_STAR_DEFAULT_SHAKE_THRESHOLD_MG;

static int read_sample(int16_t raw[3])
{
	uint8_t buf[6];
	int ret = i2c_burst_read_dt(&bus, REG_OUTX_L_XL, buf, sizeof(buf));

	if (ret == 0) {
		for (int i = 0; i < 3; i++) {
			raw[i] = (int16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
		}
	}
	return ret;
}

static void imu_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	/* Low-pass state in mg * 16 for precision. */
	int32_t lp[3] = {0, 0, 0};
	uint32_t energy = 0;
	bool primed = false;

	while (who_am_i == 0) {
		k_sleep(K_FOREVER);
	}

	while (true) {
		int16_t raw[3];
		int16_t mg[3];
		struct imu_axis_map m;

		k_msleep(POLL_MS);

		if (read_sample(raw) != 0) {
			K_SPINLOCK(&lock) {
				state.ok = false;
			}
			continue;
		}

		K_SPINLOCK(&lock) {
			m = axis_map;
		}
		for (int i = 0; i < 3; i++) {
			mg[i] = (int16_t)(m.sign[i] * RAW_TO_MG(raw[m.src[i]]));
		}

		if (!primed) {
			for (int i = 0; i < 3; i++) {
				lp[i] = mg[i] * 16;
			}
			primed = true;
		}

		uint32_t dyn = 0;

		for (int i = 0; i < 3; i++) {
			/* ~0.16 s time constant at 100 Hz. */
			lp[i] += mg[i] - lp[i] / 16;
			dyn += (uint32_t)abs(mg[i] - lp[i] / 16);
		}
		/* Decaying peak hold (~0.3 s). */
		energy = MAX(dyn, energy - energy / 32);

		K_SPINLOCK(&lock) {
			state.ax = mg[0];
			state.ay = mg[1];
			state.az = mg[2];
			state.gx = (int16_t)(lp[0] / 16);
			state.gy = (int16_t)(lp[1] / 16);
			state.gz = (int16_t)(lp[2] / 16);
			state.shake = (uint16_t)MIN(energy, UINT16_MAX);
			state.ok = true;
		}
	}
}

K_THREAD_DEFINE(imu_tid, 640, imu_thread, NULL, NULL, NULL, 7, 0, 0);

int imu_init(void)
{
	uint8_t id;
	int ret;

	if (!i2c_is_ready_dt(&bus)) {
		return -ENODEV;
	}
	ret = i2c_reg_read_byte_dt(&bus, REG_WHO_AM_I, &id);
	if (ret != 0) {
		return ret;
	}
	if (id != WHO_AM_I_LSM6DS3 && id != WHO_AM_I_LSM6DSL) {
		return -ENOTSUP;
	}
	ret = i2c_reg_write_byte_dt(&bus, REG_CTRL3_C, CTRL3_C_BDU_INC);
	if (ret == 0) {
		ret = i2c_reg_write_byte_dt(&bus, REG_CTRL1_XL, CTRL1_XL_104HZ_4G);
	}
	if (ret != 0) {
		return ret;
	}
	who_am_i = id;
	k_wakeup(imu_tid);
	return 0;
}

void imu_get(struct imu_state *out)
{
	K_SPINLOCK(&lock) {
		*out = state;
	}
}

uint8_t imu_who_am_i(void)
{
	return who_am_i;
}

void imu_set_axis_map(const struct imu_axis_map *map)
{
	K_SPINLOCK(&lock) {
		axis_map = *map;
	}
}

void imu_get_axis_map(struct imu_axis_map *map)
{
	K_SPINLOCK(&lock) {
		*map = axis_map;
	}
}

int imu_axis_map_parse(const char *s, struct imu_axis_map *map)
{
	struct imu_axis_map m;
	bool used[3] = {false, false, false};

	if (s == NULL) {
		return -EINVAL;
	}
	for (int i = 0; i < 3; i++) {
		if ((s[0] != '+' && s[0] != '-') || s[1] < 'x' || s[1] > 'z') {
			return -EINVAL;
		}
		m.sign[i] = (s[0] == '-') ? -1 : 1;
		m.src[i] = (int8_t)(s[1] - 'x');
		if (used[m.src[i]]) {
			return -EINVAL;
		}
		used[m.src[i]] = true;
		s += 2;
	}
	if (*s != '\0') {
		return -EINVAL;
	}
	*map = m;
	return 0;
}

void imu_axis_map_format(const struct imu_axis_map *map, char *buf)
{
	for (int i = 0; i < 3; i++) {
		buf[2 * i] = map->sign[i] < 0 ? '-' : '+';
		buf[2 * i + 1] = (char)('x' + map->src[i]);
	}
	buf[6] = '\0';
}

void imu_set_shake_threshold(uint16_t mg)
{
	shake_threshold = mg;
}

uint16_t imu_get_shake_threshold(void)
{
	return shake_threshold;
}
