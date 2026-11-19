# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(christmas_star)

target_include_directories(app PRIVATE src)

# Developer key (pairing): 32 hex digits in sw/dev_key.txt, never committed.
# Without the file, or with the environment variable STAR_NO_DEV_KEY=1 (public
# release builds), the firmware accepts no developer key.
set(DEV_KEY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/dev_key.txt)
set(DEV_KEY_PRESENT 0)
set(DEV_KEY_BYTES "0")
if(EXISTS ${DEV_KEY_FILE} AND NOT "$ENV{STAR_NO_DEV_KEY}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DEV_KEY_FILE})
  file(READ ${DEV_KEY_FILE} DEV_KEY_HEX)
  string(STRIP "${DEV_KEY_HEX}" DEV_KEY_HEX)
  string(LENGTH "${DEV_KEY_HEX}" DEV_KEY_LEN)
  if(NOT DEV_KEY_LEN EQUAL 32 OR NOT DEV_KEY_HEX MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "sw/dev_key.txt must hold 32 hex digits (16 bytes)")
  endif()
  string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," DEV_KEY_BYTES "${DEV_KEY_HEX}")
  set(DEV_KEY_PRESENT 1)
  message(STATUS "Developer key: from sw/dev_key.txt")
else()
  message(STATUS "Developer key: none")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/app_generated/dev_key.h.tmp
  "/* Generated from sw/dev_key.txt. Do not commit. */\n"
  "#pragma once\n#include <stdint.h>\n"
  "#define DEV_KEY_PRESENT ${DEV_KEY_PRESENT}\n"
  "static const uint8_t dev_key[16] = {${DEV_KEY_BYTES}};\n")
configure_file(${CMAKE_BINARY_DIR}/app_generated/dev_key.h.tmp
  ${CMAKE_BINARY_DIR}/app_generated/dev_key.h COPYONLY)
target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/app_generated)

target_sources(app PRIVATE
	src/main.c
	src/layout/led_layout.c
	src/layout/led_layout_table.c
	src/led/led_chain.c
	src/led/led_limit.c
	src/led/supply_monitor.c
	src/led/led_spi_dma.c
	src/led/color.c
	src/effects/engine.c
	src/effects/fx_basic.c
	src/effects/fx_golden.c
	src/effects/fx_xmas.c
	src/effects/fx_xmas2.c
	src/effects/shake_overlay.c
	src/effects/fx_player.c
	src/imu/lsm6ds.c
	src/storage/zd35_nand.c
	src/storage/effect_format.c
	src/storage/effect_store.c
	src/transfer/xmodem.c
	src/transfer/stream_frame.c
	src/bt/bm78.c
	src/bt/bm78_ad.c
	src/bt/bm78_eeprom.c
	src/pair/sha256.c
	src/pair/pair_logic.c
	src/pair/pairing.c
	src/game/comet_logic.c
	src/game/game.c
	src/game/fx_cometgame.c
	src/fw/fw_image.c
	src/fw/cmd_fw.c
	src/config/star_settings.c
	src/cli/cmd_led.c
	src/cli/direct_parse.c
	src/cli/cmd_direct.c
	src/cli/cmd_effect.c
	src/cli/cmd_hw.c
	src/cli/cmd_app.c
)
