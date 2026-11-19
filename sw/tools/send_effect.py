#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Upload a .csfx effect to the star over Bluetooth.

Over Bluetooth LE (Windows pairs the star's BM78 as "Dual-SPP" over BLE):
  send_effect.py --ble snow.csfx              # find "Dual-SPP" by name
  send_effect.py --ble 40:84:32:52:AD:04 snow.csfx --play

Over a serial port (Classic SPP COM port, or a wired UART adapter):
  send_effect.py --port COM12 snow.csfx

The star stores one effect at a time; uploading replaces it.

Requires: xmodem, and bleak (BLE) or pyserial (serial port)
"""

import argparse
import io
import os
import sys
import time

from xmodem import XMODEM1k

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

PROMPT = b"star:~$ "


class SerialLink:
    def __init__(self, port, baud):
        import serial

        self.p = serial.Serial(port, baud, timeout=0.2)

    def write(self, data):
        return self.p.write(data)

    def read(self, size=1, timeout=1.0):
        self.p.timeout = timeout
        return self.p.read(size)

    def read_until(self, token, timeout):
        end = time.time() + timeout
        out = b""
        while time.time() < end and token not in out:
            out += self.read(256, 0.2)
        return out

    def reset_input_buffer(self):
        self.p.reset_input_buffer()

    def close(self):
        self.p.close()


def upload(link, data, play=False):
    link.reset_input_buffer()
    link.write(b"\r\n")
    link.read_until(PROMPT, 3)
    link.write(b"effect upload\r\n")
    reply = link.read_until(b"XMODEM", 6)
    if b"XMODEM" not in reply:
        raise RuntimeError("star did not accept the upload:\n" + reply.decode(errors="replace"))
    # Drop the rest of the prompt text; the next byte from the star is 'C'.
    link.read_until(b"now...", 1)

    def getc(size, timeout=3):
        return link.read(size, timeout) or None

    def putc(data, timeout=3):
        return link.write(data)

    def progress(_total, ok, err):
        done = min(ok * 1024, len(data))
        print(f"\r{done}/{len(data)} bytes, {err} retries", end="", flush=True)

    t0 = time.time()
    ok = XMODEM1k(getc, putc).send(io.BytesIO(data), retry=10, callback=progress)
    print(f"\n{len(data)} bytes in {time.time() - t0:.1f} s")
    result = link.read_until(PROMPT, 6).decode(errors="replace").strip()
    print(result)
    if not ok or "OK" not in result:
        return False
    if play:
        link.write(b"effect play flash\r\n")
        print(link.read_until(PROMPT, 3).decode(errors="replace").strip())
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--ble", nargs="?", const="", metavar="ADDRESS",
                    help="use Bluetooth LE (optionally the star's address)")
    ap.add_argument("--port", help="serial port, e.g. COM12")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--play", action="store_true")
    args = ap.parse_args()

    data = open(args.file, "rb").read()
    if data[:4] != b"CSFX":
        sys.exit(f"{args.file} is not a .csfx file")

    if args.port:
        link = SerialLink(args.port, args.baud)
    elif args.ble is not None:
        from star_ble import StarBle

        import star_auth

        link = StarBle(args.ble or None)
        star_auth.login(link)  # the star's Bluetooth shell is locked until then
    else:
        sys.exit("give --ble [ADDRESS] or --port COMx")
    try:
        sys.exit(0 if upload(link, data, args.play) else 1)
    finally:
        link.close()


if __name__ == "__main__":
    main()
