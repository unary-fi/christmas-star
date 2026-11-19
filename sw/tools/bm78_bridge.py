#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Talk to the star's BM78 Bluetooth module through the J-Link (RTT).

The debug firmware (debug.conf) relays bytes between RTT and the BM78 UART
after `bt bridge test`, with the module restarted in test mode (P2_0 low),
where Microchip's tools read and write its configuration.

    python sw/tools/bm78_bridge.py probe               # HCI version / address
    python sw/tools/bm78_bridge.py hci 01 03 0c 00     # send raw bytes, print reply
    python sw/tools/bm78_bridge.py dump bm78.bin       # save the configuration EEPROM
    python sw/tools/bm78_bridge.py com COM21           # relay to a serial port
    python sw/tools/bm78_bridge.py rename [Star-AE94]  # set the advertised name
    python sw/tools/bm78_bridge.py restore bm78.bin    # write a backup back

`com` is for Microchip's PC tools (e.g. the IS1678 / BM78 UI configuration
tool). Install a virtual null-modem pair (com0com, e.g. COM20 <-> COM21),
run this script on one end and open the other end in the tool at 115200 baud.

The firmware is restarted when the script ends (BM78 back in application
mode) unless --stay is given.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rtt_shell  # noqa: E402

HCI_COMMANDS = {
    "reset": "01 03 0c 00",
    "version": "01 01 10 00",
    "bdaddr": "01 09 10 00",
}


def start_bridge(jl, mode):
    rtt_shell.read_for(jl, 0.3)
    jl.rtt_write(0, list(f"bt bridge {mode}\r\n".encode()))
    out = rtt_shell.read_for(jl, 3.0, "")
    deadline = time.time() + 3
    while b"BRIDGE READY" not in out and time.time() < deadline:
        out += rtt_shell.read_for(jl, 0.2)
    if b"BRIDGE READY" not in out:
        raise SystemExit("bridge did not start (is the debug firmware running?): %r" % out)
    # The module boots; drop the shell prompt and anything it prints first.
    time.sleep(0.5)
    rtt_shell.read_for(jl, 0.3)


def send(jl, data):
    for i in range(0, len(data), 32):
        jl.rtt_write(0, list(data[i:i + 32]))


def read_bytes(jl, seconds):
    out = b""
    end = time.time() + seconds
    while time.time() < end:
        chunk = bytes(jl.rtt_read(0, 1024))
        if chunk:
            out += chunk
            end = max(end, time.time() + 0.15)
        else:
            time.sleep(0.005)
    return out


def hexs(b):
    return " ".join(f"{x:02x}" for x in b)


def hci(jl, hexstr, wait=1.0):
    data = bytes.fromhex(hexstr.replace(" ", ""))
    send(jl, data)
    rx = read_bytes(jl, wait)
    print(f"tx: {hexs(data)}\nrx: {hexs(rx) if rx else '(nothing)'}")
    return rx


def eeprom_read_once(jl, addr, length):
    """Vendor HCI command 0xFC29: read `length` (<= 32) EEPROM bytes at `addr`."""
    read_bytes(jl, 0.02)  # drop leftovers
    send(jl, bytes([0x01, 0x29, 0xFC, 0x03, addr >> 8, addr & 0xFF, length]))
    rx = b""
    end = time.time() + 0.5
    want = 3 + 7 + length
    while len(rx) < want and time.time() < end:
        rx += bytes(jl.rtt_read(0, 256))
    # 04 0e <len> 01 29 fc 00(status) <addr hi> <addr lo> <len> <data...>
    head = bytes([0x04, 0x0E, 7 + length, 0x01, 0x29, 0xFC, 0x00, addr >> 8, addr & 0xFF, length])
    if len(rx) != want or rx[:10] != head:
        return None
    return rx[10:]


def eeprom_read(jl, addr, length, tries=6):
    """Read a block twice and accept it only when both copies match (the bridge can drop bytes)."""
    prev = None
    for _ in range(tries):
        cur = eeprom_read_once(jl, addr, length)
        if cur is not None and cur == prev:
            return cur
        prev = cur if cur is not None else prev
    raise RuntimeError(f"EEPROM read at 0x{addr:04x} failed")


def eeprom_write(jl, addr, data):
    """Vendor HCI command 0xFC27: write EEPROM bytes, 16 at a time, and verify."""
    for i in range(0, len(data), 16):
        part = bytes(data[i:i + 16])
        a = addr + i
        for _ in range(4):
            read_bytes(jl, 0.02)
            send(jl, bytes([0x01, 0x27, 0xFC, 3 + len(part), a >> 8, a & 0xFF, len(part)]) + part)
            rx = read_bytes(jl, 0.3)
            if rx == bytes([0x04, 0x0E, 0x04, 0x01, 0x27, 0xFC, 0x00]) and eeprom_read(jl, a, len(part)) == part:
                break
        else:
            raise RuntimeError(f"EEPROM write at 0x{a:04x} failed")


# Name fields in the BM78 configuration EEPROM (verified on BM78SPPS5MC2):
# the address at 0x0000 (little-endian), the device name at 0x000B (zero padded)
# and the advertising data at 0x03A0 (total length, then AD structures).
ADDR_FIELD = 0x0000
NAME_FIELD = 0x000B
ADV_FIELD = 0x03A0
SCAN_FIELD = 0x03C0  # scan response (total length, then AD structures)
NAME_MAX = 16
# Transparent UART service, little-endian as in advertising data. Advertised in
# the scan response so the apps find stars whatever their name.
SERVICE_AD = bytes([17, 0x07]) + bytes.fromhex("55e405d2af9fa98fe54a7dfe43535349")


def parse_ads(block):
    total = block[0]
    if total > 31:
        raise ValueError(f"unexpected AD length {total}")
    ads, i = [], 1
    while i < 1 + total:
        n = block[i]
        if n == 0 or i + 1 + n > 1 + total:
            raise ValueError("malformed advertising data")
        ads.append(bytes(block[i:i + 1 + n]))
        i += 1 + n
    return ads


def build_scan(scan_block):
    """Return the scan response with the service UUID added (unchanged if present)."""
    ads = parse_ads(scan_block)
    if SERVICE_AD in ads:
        return bytes(scan_block[:1 + scan_block[0]])
    body = SERVICE_AD + b"".join(ad for ad in ads if ad[1] not in (0x06, 0x07))
    if len(body) > 31:
        raise ValueError("scan response full")
    return bytes([len(body)]) + body


def default_name(eeprom_head):
    return "Star-%02X%02X" % (eeprom_head[1], eeprom_head[0])


def build_adv(adv_block, name):
    """Return the advertising block with its complete-name AD replaced."""
    ads = parse_ads(adv_block)
    named = [ad for ad in ads if ad[1] in (0x08, 0x09)]
    if len(named) != 1:
        raise ValueError("advertising data has no single name field")
    new_ad = bytes([1 + len(name), 0x09]) + name.encode()
    body = b"".join(new_ad if ad is named[0] else ad for ad in ads)
    if len(body) > 31:
        raise ValueError("name too long for the advertising data")
    return bytes([len(body)]) + body


def rename(jl, name):
    head = eeprom_read(jl, ADDR_FIELD, 32)
    name = name or default_name(head)
    if not (1 <= len(name) <= NAME_MAX) or not all(32 <= ord(c) < 127 for c in name):
        raise SystemExit(f"name must be 1-{NAME_MAX} printable ASCII characters")
    old_name = head[NAME_FIELD:].split(b"\0")[0]
    width = max(len(old_name), len(name))
    adv = eeprom_read(jl, ADV_FIELD, 32)
    new_adv = build_adv(adv, name)
    adv_width = max(1 + adv[0], len(new_adv))
    print(f"renaming {old_name.decode(errors='replace')!r} -> {name!r}")
    eeprom_write(jl, NAME_FIELD, name.encode().ljust(width, b"\0"))
    eeprom_write(jl, ADV_FIELD, new_adv.ljust(adv_width, b"\0"))
    scan = eeprom_read(jl, SCAN_FIELD, 32)
    new_scan = build_scan(scan)
    if new_scan != scan[:len(new_scan)]:
        eeprom_write(jl, SCAN_FIELD, new_scan.ljust(1 + scan[0], b"\0"))
    print("done; the module restarts with the new name")


def restore(jl, path):
    """Write back the bytes of a backup that differ from the module."""
    want = open(path, "rb").read()
    fixed = 0
    for addr in range(0, len(want), 32):
        part = want[addr:addr + 32]
        if eeprom_read(jl, addr, len(part)) != part:
            eeprom_write(jl, addr, part)
            fixed += 1
            print(f"  wrote 0x{addr:04x}")
    print(f"{fixed} blocks restored from {path}")


def dump(jl, path, size):
    data = bytearray()
    for addr in range(0, size, 32):
        data += eeprom_read(jl, addr, min(32, size - addr))
        if addr % 0x400 == 0:
            print(f"  0x{addr:04x}", flush=True)
    with open(path, "wb") as f:
        f.write(data)
    print(f"{len(data)} bytes -> {path}")


def relay(jl, port):
    import serial

    ser = serial.Serial(port, 115200, timeout=0)
    print(f"relaying RTT <-> {port}, Ctrl+C to stop")
    try:
        while True:
            busy = False
            down = ser.read(256)
            if down:
                send(jl, down)
                busy = True
            up = bytes(jl.rtt_read(0, 1024))
            if up:
                ser.write(up)
                busy = True
            if not busy:
                time.sleep(0.002)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["probe", "hci", "dump", "com", "rename", "restore"])
    ap.add_argument("args", nargs="*",
                    help="hex bytes for 'hci', file for 'dump'/'restore', port for 'com', name for 'rename'")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x1000, help="bytes to dump (default 0x1000)")
    ap.add_argument("--mode", choices=["test", "app"], default="test", help="BM78 mode (default test)")
    ap.add_argument("--stay", action="store_true", help="leave the bridge running at the end")
    ap.add_argument("--device", default="STM32F030RC")
    args = ap.parse_args()

    # Always start the firmware fresh: the bridge can't be left without a restart.
    jl = rtt_shell.connect(args.device, 4000, boot_flash=True)
    try:
        start_bridge(jl, args.mode)
        if args.action == "probe":
            for name in ("reset", "version", "bdaddr"):
                print(f"[{name}]")
                hci(jl, HCI_COMMANDS[name])
        elif args.action == "hci":
            hci(jl, " ".join(args.args))
        elif args.action == "dump":
            dump(jl, args.args[0] if args.args else "bm78_eeprom.bin", args.size)
        elif args.action == "rename":
            rename(jl, args.args[0] if args.args else None)
        elif args.action == "restore":
            if not args.args:
                raise SystemExit("give the backup file")
            restore(jl, args.args[0])
        else:
            if not args.args:
                raise SystemExit("give the serial port, e.g. com COM21")
            relay(jl, args.args[0])
    finally:
        if not args.stay:
            rtt_shell.boot_from_flash(jl)
        jl.rtt_stop()
        jl.close()


if __name__ == "__main__":
    main()
