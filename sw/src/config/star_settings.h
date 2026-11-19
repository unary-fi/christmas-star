/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * @file star_settings.h
 * @brief Persistent settings (internal flash, NVS).
 *
 * Keys under "star/": missing LEDs, brightness, current limit, IMU axis map
 * and shake threshold (saved when changed), and the running state: active
 * effect, manual frame, effect speed and shake on/off. The running state is
 * saved 3 s after the last change (debounced, to limit flash wear) and
 * restored at boot, so the star resumes whatever it was showing.
 */

/** @brief Load settings. Call before the engine starts. */
int star_settings_init(void);

/** @brief Start the effect that was running when the state was last saved. */
void star_settings_restore_effect(void);

/** @brief Note that the running state changed (schedules a debounced save). */
void star_settings_state_changed(void);

int star_settings_save_missing(void);
int star_settings_save_brightness(void);
int star_settings_save_limit(void);
int star_settings_save_imu(void);
