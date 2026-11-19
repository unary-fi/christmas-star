#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Generate the Christmas Star LED layout table and bitmap documentation.

Reads the JLCPCB pick-and-place file of the star PCB and produces:
  - sw/src/layout/led_layout_table.c  (per-LED geometry for the firmware)
  - docs/led-layout.md                (ASCII bitmaps for effect authors)

Logical LED numbering follows the data chain: logical 0..65 are D201..D266
(front face = PCB top layer), logical 66..131 are D301..D366 (back face =
PCB bottom layer).

Coordinates are "front view": millimetres, origin at the star centre,
x to the right and y up when looking at the front (top layer) face. Both faces
have their LEDs at the same XY positions, so a back LED shares the coordinates
of its "partner" on the front.

Run without arguments from anywhere; paths are resolved relative to the repo.
"""

import argparse
import csv
import math
import os
import sys

FACE_LEDS = 66
LED_COUNT = 2 * FACE_LEDS
GRID = 32
CENTRE = (200.0, -150.0)
# Star tip radius is 110 mm; map the 220 mm span onto GRID-1 cells.
SPAN_MM = 220.0

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_CSV = os.path.join(REPO, "hw", "christmas-star", "production", "positions.csv")
DEFAULT_C = os.path.join(REPO, "sw", "src", "layout", "led_layout_table.c")
DEFAULT_MD = os.path.join(REPO, "docs", "led-layout.md")


def load_leds(path):
    leds = {}
    with open(path, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            ref = row["Designator"]
            if len(ref) == 4 and ref[0] == "D" and ref[1] in "23":
                leds[ref] = (float(row["Mid X"]), float(row["Mid Y"]), row["Layer"])
    expected = [f"D2{i:02d}" for i in range(1, 67)] + [f"D3{i:02d}" for i in range(1, 67)]
    missing = [d for d in expected if d not in leds]
    if missing:
        sys.exit(f"positions file lacks LEDs: {missing}")
    return [(d,) + leds[d] for d in expected]


def build(leds):
    out = []
    for idx, (ref, x, y, layer) in enumerate(leds):
        face = 0 if idx < FACE_LEDS else 1
        want = "top" if face == 0 else "bottom"
        if layer != want:
            sys.exit(f"{ref} is on {layer}, expected {want}")
        fx = x - CENTRE[0]
        fy = y - CENTRE[1]
        r = math.hypot(fx, fy)
        ang = math.degrees(math.atan2(fy, fx)) % 360.0
        out.append(dict(ref=ref, idx=idx, face=face, des=int(ref[2:]), fx=fx, fy=fy, r=r, ang=ang))

    # Partner = LED on the other face at the same XY.
    for e in out:
        others = [o for o in out if o["face"] != e["face"]]
        p = min(others, key=lambda o: (o["fx"] - e["fx"]) ** 2 + (o["fy"] - e["fy"]) ** 2)
        if math.hypot(p["fx"] - e["fx"], p["fy"] - e["fy"]) > 1.0:
            sys.exit(f"{e['ref']} has no partner on the other face")
        e["partner"] = p["idx"]

    # The faces share their XY positions by design; the placement file has
    # sub-0.1 mm differences that could otherwise round into different cells.
    # Back LEDs therefore take the exact coordinates of their front partner.
    for e in out:
        if e["face"] == 1:
            p = out[e["partner"]]
            e["fx"], e["fy"], e["r"], e["ang"] = p["fx"], p["fy"], p["r"], p["ang"]

    # Groups: inner star (r < 56 mm) and outline. Order both clockwise,
    # starting from the concave outline vertex at 120 degrees.
    for e in out:
        e["group"] = 1 if e["r"] < 56.0 else 0
        e["cw"] = (120.0 - e["ang"]) % 360.0
    for face in (0, 1):
        for group, per_point in ((0, 8), (1, 3)):
            members = sorted((e for e in out if e["face"] == face and e["group"] == group),
                             key=lambda e: e["cw"] if group == 0 else (e["cw"] + 20.0) % 360.0)
            expected = 48 if group == 0 else 18
            if len(members) != expected:
                sys.exit(f"face {face} group {group}: {len(members)} LEDs, expected {expected}")
            for step, e in enumerate(members):
                e["step"] = step
                e["point"] = step // per_point

    # 32x32 grid, row 0 at the top.
    s = (GRID - 1) / SPAN_MM
    half = (GRID - 1) / 2.0
    cells = {}
    for e in out:
        e["col"] = int(round(e["fx"] * s + half))
        e["row"] = int(round(half - e["fy"] * s))
        if not (0 <= e["col"] < GRID and 0 <= e["row"] < GRID):
            sys.exit(f"{e['ref']} falls outside the grid")
        if e["face"] == 0:
            if (e["col"], e["row"]) in cells:
                sys.exit(f"grid collision: {e['ref']} and {cells[(e['col'], e['row'])]['ref']}")
            cells[(e["col"], e["row"])] = e
    return out, cells


def gen_c(out, cells):
    lines = [
        "/*",
        " * Copyright (c) 2024-2026 Tuomas Terho",
        " * SPDX-License-Identifier: MIT",
        " *",
        " * GENERATED FILE - do not edit. Regenerate with sw/tools/gen_led_layout.py",
        " */",
        "",
        '#include "led_layout.h"',
        "",
        "const struct led_info led_layout[LED_COUNT] = {",
    ]
    for e in out:
        lines.append(
            "\t[{idx:3d}] = {{ .designator = {des:2d}, .face = {face}, .x = {x:4d}, .y = {y:4d}, "
            ".r = {r:3d}, .angle = {a:3d}, .col = {col:2d}, .row = {row:2d}, .group = {g}, "
            ".point = {pt}, .step = {st:2d}, .partner = {p:3d} }}, /* {ref} */".format(
                idx=e["idx"], des=e["des"], face=e["face"], x=int(round(e["fx"])), y=int(round(e["fy"])),
                r=int(round(e["r"])), a=int(round(e["ang"] * 256.0 / 360.0)) % 256, col=e["col"],
                row=e["row"], g="LED_GROUP_INNER" if e["group"] else "LED_GROUP_OUTLINE",
                pt=e["point"], st=e["step"], p=e["partner"], ref=e["ref"]))
    lines.append("};")
    lines.append("")
    lines.append("/* Front-face logical index for each grid cell, LED_NONE if empty. */")
    lines.append("const uint8_t led_grid[LED_GRID_SIZE][LED_GRID_SIZE] = {")
    for row in range(GRID):
        vals = ", ".join(f"{cells[(c, row)]['idx']:3d}" if (c, row) in cells else "LED_NONE" for c in range(GRID))
        lines.append(f"\t{{ {vals} }},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def ascii_map(cells_by_pos, label):
    rows = []
    rows.append("     " + "".join(f"{c:3d}" for c in range(GRID)))
    for r in range(GRID):
        rows.append(f"{r:3d}  " + "".join(f"{label(cells_by_pos[(c, r)]):>3s}" if (c, r) in cells_by_pos else "  ."
                                          for c in range(GRID)))
    return "\n".join(rows)


def gen_md(out):
    front = {(e["col"], e["row"]): e for e in out if e["face"] == 0}
    back_front_view = {(e["col"], e["row"]): e for e in out if e["face"] == 1}
    back_rear_view = {(GRID - 1 - e["col"], e["row"]): e for e in out if e["face"] == 1}
    des = lambda e: f"{e['des']}"

    md = []
    md.append("# Christmas Star LED layout")
    md.append("")
    md.append("> Generated by `sw/tools/gen_led_layout.py` from "
              "`hw/christmas-star/production/positions.csv`. Do not edit by hand.")
    md.append("")
    md.append("## Structure")
    md.append("")
    md.append("- 132 LEDs in **one data chain**: D201 → D266 (front face, PCB top layer), "
              "then D301 → D366 (back face, PCB bottom layer).")
    md.append("- Each face is a six-pointed star (hexagram) with a radius of 110 mm:")
    md.append("  - **Outline**: 48 LEDs, 8 per star point (concave vertex at r = 62.5 mm, "
              "then 74, 86, 100, tip at 110, 100, 86, 74 mm).")
    md.append("  - **Inner star**: 18 LEDs, rotated 30° to the outline. Its 6 tips sit at r = 50 mm "
              "and point at the outline's concave vertices; 12 LEDs at r ≈ 30 mm flank them.")
    md.append("- The back face LEDs sit at exactly the same XY as the front face LEDs "
              "(each LED has a *partner* on the other face).")
    md.append("")
    md.append("## Addressing")
    md.append("")
    md.append("| Term | Meaning |")
    md.append("|---|---|")
    md.append("| Side | 1 = front (top layer, D2xx), 2 = back (bottom layer, D3xx). Used by `set_led` / `set_all`. |")
    md.append("| Index | 1–66, the designator number: side 1 index 32 = D232. |")
    md.append("| Logical index | 0–131, position in the data chain (D201 = 0, D366 = 131). Used inside the firmware and in effect files. |")
    md.append("")
    md.append("## Coordinates")
    md.append("")
    md.append("- Front-view millimetres, origin at the star centre, x right, y up "
              "(as seen when looking at the front face).")
    md.append("- Polar angle: 0 = right, counter-clockwise, stored as 0–255 for 0–360°.")
    md.append(f"- Bitmap grid: {GRID}×{GRID} cells (≈{SPAN_MM / (GRID - 1):.1f} mm per cell), row 0 at the top. "
              "Every LED has its own cell; there are no collisions.")
    md.append("- `point` 0–5 numbers the points clockwise: outline points start from the top point, "
              "inner-star tips start from the upper-left tip (120°). "
              "`step` is the position along the outline (0–47) or inner star (0–17), clockwise from the same start.")
    md.append("")
    md.append("Effect files from `make_effect.py` sample images at these grid positions. "
              "Use the bitmaps below as a template when drawing a 32×32 image.")
    md.append("")
    md.append("## Front face (side 1, designators D2xx)")
    md.append("")
    md.append("Seen from the front. Numbers are the designator index (`set_led 1 <n> ...`).")
    md.append("")
    md.append("```")
    md.append(ascii_map(front, des))
    md.append("```")
    md.append("")
    md.append("## Back face (side 2, designators D3xx), seen from the back")
    md.append("")
    md.append("What a viewer behind the star sees: the front-view picture mirrored left-to-right. "
              "Numbers are the designator index (`set_led 2 <n> ...`).")
    md.append("")
    md.append("```")
    md.append(ascii_map(back_rear_view, des))
    md.append("```")
    md.append("")
    md.append("## Back face (side 2), in front-view coordinates")
    md.append("")
    md.append("The same back LEDs drawn as if seen through the board from the front. "
              "Each number sits in the same cell as its front partner.")
    md.append("")
    md.append("```")
    md.append(ascii_map(back_front_view, des))
    md.append("```")
    md.append("")
    md.append("## Table")
    md.append("")
    md.append("| Logical | LED | Side | x mm | y mm | r mm | angle° | col | row | group | point | step | partner |")
    md.append("|---:|---|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---|")
    for e in out:
        p = out[e["partner"]]
        md.append(f"| {e['idx']} | {e['ref']} | {e['face'] + 1} | {e['fx']:.1f} | {e['fy']:.1f} | {e['r']:.1f} | "
                  f"{e['ang']:.0f} | {e['col']} | {e['row']} | {'inner' if e['group'] else 'outline'} | "
                  f"{e['point']} | {e['step']} | {p['ref']} |")
    md.append("")
    return "\n".join(md)


def load_edges(path):
    """PCB outline (Edge.Cuts) line segments in star-centre mm, y up.

    KiCad y grows downwards and the star centre is at (200, 150) in board
    coordinates (the placement file uses y negated).
    """
    import re

    s = open(path, encoding="utf-8").read()
    segs = []
    for m in re.finditer(r"\n\t\(gr_line\b", s):
        start = m.start() + 1
        depth = 0
        i = start
        while True:
            if s[i] == "(":
                depth += 1
            elif s[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        blk = s[start:i + 1]
        if '"Edge.Cuts"' not in blk:
            continue
        a = re.search(r"\(start ([-\d.]+) ([-\d.]+)\)", blk)
        b = re.search(r"\(end ([-\d.]+) ([-\d.]+)\)", blk)
        segs.append((float(a.group(1)) - CENTRE[0], -(float(a.group(2)) + CENTRE[1]),
                     float(b.group(1)) - CENTRE[0], -(float(b.group(2)) + CENTRE[1])))
    if not segs:
        sys.exit("no Edge.Cuts lines found")
    return segs


def gen_kotlin(out, edges):
    def floats(vals):
        return ", ".join(f"{v:.2f}f" for v in vals)

    def ints(vals):
        return ", ".join(str(v) for v in vals)

    lines = [
        "// GENERATED FILE - do not edit. Regenerate with sw/tools/gen_led_layout.py",
        "package io.github.christmasstar.star",
        "",
        "/**",
        " * Physical layout of the star's 132 LEDs and the PCB outline.",
        " *",
        " * Logical index = position in the data chain: 0..65 front (D201..D266),",
        " * 66..131 back (D301..D366). Coordinates are front-view millimetres with",
        " * the origin at the star centre, x right, y up. Both faces share positions;",
        " * [partner] gives the LED at the same XY on the other face.",
        " */",
        "object StarLayout {",
        f"    const val FACE_LEDS = {FACE_LEDS}",
        f"    const val LED_COUNT = {LED_COUNT}",
        f"    const val RADIUS_MM = {SPAN_MM / 2:.1f}f",
        "",
        f"    val x = floatArrayOf({floats(e['fx'] for e in out)})",
        f"    val y = floatArrayOf({floats(e['fy'] for e in out)})",
        f"    val designator = intArrayOf({ints(e['des'] for e in out)})",
        f"    val partner = intArrayOf({ints(e['partner'] for e in out)})",
        "    /** 0 = outline, 1 = inner star */",
        f"    val group = intArrayOf({ints(e['group'] for e in out)})",
        f"    val step = intArrayOf({ints(e['step'] for e in out)})",
        "    /** Same integer values as the firmware table (mm, 0..255 angle). */",
        f"    val r = intArrayOf({ints(int(round(e['r'])) for e in out)})",
        f"    val angle = intArrayOf({ints(int(round(e['ang'] * 256.0 / 360.0)) % 256 for e in out)})",
        f"    val xi = intArrayOf({ints(int(round(e['fx'])) for e in out)})",
        f"    val yi = intArrayOf({ints(int(round(e['fy'])) for e in out)})",
        "",
        "    /** PCB outline (Edge.Cuts) as segments: x1, y1, x2, y2, ... in mm. */",
        f"    val edges = floatArrayOf({floats(v for seg in edges for v in seg)})",
        "}",
        "",
    ]
    return "\n".join(lines)


def gen_js(out, edges):
    def nums(vals, fmt="{:.2f}"):
        return ", ".join(fmt.format(v) for v in vals)

    lines = [
        "// GENERATED FILE - do not edit. Regenerate with sw/tools/gen_led_layout.py",
        "// Star layout for Star Studio (same data as the app's StarLayout.kt).",
        "export const StarLayout = {",
        f"  FACE_LEDS: {FACE_LEDS},",
        f"  LED_COUNT: {LED_COUNT},",
        f"  x: [{nums(e['fx'] for e in out)}],",
        f"  y: [{nums(e['fy'] for e in out)}],",
        f"  designator: [{nums((e['des'] for e in out), '{}')}],",
        f"  partner: [{nums((e['partner'] for e in out), '{}')}],",
        f"  group: [{nums((e['group'] for e in out), '{}')}],",
        f"  step: [{nums((e['step'] for e in out), '{}')}],",
        f"  r: [{nums((int(round(e['r'])) for e in out), '{}')}],",
        f"  angle: [{nums((int(round(e['ang'] * 256.0 / 360.0)) % 256 for e in out), '{}')}],",
        f"  xi: [{nums((int(round(e['fx'])) for e in out), '{}')}],",
        f"  yi: [{nums((int(round(e['fy'])) for e in out), '{}')}],",
        f"  edges: [{nums(v for seg in edges for v in seg)}],",
        "};",
        "",
    ]
    return "\n".join(lines)


DEFAULT_JS = os.path.join(REPO, "pc", "web", "star_layout.js")
DEFAULT_PCB = os.path.join(REPO, "hw", "christmas-star", "christmas-star.kicad_pcb")
DEFAULT_KT = os.path.join(REPO, "android", "app", "src", "main", "java", "io", "github",
                          "christmasstar", "star", "StarLayout.kt")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--positions", default=DEFAULT_CSV)
    ap.add_argument("--pcb", default=DEFAULT_PCB)
    ap.add_argument("--c-out", default=DEFAULT_C)
    ap.add_argument("--md-out", default=DEFAULT_MD)
    ap.add_argument("--kt-out", default=DEFAULT_KT)
    ap.add_argument("--js-out", default=DEFAULT_JS)
    args = ap.parse_args()

    out, cells = build(load_leds(args.positions))
    edges = load_edges(args.pcb)
    os.makedirs(os.path.dirname(args.kt_out), exist_ok=True)
    with open(args.kt_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_kotlin(out, edges))
    os.makedirs(os.path.dirname(args.js_out), exist_ok=True)
    with open(args.js_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_js(out, edges))
    os.makedirs(os.path.dirname(args.c_out), exist_ok=True)
    with open(args.c_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_c(out, cells))
    os.makedirs(os.path.dirname(args.md_out), exist_ok=True)
    with open(args.md_out, "w", encoding="utf-8", newline="\n") as f:
        f.write(gen_md(out))
    print(f"wrote {args.c_out}\nwrote {args.md_out}")


if __name__ == "__main__":
    main()
