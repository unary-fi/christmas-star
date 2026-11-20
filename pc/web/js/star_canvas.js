// Star drawing, ported from the app (star/StarCanvas.kt, StarGeometry.kt, NightSky.kt).
import { StarLayout } from "../star_layout.js";

export const GOLD = "#F2C14E";
export const EXTENT_MM = 132;

/** Maps star millimetres (origin at the centre, y up) to canvas pixels. */
export class StarGeometry {
  constructor(w, h, extent = EXTENT_MM) {
    this.w = w;
    this.h = h;
    this.cx = w / 2;
    this.cy = h / 2;
    this.pxPerMm = Math.min(w, h) / (2 * extent);
  }
  toScreen(x, y) {
    return [this.cx + x * this.pxPerMm, this.cy - y * this.pxPerMm];
  }
  led(i) {
    return this.toScreen(StarLayout.x[i], StarLayout.y[i]);
  }
  /** Front LED index under a canvas point (or -1). */
  hit(px, py, radiusMm = 5) {
    let best = -1, bestD = (radiusMm * this.pxPerMm) ** 2;
    for (let i = 0; i < StarLayout.FACE_LEDS; i++) {
      const [x, y] = this.led(i);
      const d = (x - px) ** 2 + (y - py) ** 2;
      if (d < bestD) { best = i; bestD = d; }
    }
    return best;
  }
}

/** Size a canvas to its CSS box at device resolution; returns [ctx, w, h] in CSS px. */
export function fitCanvas(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const r = canvas.getBoundingClientRect();
  const w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr);
    canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return [ctx, w, h];
}

const hex2 = (v) => v.toString(16).padStart(2, "0");
export const rgbCss = (c, a = 1) =>
  a >= 1 ? `#${hex2((c >> 16) & 255)}${hex2((c >> 8) & 255)}${hex2(c & 255)}`
         : `rgba(${(c >> 16) & 255},${(c >> 8) & 255},${c & 255},${a})`;

function lighten(c) {
  const ch = (v) => Math.round(v + (255 - v) * 0.35);
  return (ch((c >> 16) & 255) << 16) | (ch((c >> 8) & 255) << 8) | ch(c & 255);
}

// A soft glow per colour, drawn once and reused: building a radial gradient for
// every LED on every frame kept a CPU core busy. Colours are kept to 5 bits per
// channel, which the eye cannot tell apart in a glow.
const glowCache = new Map();
const SPRITE = 64;
function glowSprite(c) {
  const key = (((c >> 19) & 31) << 10) | (((c >> 11) & 31) << 5) | ((c >> 3) & 31);
  let s = glowCache.get(key);
  if (!s) {
    if (glowCache.size > 2048) glowCache.clear();
    const q = (((key >> 10) & 31) << 19) | (((key >> 5) & 31) << 11) | ((key & 31) << 3);
    s = typeof OffscreenCanvas !== "undefined" ? new OffscreenCanvas(SPRITE, SPRITE) : Object.assign(document.createElement("canvas"), { width: SPRITE, height: SPRITE });
    const sc = s.getContext("2d");
    const r = SPRITE / 2;
    const grad = sc.createRadialGradient(r, r, 0, r, r, r);
    grad.addColorStop(0, rgbCss(q, 0.55));
    grad.addColorStop(0.5, rgbCss(q, 0.18));
    grad.addColorStop(1, rgbCss(q, 0));
    sc.fillStyle = grad;
    sc.fillRect(0, 0, SPRITE, SPRITE);
    glowCache.set(key, s);
  }
  return s;
}

/**
 * The star as on the PCB: board outline in thin gold lines and softly glowing LEDs.
 * colors: 66 front colours (0xRRGGBB) or null; opts: {outline, glow, marks:Set, extent}
 */
export function drawStar(ctx, w, h, colors, opts = {}) {
  const g = new StarGeometry(w, h, opts.extent || EXTENT_MM);
  const outline = opts.outline ?? 0.7, glow = opts.glow ?? 1;
  const line = Math.max(1, g.pxPerMm * 0.45);
  const e = StarLayout.edges;
  ctx.save();
  if (opts.scaleX !== undefined) {
    // Seen turned around the vertical axis: squeezed (negative = mirrored).
    ctx.translate(w / 2, 0);
    ctx.scale(Math.sign(opts.scaleX) * Math.max(0.02, Math.abs(opts.scaleX)), 1);
    ctx.translate(-w / 2, 0);
  }
  ctx.lineCap = "round";
  ctx.lineWidth = line;
  ctx.strokeStyle = `rgba(242,193,78,${outline})`;
  ctx.beginPath();
  for (let k = 0; k < e.length; k += 4) {
    const [x1, y1] = g.toScreen(e[k], e[k + 1]);
    const [x2, y2] = g.toScreen(e[k + 2], e[k + 3]);
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
  }
  ctx.stroke();

  const dot = g.pxPerMm * 2.2;
  ctx.globalCompositeOperation = "lighter";
  ctx.globalAlpha = Math.min(1, glow);
  for (let i = 0; i < StarLayout.FACE_LEDS; i++) {
    const c = colors ? colors[i] : 0;
    const lum = (0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255)) / 255;
    if (lum > 0.02 && glow > 0) {
      const [x, y] = g.led(i);
      const radius = dot * (2.8 + 2.2 * lum) * glow;
      ctx.drawImage(glowSprite(c), x - radius, y - radius, 2 * radius, 2 * radius);
    }
  }
  ctx.globalAlpha = 1;
  ctx.globalCompositeOperation = "source-over";
  for (let i = 0; i < StarLayout.FACE_LEDS; i++) {
    const c = colors ? colors[i] : 0;
    const lum = (0.299 * ((c >> 16) & 255) + 0.587 * ((c >> 8) & 255) + 0.114 * (c & 255)) / 255;
    const [x, y] = g.led(i);
    ctx.beginPath();
    if (lum > 0.02) {
      ctx.fillStyle = rgbCss(lighten(c));
      ctx.arc(x, y, dot, 0, Math.PI * 2);
      ctx.fill();
    } else {
      ctx.strokeStyle = "rgba(242,193,78,0.25)";
      ctx.lineWidth = line;
      ctx.arc(x, y, dot * 0.8, 0, Math.PI * 2);
      ctx.stroke();
    }
    if (opts.marks && opts.marks.has(i)) {
      ctx.strokeStyle = "#E5484D";
      ctx.lineWidth = Math.max(1.5, line * 1.4);
      const m = dot * 1.6;
      ctx.beginPath();
      ctx.moveTo(x - m, y - m); ctx.lineTo(x + m, y + m);
      ctx.moveTo(x + m, y - m); ctx.lineTo(x - m, y + m);
      ctx.stroke();
    }
  }
  ctx.restore();
  return g;
}

/** A sparse, slowly twinkling star field behind everything (NightSky.kt). */
export function startSky(canvas) {
  let seed = 2024;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
  const stars = Array.from({ length: 110 }, () => ({
    x: rnd(), y: rnd(), r: 0.5 + rnd() * rnd() * 1.3, phase: rnd() * Math.PI * 2,
    speed: 0.4 + rnd() * 0.8, bright: 0.15 + rnd() * 0.45,
  }));
  // The twinkle is slow: ten frames a second look the same and cost far less
  // than redrawing the whole window at the display's rate.
  let last = -1e9;
  const frame = (t) => {
    requestAnimationFrame(frame);
    if (t - last < 100) return;
    last = t;
    const [ctx, w, h] = fitCanvas(canvas);
    ctx.clearRect(0, 0, w, h);
    const time = t / 1000 * (2 * Math.PI * 10 / 60);
    for (const s of stars) {
      const tw = 0.55 + 0.45 * Math.sin(time * s.speed + s.phase);
      ctx.fillStyle = `rgba(255,255,255,${Math.min(1, s.bright * tw)})`;
      ctx.beginPath();
      ctx.arc(s.x * w, s.y * h, s.r, 0, Math.PI * 2);
      ctx.fill();
    }
  };
  requestAnimationFrame(frame);
}
