# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Tests for the host tools: layout generation and the .csfx file format."""

import os
import struct
import sys
import zlib

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import gen_led_layout as gl  # noqa: E402
import make_effect as me  # noqa: E402


@pytest.fixture(scope="module")
def leds():
    out, cells = gl.build(gl.load_leds(gl.DEFAULT_CSV))
    return out, cells


def test_layout_counts(leds):
    out, cells = leds
    assert len(out) == 132
    assert len(cells) == 66
    assert sum(1 for e in out if e["face"] == 0 and e["group"] == 0) == 48
    assert sum(1 for e in out if e["face"] == 0 and e["group"] == 1) == 18


def test_partners_symmetric(leds):
    out, _ = leds
    for e in out:
        p = out[e["partner"]]
        assert p["partner"] == e["idx"]
        assert p["face"] != e["face"]


def test_d259_position(leds):
    out, _ = leds
    d259 = next(e for e in out if e["ref"] == "D259")
    assert d259["idx"] == 58
    assert d259["group"] == 1  # inner star tip


def test_generated_c_is_current(leds, tmp_path):
    out, cells = leds
    current = open(gl.DEFAULT_C, encoding="utf-8").read()
    assert gl.gen_c(out, cells) == current, "rerun sw/tools/gen_led_layout.py"


def test_effect_file_format(tmp_path):
    from PIL import Image

    imgs = []
    for color in ((255, 0, 0), (0, 0, 255)):
        imgs.append((Image.new("RGB", (gl.GRID, gl.GRID), color), 100))
    blob = me.make_effect(imgs, None, "test", True, None, "mirror")

    magic, ver, count, flags, name, frames, crc = me.HEADER.unpack_from(blob)
    assert me.HEADER.size == 32
    assert magic == b"CSFX" and ver == 1 and count == 132 and flags == 1
    assert name.rstrip(b"\0") == b"test" and frames == 2
    data = blob[32:]
    assert len(data) == 2 * 398
    assert zlib.crc32(data) & 0xFFFFFFFF == crc
    dur, = struct.unpack_from("<H", data)
    assert dur == 100
    assert data[2:5] == bytes((255, 0, 0))
    assert data[398 + 2:398 + 5] == bytes((0, 0, 255))


def test_back_mirror(tmp_path):
    from PIL import Image

    img = Image.new("RGB", (gl.GRID, gl.GRID), (0, 0, 0))
    # Light the left half of the picture.
    for x in range(gl.GRID // 2):
        for y in range(gl.GRID):
            img.putpixel((x, y), (255, 255, 255))
    blob = me.make_effect([(img, 40)], None, "m", False, None, "mirror")
    leds = me.layout()
    rgb = blob[32 + 2:]
    for e in leds:
        lit = rgb[e["idx"] * 3] == 255
        if e["face"] == 0:
            assert lit == (e["col"] < gl.GRID // 2)
        else:
            # Seen from behind the left half is on the viewer's left too.
            assert lit == (gl.GRID - 1 - e["col"] < gl.GRID // 2)
