# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT

# J-Link is the default debug probe (SWD header J401).
board_runner_args(jlink "--device=STM32F030RC" "--iface=swd" "--speed=4000")
board_runner_args(pyocd "--target=stm32f030xc")

# keep first
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd-stm32.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
