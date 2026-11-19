# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Star protocol pieces for Star Studio (no I/O): live frames, .csfx files, XMODEM-1K.

They match the firmware (sw/src/transfer/stream_frame.h, sw/src/storage/effect_format.h)
and the Android app (protocol/StreamFrame.kt, Csfx.kt, Xmodem.kt).
"""
import struct
import zlib

FACE_LEDS = 66
LED_COUNT = 132

ACK, NAK, CAN, EOT, STX, SOH = 0x06, 0x15, 0x18, 0x04, 0x02, 0x01
STREAM_FLAG_BACK_COPY = 0x01
STREAM_FLAG_END = 0x80


def crc16_xmodem(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def rgb565(c):
    r, g, b = (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def stream_frame(front=None, end=False):
    """A5 5A | flags | n | n x RGB565 LE | CRC-16/XMODEM BE (over flags..payload)."""
    if end:
        flags, colors = STREAM_FLAG_END, []
    else:
        flags, colors = STREAM_FLAG_BACK_COPY, list(front)[:FACE_LEDS]
    body = bytes([flags, len(colors)]) + b"".join(struct.pack("<H", rgb565(c)) for c in colors)
    return b"\xA5\x5A" + body + struct.pack(">H", crc16_xmodem(body))


def rgb888(v):
    r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2)


def decode_frames(buf):
    """Stream frames in buf -> [(flags, [0xRRGGBB, ...])]; bad or partial frames are skipped."""
    out, i = [], 0
    while True:
        i = buf.find(b"\xA5\x5A", i)
        if i < 0 or i + 4 > len(buf):
            return out
        n = buf[i + 3]
        end = i + 4 + 2 * n + 2
        if end > len(buf):
            return out
        body = buf[i + 2:end - 2]
        if struct.unpack(">H", buf[end - 2:end])[0] == crc16_xmodem(body):
            colors = [rgb888(v) for v in struct.unpack_from(f"<{n}H", buf, i + 4)]
            out.append((buf[i + 2], colors))
            i = end
        else:
            i += 2


def parse_effects(text):
    """'fx <id> <flags> <title>|<subtitle>' lines -> [dict(id, title, subtitle, tilt, two_faces)]."""
    out = []
    for line in text.replace("\r", "").split("\n"):
        parts = line.strip().split(" ", 3)
        if len(parts) < 4 or parts[0] != "fx":
            continue
        title, _, subtitle = parts[3].partition("|")
        out.append({"id": parts[1], "title": title, "subtitle": subtitle,
                    "tilt": "t" in parts[2], "twoFaces": "2" in parts[2]})
    return out


def csfx(name, frames, partner, loop=True):
    """Build a .csfx file. frames: [(66 front colours 0xRRGGBB, duration_ms)].

    The back LEDs copy their front partner (same picture on both faces), like the app.
    """
    data = bytearray()
    for front, duration in frames:
        data += struct.pack("<H", max(1, min(65535, int(duration))))
        for i in range(LED_COUNT):
            c = front[i if i < FACE_LEDS else partner[i]]
            data += bytes(((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF))
    header = struct.pack("<4sBBH16sII", b"CSFX", 1, LED_COUNT, 1 if loop else 0,
                         name.encode("utf-8")[:16].ljust(16, b"\0"), len(frames),
                         zlib.crc32(data) & 0xFFFFFFFF)
    return header + bytes(data)


IMAGE_MAGIC = 0x96F3B83D


def image_version(data):
    """Version "major.minor.rev[+build]" of a signed MCUboot image, or None."""
    if len(data) < 32 or struct.unpack_from("<I", data)[0] != IMAGE_MAGIC:
        return None
    major, minor, rev, build = struct.unpack_from("<BBHI", data, 20)
    return f"{major}.{minor}.{rev}" + (f"+{build}" if build else "")


def xmodem_blocks(data):
    """XMODEM-1K (CRC) blocks: STX | n | ~n | 1024 bytes (padded 0x1A) | CRC BE."""
    blocks = []
    for k, i in enumerate(range(0, len(data), 1024)):
        n = (k + 1) & 0xFF
        payload = bytes(data[i:i + 1024]).ljust(1024, b"\x1A")
        blocks.append(bytes([STX, n, 0xFF - n]) + payload + struct.pack(">H", crc16_xmodem(payload)))
    return blocks
