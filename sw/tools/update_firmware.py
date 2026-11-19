#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Update a star's firmware over Bluetooth LE.

  update_firmware.py build/star/sw/zephyr/zephyr.signed.bin              # nearest star
  update_firmware.py --star "Living room" build/star/sw/zephyr/zephyr.signed.bin
  update_firmware.py --address 40:84:32:52:AE:94 FILE --permanent

The signed image (zephyr.signed.bin from a sysbuild build) is sent with
XMODEM-1K into the star's second firmware slot; then the star restarts and
MCUboot swaps the firmware. By default the update is a test: the new firmware
confirms itself after 20 s, and if it crashes or hangs before that, the next
restart brings the old firmware back. --permanent skips the trial.

The star needs the BOOT0 pull-down (hardware 1.1 or the 10 kOhm fix);
without it, the restart ends in ST's ROM bootloader.

Requires: bleak, xmodem
"""
import argparse
import io
import os
import struct
import sys
import time

from xmodem import XMODEM1k

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import star_auth  # noqa: E402
from star_ble import StarBle, run_command  # noqa: E402

IMAGE_MAGIC = 0x96F3B83D


def image_version(data):
    """Version of a signed MCUboot image, or None if it is not one."""
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != IMAGE_MAGIC:
        return None
    major, minor, rev, build = struct.unpack_from("<BBHI", data, 20)
    return f"{major}.{minor}.{rev}" + (f"+{build}" if build else "")


def upload(link, data):
    link.reset_input_buffer()
    link.write(b"fw upload\r\n")
    reply = link.read_until(b"now...", 6)
    if b"XMODEM" not in reply:
        raise RuntimeError("the star did not accept the upload:\n" + reply.decode(errors="replace"))

    def getc(size, timeout=3):
        return link.read(size, timeout) or None

    def putc(buf, timeout=3):
        return link.write(buf)

    def progress(_total, ok, err):
        done = min(ok * 1024, len(data))
        print(f"\r{done}/{len(data)} bytes, {err} retries", end="", flush=True)

    t0 = time.time()
    ok = XMODEM1k(getc, putc).send(io.BytesIO(data), retry=10, callback=progress)
    print(f"\n{len(data)} bytes in {time.time() - t0:.0f} s")
    result = link.read_until(b"star:~$ ", 10).decode(errors="replace").strip()
    print(result)
    return ok and "upload OK" in result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="zephyr.signed.bin")
    ap.add_argument("--address")
    ap.add_argument("--star", help="star name (default: the nearest star)")
    ap.add_argument("--permanent", action="store_true", help="no trial run (no automatic rollback)")
    args = ap.parse_args()

    data = open(args.image, "rb").read()
    version = image_version(data)
    if version is None:
        sys.exit(f"{args.image} is not a signed firmware image (use zephyr.signed.bin)")

    link = StarBle(args.address, args.star)
    address = link.address
    try:
        info = star_auth.login(link)
        print(f"{info.get('name') or address}: firmware {info.get('fw', '?')} -> {version}")
        if not upload(link, data):
            sys.exit("upload failed")
        print(run_command(link, "fw apply permanent" if args.permanent else "fw apply"))
    finally:
        link.close()

    # The star restarts and MCUboot swaps the slots. The Bluetooth module may
    # still advertise during the swap; the firmware restarts it when it starts,
    # which drops an early link. So retry connect + login until it answers.
    t0 = time.time()
    print("waiting for the star to restart...")
    time.sleep(10)
    info = None
    while time.time() - t0 < 120:
        link = None
        try:
            link = StarBle(address)
            info = star_auth.login(link)
            print(f"back after {time.time() - t0:.0f} s, now running {info.get('fw')}")
            print(run_command(link, "fw status"))
            break
        except Exception:
            time.sleep(4)
        finally:
            if link is not None:
                link.close()
    if info is None:
        sys.exit("the star did not come back; check it with the J-Link")
    if info.get("fw") != version.split("+")[0]:
        sys.exit("the star is not running the new firmware (it rolled back?)")


if __name__ == "__main__":
    main()
