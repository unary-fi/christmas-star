#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Run shell commands on the star over SEGGER RTT (J-Link, debug build).

  rtt_shell.py "led status" "nand info"
  rtt_shell.py --interactive

Requires: pylink-square and the SEGGER J-Link software.
"""

import argparse
import sys
import time

import pylink

PROMPT = b"rtt:~$ "


RCC_APB2ENR = 0x40021018
SYSCFG_CFGR1 = 0x40010000
FLASH_BASE = 0x08000000


def boot_from_flash(jl):
    """Start the firmware in flash even if BOOT0 selects the ROM bootloader.

    Hardware 1.0 has BOOT0 floating (no pull-down), so a reset usually ends
    up in ST's system bootloader. Halt at reset, map main flash at address 0
    (the Cortex-M0 has no VTOR), load SP/PC from the flash vector table and run.
    With MCUboot at the start of flash, this starts the bootloader, which
    checks the firmware's signature (a few seconds) and starts it.
    """
    jl.reset(halt=True)
    regs = {jl.register_name(i): i for i in jl.register_list()}

    def reg(prefix):
        return next(i for n, i in regs.items() if n.startswith(prefix))

    apb2 = jl.memory_read32(RCC_APB2ENR, 1)[0]
    jl.memory_write32(RCC_APB2ENR, [apb2 | 1])          # SYSCFG clock
    cfgr1 = jl.memory_read32(SYSCFG_CFGR1, 1)[0]
    jl.memory_write32(SYSCFG_CFGR1, [cfgr1 & ~0x3])     # MEM_MODE = main flash
    sp, pc = jl.memory_read32(FLASH_BASE, 2)
    jl.register_write(reg("R13"), sp)
    jl.register_write(reg("MSP"), sp)
    jl.register_write(reg("R15"), pc & ~1)
    jl.register_write(reg("XPSR"), 0x01000000)            # Thumb state
    jl.restart()


def connect(device, speed, boot_flash=False):
    jl = pylink.JLink()
    jl.open()
    jl.set_tif(pylink.enums.JLinkInterfaces.SWD)
    jl.connect(device, speed=speed)
    if boot_flash:
        boot_from_flash(jl)
        time.sleep(4.0)  # MCUboot verifies the firmware before starting it
    jl.rtt_start()
    for _ in range(50):
        try:
            if jl.rtt_get_num_up_buffers() > 0:
                break
        except pylink.errors.JLinkRTTException:
            pass
        time.sleep(0.1)
    return jl


def read_for(jl, seconds, until=None):
    out = b""
    end = time.time() + seconds
    while time.time() < end:
        data = bytes(jl.rtt_read(0, 1024))
        if data:
            out += data
            if until and out.rstrip().endswith(until.rstrip()):
                break
        else:
            time.sleep(0.02)
    return out


def run(jl, command, timeout):
    jl.rtt_write(0, list((command + "\r\n").encode()))
    out = read_for(jl, timeout, PROMPT)
    text = out.decode(errors="replace")
    # Drop the echoed command and the trailing prompt.
    lines = [ln for ln in text.replace("\r", "").split("\n") if ln.strip() and not ln.startswith("rtt:~$")]
    if lines and lines[0].strip() == command:
        lines = lines[1:]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*")
    ap.add_argument("--device", default="STM32F030RC")
    ap.add_argument("--speed", type=int, default=4000)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--boot-log", action="store_true", help="print what the star has printed so far")
    ap.add_argument("--interactive", action="store_true")
    ap.add_argument("--boot-flash", action="store_true",
                    help="restart the firmware from flash first (works around the floating BOOT0 pin)")
    args = ap.parse_args()

    jl = connect(args.device, args.speed, args.boot_flash)
    try:
        pending = read_for(jl, 0.5)
        if args.boot_log:
            print(pending.decode(errors="replace"))
        for c in args.commands:
            print(f"$ {c}")
            print(run(jl, c, args.timeout))
        if args.interactive:
            while True:
                try:
                    c = input("rtt> ")
                except EOFError:
                    break
                print(run(jl, c, args.timeout))
    finally:
        jl.rtt_stop()
        jl.close()


if __name__ == "__main__":
    sys.exit(main())
