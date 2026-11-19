#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Talk to the star over Bluetooth LE (BM78 Transparent UART service).

Windows pairs the BM78 as a BLE device, which does not create a COM port.
This module provides a serial-like byte stream over the Microchip Transparent
UART GATT service, and a small interactive terminal. The star's shell is
locked until the client authenticates (star_auth.py); this tool logs in with
the developer key (sw/dev_key.txt) or this PC's own key.

  star_ble.py --list                # nearby stars
  star_ble.py                       # connect to the nearest star, open a terminal
  star_ble.py --star Kitchen "led status" "effect play rainbow"
  star_ble.py --address 40:84:32:52:AE:94 --pair      # pair the star with this PC

Requires: bleak
"""

import argparse
import asyncio
import sys
import threading
import time

from bleak import BleakClient, BleakScanner

import star_auth

SERVICE = "49535343-fe7d-4ae5-8fa9-9fafd205e455"
TX_CHAR = "49535343-1e4d-4bd9-ba61-23c647249616"  # module -> host (notify)
RX_CHAR = "49535343-8841-43f4-a8d4-ecbe34729bb3"  # host -> module (write)
FACTORY_NAME = "Dual-SPP"


def is_star(name, adv):
    """Stars list the service in their scan response (after renaming) or keep the factory name."""
    return SERVICE in [u.lower() for u in adv.service_uuids] or name == FACTORY_NAME


async def scan(timeout=5.0):
    """Return [(address, name, rssi)] of nearby stars, strongest first."""
    found = await BleakScanner.discover(timeout=timeout, return_adv=True)
    stars = [(d.address, a.local_name or d.name or "", a.rssi)
             for d, a in found.values() if is_star(a.local_name or d.name, a)]
    return sorted(stars, key=lambda s: -s[2])


class StarBle:
    """Blocking, serial-like wrapper around the BLE transparent UART."""

    def __init__(self, address=None, name=None, timeout=20.0):
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()
        self._buf = bytearray()
        self._cond = threading.Condition()
        self._client = None
        self._run(self._connect(address, name, timeout))

    def _run(self, coro, timeout=60.0):
        return asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout)

    async def _connect(self, address, name, timeout):
        if not address:
            stars = []
            for _ in range(3):  # Windows scans sometimes miss the scan response
                stars = await scan(min(timeout, 6.0))
                if name:
                    stars = [s for s in stars if s[1] == name]
                if stars:
                    break
            if not stars:
                raise RuntimeError(f"star not found ({name or 'none nearby'})")
            address = stars[0][0]
        self.address = address
        for attempt in range(3):
            dev = await BleakScanner.find_device_by_address(address, timeout=timeout)
            if dev is None:
                raise RuntimeError(f"star not found ({address})")
            self._client = BleakClient(dev, timeout=timeout)
            try:
                await self._client.connect()
                await self._client.start_notify(TX_CHAR, self._on_data)
                break
            except Exception:
                # Windows sometimes drops a fresh link (e.g. the module just restarted).
                try:
                    await self._client.disconnect()
                except Exception:
                    pass
                if attempt == 2:
                    raise
                await asyncio.sleep(1.5)
        # Leave room for the ATT header; the BM78 may only accept 20 bytes.
        self.chunk = max(20, min(self._client.mtu_size - 3, 128))

    def _on_data(self, _handle, data):
        with self._cond:
            self._buf.extend(data)
            self._cond.notify_all()

    def write(self, data):
        for i in range(0, len(data), self.chunk):
            part = bytes(data[i:i + self.chunk])
            self._run(self._client.write_gatt_char(RX_CHAR, part, response=True))
        return len(data)

    def read(self, size=1, timeout=1.0):
        end = time.time() + timeout
        with self._cond:
            while not self._buf:
                left = end - time.time()
                if left <= 0:
                    return b""
                self._cond.wait(left)
            out = bytes(self._buf[:size])
            del self._buf[:size]
            return out

    def read_until(self, token, timeout):
        end = time.time() + timeout
        out = b""
        while time.time() < end:
            out += self.read(256, min(0.2, max(0.01, end - time.time())))
            if token in out:
                break
        return out

    def reset_input_buffer(self):
        with self._cond:
            self._buf.clear()

    def close(self):
        if self._client is not None:
            try:
                self._run(self._client.disconnect(), 10)
            except Exception:
                pass
        self._loop.call_soon_threadsafe(self._loop.stop)


def run_command(link, command, timeout=3.0):
    link.reset_input_buffer()
    link.write((command + "\r\n").encode())
    text = link.read_until(b"star:~$ ", timeout).decode(errors="replace")
    lines = [ln for ln in text.replace("\r", "").split("\n") if ln.strip()]
    lines = [ln for ln in lines if not ln.startswith("star:~$")]
    if lines and lines[0].strip() == command:
        lines = lines[1:]  # the shell echoes the command
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("commands", nargs="*")
    ap.add_argument("--address", help="BLE address, e.g. 40:84:32:52:AE:94")
    ap.add_argument("--star", help="star name (default: the nearest star)")
    ap.add_argument("--list", action="store_true", help="list nearby stars and exit")
    ap.add_argument("--pair", action="store_true", help="pair the star with this PC if its window is open")
    ap.add_argument("--timeout", type=float, default=3.0)
    args = ap.parse_args()

    if args.list:
        for addr, name, rssi in asyncio.run(scan()):
            print(f"{addr}  {rssi:4d} dBm  {name}")
        return 0
    link = StarBle(args.address, args.star)
    try:
        info = star_auth.login(link, pair=args.pair)
        print(f"# {info.get('name') or link.address}: owner={info.get('owner')} "
              f"access={info.get('access')}")
        if not args.commands:
            run_command(link, "app bye")  # echo back on for typing
        for c in args.commands:
            print(f"$ {c}")
            print(run_command(link, c, args.timeout))
        if not args.commands:
            print("Connected. Type commands, Ctrl+Z/Ctrl+D to quit.")
            while True:
                try:
                    c = input("star> ")
                except EOFError:
                    break
                print(run_command(link, c, args.timeout))
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
