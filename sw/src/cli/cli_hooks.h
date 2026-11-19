/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * @file cli_hooks.h
 * @brief Abort binary shell modes when the Bluetooth link drops.
 */

/** @brief End a running live stream (`stream`), if any. */
void app_stream_abort(void);

/** @brief Cancel a running XMODEM upload (`effect upload`), if any. */
void effect_upload_abort(void);

/** @brief Cancel a running firmware upload (`fw upload`), if any. */
void fw_upload_abort(void);
