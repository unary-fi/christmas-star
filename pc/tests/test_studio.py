# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Tests for Star Studio's protocol pieces and the pairing helpers.

    python -m pytest pc/tests sw/tools/tests
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pc"))
sys.path.insert(0, str(ROOT / "sw" / "tools"))

import star_auth  # noqa: E402
import star_protocol as sp  # noqa: E402
from star_studio import parse_state  # noqa: E402


def test_crc16_xmodem_check_value():
    assert sp.crc16_xmodem(b"123456789") == 0x31C3


def test_stream_frame_layout():
    front = [0xFF0000, 0x00FF00, 0x0000FF] + [0] * 63
    f = sp.stream_frame(front)
    assert f[:4] == bytes([0xA5, 0x5A, sp.STREAM_FLAG_BACK_COPY, 66])
    assert len(f) == 4 + 2 * 66 + 2
    assert struct.unpack_from("<3H", f, 4) == (0xF800, 0x07E0, 0x001F)
    assert struct.unpack(">H", f[-2:])[0] == sp.crc16_xmodem(f[2:-2])
    end = sp.stream_frame(end=True)
    assert end[:4] == bytes([0xA5, 0x5A, sp.STREAM_FLAG_END, 0])


def test_csfx_back_copies_front():
    partner = list(range(66, 132)) + list(range(66))
    front = list(range(66))
    blob = sp.csfx("Test", [(front, 40)], partner, loop=True)
    magic, ver, leds, flags, name, frames, crc = struct.unpack_from("<4sBBH16sII", blob)
    assert (magic, ver, leds, flags, frames) == (b"CSFX", 1, 132, 1, 1)
    assert name.rstrip(b"\0") == b"Test"
    data = blob[32:]
    assert crc == zlib.crc32(data) & 0xFFFFFFFF
    assert struct.unpack_from("<H", data)[0] == 40
    # Back LED 66 shows front LED partner[66] = 0.
    assert data[2 + 3 * 66: 2 + 3 * 67] == bytes((0, 0, 0))
    assert data[2 + 3 * 70: 2 + 3 * 71] == bytes((0, 0, 4))


def test_csfx_matches_make_effect_header():
    import make_effect

    leds = make_effect.layout()
    partner = [e["partner"] for e in leds]
    blob = sp.csfx("snow", [([0x102030] * 66, 1000 / 25)], partner, loop=True)
    assert blob[:8] == make_effect.HEADER.pack(b"CSFX", 1, 132, 1, b"snow".ljust(16, b"\0"), 1, 0)[:8]


def test_xmodem_blocks():
    blocks = sp.xmodem_blocks(b"x" * 1500)
    assert len(blocks) == 2
    b = blocks[1]
    assert b[:3] == bytes([sp.STX, 2, 0xFD])
    assert b[3 + 476: 3 + 1024] == b"\x1A" * 548
    assert struct.unpack(">H", b[-2:])[0] == sp.crc16_xmodem(b[3:-2])


def test_image_version():
    hdr = bytearray(32)
    struct.pack_into("<IIHHII", hdr, 0, 0x96F3B83D, 0, 0x200, 0, 82000, 0)
    struct.pack_into("<BBHI", hdr, 20, 1, 3, 2, 0)
    assert sp.image_version(bytes(hdr)) == "1.3.2"
    struct.pack_into("<I", hdr, 24, 7)
    assert sp.image_version(bytes(hdr)) == "1.3.2+7"
    assert sp.image_version(b"CSFX" + bytes(40)) is None
    assert sp.image_version(b"\x3d\xb8") is None


def test_auth_vector_shared_with_firmware():
    # Same vector as sw/tests/unit/src/test_pair.c and the Android tests.
    mac = star_auth.auth_mac(bytes(range(16)), bytes(range(16, 32)))
    assert mac == "8b9b4fe50baada231361d9491a71d3ef7ae699bc95c7366a34c5784d99370d9a"


def test_parse_hello():
    info = star_auth.parse_hello("ok proto=2 leds=132 fw=1.2 owner=paired pairing=closed "
                                 "access=none nonce=00112233445566778899aabbccddeeff name=Living room")
    assert info["proto"] == 2 and info["owner"] == "paired" and info["name"] == "Living room"
    assert star_auth.parse_hello("ok proto=1 leds=132 fw=1.1")["proto"] == 1
    assert star_auth.parse_hello("locked") is None


def test_parse_state_with_spaces():
    s = parse_state("state effect=flash bright=128 shake=1 speed=100 fps=50 missing=1 cap=655040 "
                    "flash=My video frames=250 owner=paired name=Living room")
    assert s["effect"] == "flash" and s["flash"] == "My video" and s["frames"] == "250"
    assert s["owner"] == "paired" and s["name"] == "Living room"
    s = parse_state("state effect=rainbow bright=1 shake=0 speed=100 fps=50 missing=0 cap=1 "
                    "flash=- frames=0 owner=free name=")
    assert s["flash"] == "" and s["name"] == ""
