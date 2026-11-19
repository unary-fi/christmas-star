#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Draw the Google Play store graphics for the Christmas Star app.

    python android/play/make_assets.py

  icon-512.png          512 x 512 store icon (the launcher icon's artwork,
                        res/drawable/ic_launcher_*.xml, full square)
  feature-graphic.png   1024 x 500 feature graphic: the star as on the PCB,
                        glowing gold, with the title in Cormorant Garamond
  christmas_star_banner.png     2400 x 600 web page banner (e.g. the Google Sites
                        header behind the page title): the star, centred
  christmas_star_footer.png     2400 x 300 web page footer: a garland of gold lights
  christmas_star_footer_low.png the same, kept low and to the side for footers with
                        text and links on top
  christmas_star_background.png  2400 x 1600 plain night sky for page sections
                        with text on top (Google Sites: section background)

  christmas_star_logo.png       512 x 512 star mark, transparent (site logo)
  christmas_star_logo_wide.png  star + "Christmas Star", transparent, for dark
                        backgrounds
  christmas_star_favicon.png    512 x 512 favicon: gold wireframe star on a
  christmas_star_favicon.ico    night-navy rounded square (.ico: 16, 32, 48 px)

Requires: pillow
"""
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FONT = ROOT / "android" / "app" / "src" / "main" / "res" / "font" / "cormorant_garamond.ttf"
LAYOUT = ROOT / "pc" / "web" / "star_layout.js"
SS = 4  # supersampling for smooth edges

NIGHT_TOP = (7, 11, 30)
NIGHT_BOTTOM = (36, 29, 82)
GOLD = (242, 193, 78)
GOLD_SOFT = (255, 231, 168)
TIP = (255, 244, 214)


def gradient(w, h, a, b, diagonal=False):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h) if diagonal else y / h
            px[x, y] = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
    return img


def icon(size=512):
    """The launcher icon (viewport 108) cropped to its central 80 units."""
    s = size * SS
    k = s / 80.0
    off = 14.0

    def p(x, y):
        return ((x - off) * k, (y - off) * k)

    img = gradient(s // 8, s // 8, (10, 16, 48), (36, 29, 82), diagonal=True).resize((s, s), Image.BICUBIC)
    d = ImageDraw.Draw(img, "RGBA")
    for x, y, r, a in [(22, 24, .8, 102), (86, 30, .7, 68), (80, 84, .8, 85), (26, 82, .6, 51), (40, 20, .5, 68)]:
        cx, cy = p(x, y)
        d.ellipse([cx - r * k, cy - r * k, cx + r * k, cy + r * k], fill=(255, 255, 255, a))

    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    cx, cy = p(54, 54)
    gd.ellipse([cx - 22 * k, cy - 22 * k, cx + 22 * k, cy + 22 * k], fill=GOLD + (70,))
    glow = glow.filter(ImageFilter.GaussianBlur(6 * k))
    img.paste(glow, (0, 0), glow)

    outer = [(54, 26), (61.5, 41), (78.2, 40), (69, 54), (78.2, 68), (61.5, 67), (54, 82),
             (46.5, 67), (29.8, 68), (39, 54), (29.8, 40), (46.5, 41)]
    inner = [(64, 54), (58.5, 56.6), (59, 62.7), (54, 59.2), (49, 62.7), (49.5, 56.6), (44, 54),
             (49.5, 51.4), (49, 45.3), (54, 48.8), (59, 45.3), (58.5, 51.4)]
    d = ImageDraw.Draw(img, "RGBA")
    d.line([p(*q) for q in outer + outer[:1]], fill=GOLD, width=int(2 * k), joint="curve")
    d.line([p(*q) for q in inner + inner[:1]], fill=GOLD_SOFT + (204,), width=int(1.2 * k), joint="curve")
    for x, y in [(54, 24.5), (54, 80), (28.6, 38.5), (79.4, 38.5), (28.6, 66.5), (79.4, 66.5)]:
        cx, cy = p(x, y + 1.8)
        d.ellipse([cx - 1.8 * k, cy - 1.8 * k, cx + 1.8 * k, cy + 1.8 * k], fill=TIP)
    return img.resize((size, size), Image.LANCZOS)


def layout():
    js = LAYOUT.read_text()

    def arr(name):
        return [float(v) for v in re.search(name + r": \[([^\]]*)\]", js).group(1).split(",")]

    n = 66
    return arr(r"\bx")[:n], arr(r"\by")[:n], arr("edges")


def feature(w=1024, h=500):
    W, H = w * SS, h * SS
    img = gradient(w // 4, h // 4, NIGHT_TOP, NIGHT_BOTTOM).resize((W, H), Image.BICUBIC).convert("RGBA")
    d = ImageDraw.Draw(img, "RGBA")
    # A sparse star field.
    seed = 2024
    for _ in range(90):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed % 10000) / 10000 * W
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = (seed % 10000) / 10000 * H
        r = (0.6 + (seed % 7) / 7) * SS
        a = 40 + seed % 90
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, a))

    xs, ys, edges = layout()
    cx, cy, scale = W * 0.775, H * 0.5, H / 300.0

    def p(x, y):
        return (cx + x * scale, cy - y * scale)

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - 75 * scale, cy - 75 * scale, cx + 75 * scale, cy + 75 * scale], fill=GOLD + (55,))
    for i in range(len(xs)):
        x, y = p(xs[i], ys[i])
        r = 7 * scale
        gd.ellipse([x - r, y - r, x + r, y + r], fill=(255, 175, 60, 170))
    glow = glow.filter(ImageFilter.GaussianBlur(5 * scale))
    img.alpha_composite(glow)

    d = ImageDraw.Draw(img, "RGBA")
    for k in range(0, len(edges), 4):
        d.line([p(edges[k], edges[k + 1]), p(edges[k + 2], edges[k + 3])], fill=GOLD + (215,),
               width=max(2, int(0.55 * scale)))
    for i in range(len(xs)):
        x, y = p(xs[i], ys[i])
        r = 2.0 * scale
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 228, 170, 255))

    title = ImageFont.truetype(str(FONT), 80 * SS)
    sub = ImageFont.truetype(str(FONT), 31 * SS)
    tx = 70 * SS
    d.text((tx, 160 * SS), "Christmas Star", font=title, fill=GOLD_SOFT)
    d.text((tx + 4 * SS, 272 * SS), "Light, colour and magic", font=sub, fill=(237, 239, 247, 230))
    d.text((tx + 4 * SS, 314 * SS), "for your smart Christmas star", font=sub, fill=(154, 163, 199, 255))
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def star_field(img, count, seed, max_alpha=130):
    """Sparse, faint stars over an RGBA image (deterministic)."""
    W, H = img.size
    d = ImageDraw.Draw(img, "RGBA")
    for _ in range(count):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = (seed % 10000) / 10000 * W
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = (seed % 10000) / 10000 * H
        r = (0.5 + (seed % 7) / 6) * SS
        a = 30 + seed % max(1, max_alpha - 30)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 255, a))


def glowing_star(img, cx, cy, scale, strength=1.0):
    """The star as on the PCB: gold outline and softly glowing LEDs."""
    W, H = img.size
    xs, ys, edges = layout()

    def p(x, y):
        return (cx + x * scale, cy - y * scale)

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse([cx - 80 * scale, cy - 80 * scale, cx + 80 * scale, cy + 80 * scale],
               fill=GOLD + (int(45 * strength),))
    for i in range(len(xs)):
        x, y = p(xs[i], ys[i])
        r = 7 * scale
        gd.ellipse([x - r, y - r, x + r, y + r], fill=(255, 175, 60, int(150 * strength)))
    glow = glow.filter(ImageFilter.GaussianBlur(5 * scale))
    img.alpha_composite(glow)
    d = ImageDraw.Draw(img, "RGBA")
    for k in range(0, len(edges), 4):
        d.line([p(edges[k], edges[k + 1]), p(edges[k + 2], edges[k + 3])],
               fill=GOLD + (int(200 * strength),), width=max(2, int(0.55 * scale)))
    for i in range(len(xs)):
        x, y = p(xs[i], ys[i])
        r = 2.0 * scale
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 228, 170, int(255 * strength)))


def banner(w=2400, h=600):
    """Night sky with the star in the middle; the page title goes on top."""
    W, H = w * SS, h * SS
    img = gradient(w // 8, h // 8, NIGHT_TOP, NIGHT_BOTTOM).resize((W, H), Image.BICUBIC).convert("RGBA")
    star_field(img, 220, 7)
    # Calmer than the feature graphic, so white title text stays readable.
    glowing_star(img, W / 2, H / 2, H / 290.0, strength=0.6)
    # A faint horizon glow along the bottom edge.
    haze = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(haze).ellipse([-W * 0.2, H * 0.82, W * 1.2, H * 1.5], fill=(242, 193, 78, 26))
    img.alpha_composite(haze.filter(ImageFilter.GaussianBlur(40 * SS)))
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def footer(w=2400, h=300):
    """A garland of small gold lights along a gentle wave, a little star in the middle."""
    W, H = w * SS, h * SS
    img = gradient(w // 8, h // 8, NIGHT_BOTTOM, NIGHT_TOP).resize((W, H), Image.BICUBIC).convert("RGBA")
    star_field(img, 90, 11, max_alpha=90)
    cx, cy = W / 2, H * 0.48

    def wave(x):
        return cy + math.sin((x - cx) / W * 2 * math.pi * 3) * H * 0.12

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    d = ImageDraw.Draw(img, "RGBA")
    # The wire, then the bulbs (a gap around the centre star).
    pts = [(x, wave(x)) for x in range(0, W + 1, 8 * SS)]
    d.line(pts, fill=GOLD + (70,), width=2 * SS)
    step = 46 * SS
    n = int(W / step)
    for k in range(n + 1):
        x = k * step + (W - n * step) / 2
        if abs(x - cx) < 110 * SS:
            continue
        y = wave(x)
        warm = (255, 196, 92) if k % 3 else (255, 232, 180)
        gd.ellipse([x - 13 * SS, y - 13 * SS, x + 13 * SS, y + 13 * SS], fill=warm + (110,))
        d.ellipse([x - 4 * SS, y - 4 * SS, x + 4 * SS, y + 4 * SS], fill=warm + (255,))
    # The little star (the app icon's hexagram), with a soft halo.
    r = 46 * SS
    gd.ellipse([cx - r * 1.3, cy - r * 1.3, cx + r * 1.3, cy + r * 1.3], fill=GOLD + (70,))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9 * SS)))
    d = ImageDraw.Draw(img, "RGBA")
    pts = []
    for i in range(12):
        a = math.pi / 2 + i * math.pi / 6
        rr = r if i % 2 == 0 else r * 0.58
        pts.append((cx + rr * math.cos(a), cy - rr * math.sin(a)))
    d.line(pts + pts[:1], fill=GOLD, width=int(3.2 * SS), joint="curve")
    for i in range(0, 12, 2):
        x, y = pts[i]
        d.ellipse([x - 4.5 * SS, y - 4.5 * SS, x + 4.5 * SS, y + 4.5 * SS], fill=TIP)
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def footer_low(w=2400, h=300):
    """Footer for pages with text on top: the garland low along the bottom edge,
    the little star at the right, the upper part left calm for logos and links."""
    W, H = w * SS, h * SS
    img = gradient(w // 8, h // 8, NIGHT_BOTTOM, NIGHT_TOP).resize((W, H), Image.BICUBIC).convert("RGBA")
    star_field(img, 70, 13, max_alpha=70)
    base = H * 0.86
    sx, sy = W * 0.88, H * 0.70

    def wave(x):
        return base + math.sin(x / W * 2 * math.pi * 2.5) * H * 0.045

    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    d = ImageDraw.Draw(img, "RGBA")
    d.line([(x, wave(x)) for x in range(0, W + 1, 8 * SS)], fill=GOLD + (60,), width=2 * SS)
    step = 46 * SS
    for k in range(int(W / step) + 1):
        x = k * step + step / 2
        if abs(x - sx) < 80 * SS:
            continue
        y = wave(x)
        warm = (255, 196, 92) if k % 3 else (255, 232, 180)
        gd.ellipse([x - 12 * SS, y - 12 * SS, x + 12 * SS, y + 12 * SS], fill=warm + (100,))
        d.ellipse([x - 3.5 * SS, y - 3.5 * SS, x + 3.5 * SS, y + 3.5 * SS], fill=warm + (255,))
    r = 34 * SS
    gd.ellipse([sx - r * 1.3, sy - r * 1.3, sx + r * 1.3, sy + r * 1.3], fill=GOLD + (60,))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(8 * SS)))
    d = ImageDraw.Draw(img, "RGBA")
    pts = []
    for i in range(12):
        a = math.pi / 2 + i * math.pi / 6
        rr = r if i % 2 == 0 else r * 0.58
        pts.append((sx + rr * math.cos(a), sy - rr * math.sin(a)))
    d.line(pts + pts[:1], fill=GOLD, width=int(2.8 * SS), joint="curve")
    for i in range(0, 12, 2):
        x, y = pts[i]
        d.ellipse([x - 3.8 * SS, y - 3.8 * SS, x + 3.8 * SS, y + 3.8 * SS], fill=TIP)
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def background(w=2400, h=1600):
    """Plain night sky for web page sections with text on top (no motif)."""
    W, H = w * SS // 2, h * SS // 2
    img = gradient(w // 8, h // 8, NIGHT_TOP, NIGHT_BOTTOM).resize((W, H), Image.BICUBIC).convert("RGBA")
    star_field(img, 150, 23, max_alpha=55)
    return img.resize((w, h), Image.LANCZOS).convert("RGB")


def stroke(d, pts, width, color):
    """A closed outline with an even, centred line and round joints."""
    w = max(1.0, width)
    for i in range(len(pts)):
        d.line([pts[i], pts[(i + 1) % len(pts)]], fill=color, width=int(round(w)))
    for x, y in pts:
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=color)


def hexagram(d, cx, cy, r, width, color, inner=True, tips=True, inner_color=GOLD_SOFT + (204,)):
    """The app icon's star: outer hexagram (r = tip radius), inner star, tip lights."""
    outer, inn = [], []
    for i in range(12):
        a = math.pi / 2 + i * math.pi / 6
        ro = r if i % 2 == 0 else r * 0.535
        outer.append((cx + ro * math.cos(a), cy - ro * math.sin(a)))
        ri = r * 0.36 if i % 2 == 0 else r * 0.2
        inn.append((cx + ri * math.cos(a + math.pi / 2), cy - ri * math.sin(a + math.pi / 2)))
    stroke(d, outer, width, color)
    if inner:
        stroke(d, inn, width * 0.6, inner_color)
    if tips:
        t = width * 0.95
        for i in range(0, 12, 2):
            x, y = outer[i]
            d.ellipse([x - t, y - t, x + t, y + t], fill=TIP)


def logo_mark(size=512):
    """The star alone on transparent, with a soft glow."""
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    cx = cy = s / 2
    r = s * 0.40
    glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cx - r * 0.7, cy - r * 0.7, cx + r * 0.7, cy + r * 0.7], fill=(255, 214, 120, 38))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(r * 0.3)))
    hexagram(ImageDraw.Draw(img, "RGBA"), cx, cy, r, r * 0.075, GOLD + (255,))
    return img.resize((size, size), Image.LANCZOS)


def logo_wide(w=1400, h=360):
    """Star + "Christmas Star" in Cormorant Garamond, soft gold, transparent (for dark backgrounds)."""
    W, H = w * SS, h * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    mark = logo_mark(h).resize((H, H), Image.LANCZOS)
    img.alpha_composite(mark, (0, 0))
    font = ImageFont.truetype(str(FONT), int(H * 0.46))
    d = ImageDraw.Draw(img, "RGBA")
    text = "Christmas Star"
    box = d.textbbox((0, 0), text, font=font)
    y = (H - (box[3] - box[1])) / 2 - box[1]
    d.text((H * 1.02, y), text, font=font, fill=GOLD_SOFT + (255,))
    right = int(H * 1.02 + box[2] + H * 0.06)
    return img.crop((0, 0, min(W, right), H)).resize((min(W, right) // SS, h), Image.LANCZOS)


def favicon(size=512):
    """Rounded night-navy square with a gold wireframe star. The line weight
    grows a little at small sizes so the outline stays readable at 16 px."""
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    bg = gradient(64, 64, (16, 22, 58), (36, 29, 82), diagonal=True).resize((s, s), Image.BICUBIC).convert("RGBA")
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=s * 0.22, fill=255)
    img.paste(bg, (0, 0), mask)
    cx, cy, r = s / 2, s / 2, s * 0.40
    pts = []
    for i in range(12):
        a = math.pi / 2 + i * math.pi / 6
        ro = r if i % 2 == 0 else r * 0.535
        pts.append((cx + ro * math.cos(a), cy - ro * math.sin(a)))
    weight = 0.055 if size >= 64 else 0.075 if size >= 32 else 0.10
    stroke(ImageDraw.Draw(img, "RGBA"), pts, s * weight, GOLD + (255,))
    return img.resize((size, size), Image.LANCZOS)


def main():
    icon().save(HERE / "icon-512.png")
    feature().save(HERE / "feature-graphic.png")
    banner().save(HERE / "christmas_star_banner.png")
    footer().save(HERE / "christmas_star_footer.png")
    footer_low().save(HERE / "christmas_star_footer_low.png")
    background().save(HERE / "christmas_star_background.png")
    logo_mark().save(HERE / "christmas_star_logo.png")
    logo_wide().save(HERE / "christmas_star_logo_wide.png")
    favicon().save(HERE / "christmas_star_favicon.png")
    # Each .ico size drawn on its own, with its own line weight.
    small = [favicon(n) for n in (16, 32, 48)]
    small[2].save(HERE / "christmas_star_favicon.ico", sizes=[(16, 16), (32, 32), (48, 48)],
                  append_images=small[:2])
    print("wrote icon-512.png, feature-graphic.png and the christmas_star_*.png page images")


if __name__ == "__main__":
    main()
