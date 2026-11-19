# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
#
# Unit tests for the hardware-independent firmware modules.
#   west build -b qemu_cortex_m3 sw/tests/unit -d build/tests -t run
# or with twister:
#   west twister -T sw/tests -p qemu_cortex_m3

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(star_unit_tests)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_include_directories(app PRIVATE ${APP})
target_sources(app PRIVATE
	src/test_layout.c
	src/test_parse.c
	src/test_xmodem.c
	src/test_format.c
	src/test_effects.c
	src/test_stream.c
	src/test_supply.c
	src/test_pair.c
	src/test_fw.c
	src/test_comet.c
	src/stubs.c
	${APP}/layout/led_layout.c
	${APP}/layout/led_layout_table.c
	${APP}/led/color.c
	${APP}/led/led_limit.c
	${APP}/cli/direct_parse.c
	${APP}/transfer/xmodem.c
	${APP}/storage/effect_format.c
	${APP}/effects/fx_basic.c
	${APP}/effects/fx_golden.c
	${APP}/effects/fx_xmas.c
	${APP}/effects/fx_xmas2.c
	${APP}/effects/shake_overlay.c
	${APP}/transfer/stream_frame.c
	${APP}/pair/sha256.c
	${APP}/pair/pair_logic.c
	${APP}/bt/bm78_ad.c
	${APP}/fw/fw_image.c
	${APP}/game/comet_logic.c
)
