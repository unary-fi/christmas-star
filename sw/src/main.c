/*
 * Copyright (c) 2024-2026 Tuomas Terho
 * SPDX-License-Identifier: MIT
 */

/*
 * Christmas Star firmware, hardware 1.0.
 *
 * Start-up order: LED chain (applies the default missing-LED list) ->
 * settings (override defaults) -> NAND + effect store -> accelerometer ->
 * BM78 Bluetooth module -> pairing (locks the Bluetooth shell) -> effect engine with the default effect.
 * Failures of optional parts are reported in the boot banner but do not stop
 * the star from glowing.
 */

#include "bt/bm78.h"
#include "config/star_settings.h"
#include "effects/effect.h"
#include "fw/fw_update.h"
#include "imu/lsm6ds.h"
#include "led/led_chain.h"
#include "pair/pairing.h"
#include "storage/effect_store.h"
#include "storage/zd35_nand.h"

#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/version.h>
#include <zephyr/shell/shell.h>

#if defined(CONFIG_SHELL_BACKEND_RTT)
#include <zephyr/shell/shell_rtt.h>
#endif
#include <zephyr/shell/shell_uart.h>

static int rc_led, rc_settings, rc_nand, rc_store, rc_imu, rc_bt, rc_pair;

static void banner(const struct shell *sh)
{
	if (sh == NULL) {
		return;
	}
	shell_print(sh, "\nChristmas Star firmware %s (Zephyr %s)", APP_VERSION_STRING,
		    KERNEL_VERSION_STRING);
	shell_print(sh, "leds %d, settings %d, nand %d, store %d, imu %d (0x%02x), bt %d, pair %d",
		    rc_led, rc_settings, rc_nand, rc_store, rc_imu, imu_who_am_i(), rc_bt, rc_pair);
	shell_print(sh, "star '%s', %s, pairing window %us", pairing_name(),
		    pairing_owned() ? "paired" : "free", pairing_window_left() / 1000);
	shell_print(sh, "effect: %s. Type 'help' for commands.", engine_get()->name);
}

int main(void)
{
	rc_led = led_chain_init();
	rc_settings = star_settings_init();

	rc_nand = nand_init();
	rc_store = rc_nand == 0 ? effect_store_init() : rc_nand;
	rc_imu = imu_init();
	rc_bt = bm78_init();
	rc_pair = pairing_init();

	star_settings_restore_effect();
	engine_start();
	fw_boot_check();

	banner(shell_backend_uart_get_ptr());
#if defined(CONFIG_SHELL_BACKEND_RTT)
	banner(shell_backend_rtt_get_ptr());
#endif
	return 0;
}
