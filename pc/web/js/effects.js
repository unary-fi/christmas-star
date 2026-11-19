// Effect previews: a port of the app's effects/EffectPreviews.kt (same integer maths
// as the firmware's fx_xmas.c, fx_basic.c and fx_golden.c). Front face only.
import { StarLayout } from "../star_layout.js";

export const EFFECTS = [
  ["golden", "Golden Star", "Warm gold, gently shimmering"],
  ["snowfall", "Snowfall", "Flakes drifting down through the night"],
  ["candle", "Candlelight", "A soft flame in the heart of the star"],
  ["aurora", "Northern Lights", "Green and violet curtains"],
  ["candycane", "Candy Cane", "Red and white swirls"],
  ["treelights", "Tree Lights", "Old-fashioned coloured bulbs"],
  ["bethlehem", "Star of Bethlehem", "Radiant light from the centre"],
  ["fireworks", "Fireworks", "Bursts for the New Year"],
  ["frost", "Frost", "Ice crystals creeping in"],
  ["twinkle", "Twinkle", "Warm glow with sparkles"],
  ["comet", "Comets", "Chasing around the outline"],
  ["rainbow", "Rainbow", "All the colours, turning"],
].map(([id, title, subtitle]) => ({ id, title, subtitle }));

const N = StarLayout.FACE_LEDS;
const X = StarLayout.xi, Y = StarLayout.yi, R = StarLayout.r, A = StarLayout.angle;
const G = StarLayout.group, S = StarLayout.step;
const FLAKES = 14;

export const tri = (p) => { const q = p & 0xff; return q < 128 ? q * 2 : (255 - q) * 2; };
export const rgb = (r, g, b) => ((r & 255) << 16) | ((g & 255) << 8) | (b & 255);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const idiv = (a, b) => Math.trunc(a / b);
export function scale(c, level) {
  const l = clamp(level, 0, 255);
  return rgb(idiv(((c >> 16) & 255) * l + 127, 255), idiv(((c >> 8) & 255) * l + 127, 255),
             idiv((c & 255) * l + 127, 255));
}
const add = (a, b) => rgb(Math.min(255, ((a >> 16) & 255) + ((b >> 16) & 255)),
  Math.min(255, ((a >> 8) & 255) + ((b >> 8) & 255)), Math.min(255, (a & 255) + (b & 255)));
function mix(a, b, t) {
  const ch = (x, y) => x + idiv((y - x) * t, 255);
  return rgb(ch((a >> 16) & 255, (b >> 16) & 255), ch((a >> 8) & 255, (b >> 8) & 255), ch(a & 255, b & 255));
}
export function hsv(h, s, v) {
  const region = idiv(h, 43), rem = (h - region * 43) * 6;
  const p = idiv(v * (255 - s), 255), q = idiv(v * (255 - idiv(s * rem, 255)), 255);
  const t = idiv(v * (255 - idiv(s * (255 - rem), 255)), 255);
  switch (region) {
    case 0: return rgb(v, t, p);
    case 1: return rgb(q, v, p);
    case 2: return rgb(p, v, t);
    case 3: return rgb(p, q, v);
    case 4: return rgb(t, p, v);
    default: return rgb(v, p, q);
  }
}

export class EffectPreview {
  constructor(id) {
    this.id = id;
    this.frame = new Array(N).fill(0);
    this.rng = 0x12345678;
    this.heat = new Array(N).fill(0);
    this.flakes = Array.from({ length: FLAKES }, () => [0, 0, 0, 0]);
    this.snowLast = 0;
    this.bursts = Array.from({ length: 3 }, () => [0, 0, 0, 0, 0]);
    this.started = false;
  }

  rand() {
    let x = this.rng | 0;
    x ^= x << 13; x ^= x >>> 17; x ^= x << 5;
    this.rng = x | 0;
    return (x >>> 1);
  }

  render(t) {
    if (!this.started) { this.start(); this.started = true; }
    const fn = {
      golden: () => this.golden(t), snowfall: () => this.snowfall(t), candle: () => this.candle(),
      aurora: () => this.aurora(t), candycane: () => this.candy(t), treelights: () => this.tree(t),
      bethlehem: () => this.bethlehem(t), fireworks: () => this.fireworks(t), frost: () => this.frost(t),
      twinkle: () => this.twinkle(), comet: () => this.comet(t), rainbow: () => this.rainbow(t),
      flash: () => this.flashPlaceholder(t),
    }[this.id];
    if (fn) fn();
    return this.frame;
  }

  start() {
    if (this.id === "snowfall") this.flakes.forEach((f) => this.spawnFlake(f, true));
    if (this.id === "twinkle") this.frame.fill(rgb(60, 35, 6));
  }

  golden(t) {
    for (let i = 0; i < N; i++) {
      const shimmer = tri(idiv(t, 10) + A[i] + R[i]);
      this.frame[i] = scale(rgb(255, 165, 35), 170 + idiv(shimmer * 85, 255));
    }
  }

  spawnFlake(f, anywhere) {
    const side = this.rand() % 240 - 120;
    const back = anywhere ? this.rand() % 240 - 120 : -125;
    f[0] = idiv(256 * side * 16, 256);
    f[1] = idiv(-256 * back * 16, 256);
    f[2] = 6 + this.rand() % 10;
    f[3] = this.rand() & 0xff;
  }

  snowfall(t) {
    const dt = Math.max(0, Math.min(t - this.snowLast, 100));
    this.snowLast = t;
    this.frame.fill(rgb(2, 6, 28));
    for (const f of this.flakes) {
      const step = idiv(f[2] * dt, 8);
      const drift = idiv((tri(f[3] + idiv(t, 12)) - 128) * dt, 256);
      f[0] += idiv(256 * drift, 256);
      f[1] += idiv(-256 * step, 256);
      const x = idiv(f[0], 16), y = idiv(f[1], 16);
      if (Math.abs(x) > 130 || Math.abs(y) > 130) { this.spawnFlake(f, false); continue; }
      this.splat(x, y, rgb(210, 225, 255), 14);
    }
  }

  splat(x, y, c, radius) {
    const r2 = radius * radius;
    for (let i = 0; i < N; i++) {
      const dx = X[i] - x, dy = Y[i] - y, d2 = dx * dx + dy * dy;
      if (d2 < r2) this.frame[i] = add(this.frame[i], scale(c, 255 - idiv(d2 * 255, r2)));
    }
  }

  candle() {
    for (let i = 0; i < N; i++) {
      const target = 150 + this.rand() % 106;
      this.heat[i] = this.heat[i] + idiv(target - this.heat[i], 5);
      const glow = G[i] === 1 ? this.heat[i] : idiv(this.heat[i] * (140 - R[i]), 90);
      const c = mix(rgb(255, 70, 0), rgb(255, 170, 50), this.heat[i]);
      this.frame[i] = scale(c, clamp(glow, 20, 255));
    }
  }

  aurora(t) {
    for (let i = 0; i < N; i++) {
      const w1 = tri(X[i] * 2 + idiv(t, 24));
      const w2 = tri(Y[i] * 3 - X[i] + idiv(t, 37));
      const lum = idiv(w1 * w2, 255);
      const hue = (80 + idiv(tri(Y[i] + idiv(t, 50)) * 110, 255)) & 0xff;
      this.frame[i] = hsv(hue, 210, 15 + idiv(lum * 240, 255));
    }
  }

  candy(t) {
    for (let i = 0; i < N; i++) {
      const phase = ((A[i] + R[i] * 2 - idiv(t, 10)) * 4) & 0xff;
      const s = clamp((tri(phase) - 96) * 4, 0, 255);
      this.frame[i] = mix(rgb(230, 10, 20), rgb(255, 235, 220), s);
    }
  }

  tree(t) {
    const bulbs = [rgb(230, 20, 10), rgb(20, 200, 40), rgb(255, 160, 20), rgb(30, 60, 255)];
    for (let i = 0; i < N; i++) {
      if (G[i] === 0) {
        const colour = S[i] % 4;
        const b = tri(idiv(t, 7 + colour) + colour * 64);
        this.frame[i] = scale(bulbs[colour], 50 + idiv(b * 205, 255));
      } else {
        const b = tri(idiv(t, 20) + A[i]);
        this.frame[i] = scale(rgb(255, 200, 120), 60 + idiv(b, 3));
      }
    }
  }

  bethlehem(t) {
    for (let i = 0; i < N; i++) {
      const wave = tri(R[i] * 3 - idiv(t, 5));
      const tip = G[i] === 0 && S[i] % 8 === 4;
      const core = R[i] < 40 ? 120 : 0;
      const lum = 40 + idiv(wave * 160, 255) + core + (tip ? 60 : 0);
      const c = mix(rgb(255, 150, 30), rgb(255, 245, 220), clamp(255 - R[i] * 2, 0, 255));
      this.frame[i] = scale(c, clamp(lum, 0, 255));
    }
  }

  fireworks(t) {
    this.fade(200);
    for (const bu of this.bursts) {
      const age = t - bu[3];
      if (bu[4] === 0 || age > bu[4]) {
        if (this.rand() % 40 === 0) {
          bu[0] = this.rand() % 120 - 60;
          bu[1] = this.rand() % 120 - 60;
          bu[2] = this.rand() & 0xff;
          bu[3] = t;
          bu[4] = 900 + this.rand() % 600;
        }
        continue;
      }
      const radius = idiv(age * 70, bu[4]);
      const lum = 255 - idiv(age * 255, bu[4]);
      for (let i = 0; i < N; i++) {
        const dx = X[i] - bu[0], dy = Y[i] - bu[1];
        const d = Math.trunc(Math.sqrt(dx * dx + dy * dy));
        if (Math.abs(d - radius) < 9) {
          const sat = this.rand() % 4 === 0 ? 60 : 230;
          this.frame[i] = add(this.frame[i], hsv(bu[2], sat, lum));
        }
      }
    }
  }

  frost(t) {
    const cycle = t % 8000;
    const front = cycle < 4000 ? 120 - idiv(cycle * 120, 4000) : cycle < 6000 ? 0 : idiv((cycle - 6000) * 120, 2000);
    this.fade(225);
    for (let i = 0; i < N; i++) {
      let base = rgb(4, 10, 30);
      if (R[i] >= front) {
        const shimmer = tri(idiv(t, 9) + A[i] * 3 + R[i]);
        base = mix(rgb(40, 110, 220), rgb(170, 220, 255), shimmer);
      }
      let c = add(this.frame[i], scale(base, 60));
      if (R[i] >= front && this.rand() % 90 === 0) c = rgb(255, 255, 255);
      this.frame[i] = c;
    }
  }

  twinkle() {
    const base = [60, 35, 6];
    for (let i = 0; i < N; i++) {
      const c = this.frame[i];
      let r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
      r -= idiv(r - base[0], 12); g -= idiv(g - base[1], 12); b -= idiv(b - base[2], 12);
      this.frame[i] = rgb(r, g, b);
    }
    for (let k = 0; k < 2; k++) {
      const r = this.rand();
      if ((r & 0x7) === 0) this.frame[(r >>> 8) % N] = rgb(255, 240, 200);
    }
  }

  comet(t) {
    const head = idiv(t * 48, 1600) % 48;
    const breath = tri(idiv(t, 12));
    for (let i = 0; i < N; i++) {
      if (G[i] === 0) {
        const d1 = (head + 48 - S[i]) % 48, d2 = (head + 24 + 48 - S[i]) % 48;
        const d = Math.min(d1, d2);
        this.frame[i] = scale(rgb(200, 220, 255), d < 8 ? 255 - d * 32 : 0);
      } else {
        this.frame[i] = scale(rgb(255, 150, 20), 40 + idiv(breath * 3, 4));
      }
    }
  }

  rainbow(t) {
    const rot = idiv(t, 16);
    for (let i = 0; i < N; i++) this.frame[i] = hsv((A[i] + idiv(R[i], 2) - rot) & 0xff, 255, 255);
  }

  flashPlaceholder(t) {
    for (let i = 0; i < N; i++) this.frame[i] = hsv((Y[i] + idiv(t, 20)) & 0xff, 120, 170);
  }

  fade(keep) {
    for (let i = 0; i < N; i++) this.frame[i] = scale(this.frame[i], keep);
  }
}
