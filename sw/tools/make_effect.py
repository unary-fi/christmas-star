#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Create a Christmas Star effect file (.csfx) from images.

Each input image (or each frame of an animated GIF) is fitted onto the star's
32x32 bitmap grid (see docs/led-layout.md) and sampled at the LED positions.
Images of any size work; they are scaled to cover the star's bounding box.

Examples:
  make_effect.py snow.gif -o snow.csfx --loop
  make_effect.py frame_*.png -o wave.csfx --fps 25
  make_effect.py front.gif --back back.gif -o two_sided.csfx

Back face options:
  --back-mode mirror  (default) back shows the front image mirrored, so it
                      looks the same when seen from behind
  --back-mode same    back LEDs copy their front partners (same XY)
  --back IMAGE        separate image(s) for the back, drawn as seen from behind
"""

import argparse
import glob
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_led_layout import DEFAULT_CSV, FACE_LEDS, GRID, LED_COUNT, build, load_leds  # noqa: E402

MAGIC = b"CSFX"
VERSION = 1
FLAG_LOOP = 1
HEADER = struct.Struct("<4sBBH16sII")


def layout():
    out, _ = build(load_leds(DEFAULT_CSV))
    return out


def load_frames(patterns):
    from PIL import Image, ImageSequence

    paths = []
    for p in patterns:
        found = sorted(glob.glob(p))
        paths.extend(found if found else [p])
    frames = []
    for path in paths:
        img = Image.open(path)
        for fr in ImageSequence.Iterator(img):
            dur = fr.info.get("duration", img.info.get("duration"))
            frames.append((fr.convert("RGB").resize((GRID, GRID), Image.LANCZOS), dur))
    return frames


def sample(img, col, row):
    return img.getpixel((col, row))


def encode_frame(leds, front_img, back_img, back_mode, duration_ms):
    rgb = bytearray()
    for e in leds:
        if e["face"] == 0:
            px = sample(front_img, e["col"], e["row"])
        elif back_img is not None:
            # Back image is drawn as seen from behind: mirror the column.
            px = sample(back_img, GRID - 1 - e["col"], e["row"])
        elif back_mode == "same":
            px = sample(front_img, e["col"], e["row"])
        else:
            # Mirror: a viewer behind sees the front picture the right way round.
            px = sample(front_img, GRID - 1 - e["col"], e["row"])
        rgb += bytes(px)
    return struct.pack("<H", max(1, min(65535, int(duration_ms)))) + bytes(rgb)


def make_effect(frames, back_frames, name, loop, fps, back_mode):
    leds = layout()
    assert len(leds) == LED_COUNT == 2 * FACE_LEDS
    data = bytearray()
    for i, (img, dur) in enumerate(frames):
        back = back_frames[i % len(back_frames)][0] if back_frames else None
        d = dur if (dur and fps is None) else 1000.0 / (fps or 25)
        data += encode_frame(leds, img, back, back_mode, d)
    header = HEADER.pack(MAGIC, VERSION, LED_COUNT, FLAG_LOOP if loop else 0,
                         name.encode("utf-8")[:16].ljust(16, b"\0"), len(frames),
                         zlib.crc32(data) & 0xFFFFFFFF)
    return header + bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+", help="image files, GIFs or glob patterns (front face)")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--name", help="effect name (max 16 chars, default: output file name)")
    ap.add_argument("--fps", type=float, help="frame rate (default: GIF timing, else 25)")
    ap.add_argument("--loop", action="store_true", help="repeat forever")
    ap.add_argument("--back", nargs="+", help="separate image(s) for the back face")
    ap.add_argument("--back-mode", choices=("mirror", "same"), default="mirror")
    args = ap.parse_args()

    frames = load_frames(args.images)
    if not frames:
        sys.exit("no frames")
    back = load_frames(args.back) if args.back else None
    name = args.name or os.path.splitext(os.path.basename(args.output))[0]
    blob = make_effect(frames, back, name, args.loop, args.fps, args.back_mode)
    with open(args.output, "wb") as f:
        f.write(blob)
    print(f"{args.output}: {len(frames)} frames, {len(blob)} bytes")


if __name__ == "__main__":
    main()
