// Copyright (c) 2024-2026 Tuomas Terho
// SPDX-License-Identifier: MIT
//
// The star's protocol pieces (no I/O): live frames, .csfx files, XMODEM-1K,
// signed images, the shell's reply lines and the login MAC. They match the
// firmware (sw/src/transfer/stream_frame.h, sw/src/storage/effect_format.h),
// the Android app (protocol/*.kt) and pc/star_protocol.py.

export const FACE_LEDS = 66;
export const LED_COUNT = 132;
export const ACK = 0x06, NAK = 0x15, CAN = 0x18, EOT = 0x04, STX = 0x02;
const FLAG_BACK_COPY = 0x01;
const FLAG_END = 0x80;
const IMAGE_MAGIC = 0x96f3b83d;

const enc = new TextEncoder();

export function crc16Xmodem(data, crc = 0) {
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1) & 0xffff;
  }
  return crc;
}

let crcTable = null;
export function crc32(data) {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c >>> 0;
    }
  }
  let c = 0xffffffff;
  for (const b of data) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export function concat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}

export const rgb565 = (c) => (((c >> 16) & 0xff) >> 3) << 11 | (((c >> 8) & 0xff) >> 2) << 5 | ((c & 0xff) >> 3);

function rgb888(v) {
  const r = (v >> 11) & 0x1f, g = (v >> 5) & 0x3f, b = v & 0x1f;
  return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

/** A5 5A | flags | n | n x RGB565 LE | CRC-16/XMODEM BE (over flags..payload). */
export function streamFrame(front = null, end = false) {
  const colors = end ? [] : Array.from(front).slice(0, FACE_LEDS);
  const body = new Uint8Array(2 + 2 * colors.length);
  body[0] = end ? FLAG_END : FLAG_BACK_COPY;
  body[1] = colors.length;
  colors.forEach((c, i) => { const v = rgb565(c); body[2 + 2 * i] = v & 0xff; body[3 + 2 * i] = v >> 8; });
  const crc = crc16Xmodem(body);
  return concat(new Uint8Array([0xa5, 0x5a]), body, new Uint8Array([crc >> 8, crc & 0xff]));
}

/** Stream frames in buf -> [{flags, colors}]; bad or partial frames are skipped. */
export function decodeFrames(buf) {
  const out = [];
  let i = 0;
  while (i + 4 <= buf.length) {
    if (buf[i] !== 0xa5 || buf[i + 1] !== 0x5a) { i++; continue; }
    const n = buf[i + 3];
    const end = i + 4 + 2 * n + 2;
    if (end > buf.length) break;
    const body = buf.subarray(i + 2, end - 2);
    if (((buf[end - 2] << 8) | buf[end - 1]) === crc16Xmodem(body)) {
      const colors = [];
      for (let k = 0; k < n; k++) colors.push(rgb888(buf[i + 4 + 2 * k] | (buf[i + 5 + 2 * k] << 8)));
      out.push({ flags: buf[i + 2], colors });
      i = end;
    } else {
      i += 2;
    }
  }
  return out;
}

/** .csfx file from [{front: 66 colours 0xRRGGBB, ms}]; the back copies its front partner. */
export function csfx(name, frames, partner, loop = true) {
  const data = new Uint8Array(frames.length * (2 + 3 * LED_COUNT));
  let at = 0;
  for (const { front, ms } of frames) {
    const d = Math.max(1, Math.min(65535, Math.round(ms)));
    data[at++] = d & 0xff;
    data[at++] = d >> 8;
    for (let i = 0; i < LED_COUNT; i++) {
      const c = front[i < FACE_LEDS ? i : partner[i]];
      data[at++] = (c >> 16) & 0xff; data[at++] = (c >> 8) & 0xff; data[at++] = c & 0xff;
    }
  }
  const header = new Uint8Array(32);
  const v = new DataView(header.buffer);
  header.set(enc.encode("CSFX"), 0);
  header[4] = 1;
  header[5] = LED_COUNT;
  v.setUint16(6, loop ? 1 : 0, true);
  header.set(enc.encode(name).slice(0, 16), 8);
  v.setUint32(24, frames.length, true);
  v.setUint32(28, crc32(data), true);
  return concat(header, data);
}

/** Version "major.minor.rev[+build]" of a signed MCUboot image, or null. */
export function imageVersion(data) {
  if (data.length < 32) return null;
  const v = new DataView(data.buffer, data.byteOffset, data.byteLength);
  if (v.getUint32(0, true) !== IMAGE_MAGIC) return null;
  const build = v.getUint32(24, true);
  return `${data[20]}.${data[21]}.${v.getUint16(22, true)}` + (build ? `+${build}` : "");
}

/** XMODEM-1K (CRC) blocks: STX | n | ~n | 1024 bytes (padded 0x1A) | CRC BE. */
export function xmodemBlocks(data) {
  const blocks = [];
  for (let k = 0, i = 0; i < data.length; k++, i += 1024) {
    const n = (k + 1) & 0xff;
    const payload = new Uint8Array(1024).fill(0x1a);
    payload.set(data.subarray(i, i + 1024));
    const crc = crc16Xmodem(payload);
    blocks.push(concat(new Uint8Array([STX, n, 0xff - n]), payload, new Uint8Array([crc >> 8, crc & 0xff])));
  }
  return blocks;
}

function kv(text) {
  const out = {};
  for (const item of text.split(/\s+/)) {
    const i = item.indexOf("=");
    if (i > 0) out[item.slice(0, i)] = item.slice(i + 1);
  }
  return out;
}

/** 'fx <id> <flags> <title>|<subtitle>' lines -> [{id, title, subtitle, tilt, twoFaces}]. */
export function parseEffects(text) {
  const out = [];
  for (const raw of text.replace(/\r/g, "").split("\n")) {
    const line = raw.trim();
    const m = line.match(/^fx (\S+) (\S+) (.*)$/);
    if (!m) continue;
    const bar = m[3].indexOf("|");
    out.push({
      id: m[1],
      title: bar < 0 ? m[3] : m[3].slice(0, bar),
      subtitle: bar < 0 ? "" : m[3].slice(bar + 1),
      tilt: m[2].includes("t"),
      twoFaces: m[2].includes("2"),
    });
  }
  return out;
}

/** 'state effect=.. flash=<name> frames=N ... name=<name>' (the names may hold spaces). */
export function parseState(text) {
  const line = text.replace(/\r/g, "").split("\n").find((l) => l.startsWith("state "));
  if (!line) return {};
  let head = line.slice(6);
  let name = "";
  const n = head.indexOf(" name=");
  if (n >= 0) { name = head.slice(n + 6); head = head.slice(0, n); }
  let flash = "";
  const f = head.indexOf(" flash=");
  if (f >= 0) {
    const rest = head.slice(f + 7);
    const fr = rest.indexOf(" frames=");
    flash = fr >= 0 ? rest.slice(0, fr) : rest;
    head = head.slice(0, f) + (fr >= 0 ? rest.slice(fr) : "");
  }
  const out = kv(head);
  out.flash = flash === "-" ? "" : flash;
  out.name = name.trim();
  return out;
}

/** "ok proto=.. ... nonce=<hex> name=<name>" -> object, or null. */
export function parseHello(line) {
  line = line.trim();
  if (!line.startsWith("ok ")) return null;
  let head = line.slice(3);
  let name = "";
  const n = head.indexOf(" name=");
  if (n >= 0) { name = head.slice(n + 6); head = head.slice(0, n); }
  const out = kv(head);
  out.name = name;
  out.proto = parseInt(out.proto || "1", 10);
  return out;
}

export const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");

/** 16-byte key from 32 hex digits (spaces and dashes allowed), or null. */
export function parseKey(text) {
  const h = (text || "").replace(/[\s:-]/g, "");
  if (!/^[0-9a-fA-F]{32}$/.test(h)) return null;
  const key = new Uint8Array(16);
  for (let i = 0; i < 16; i++) key[i] = parseInt(h.slice(2 * i, 2 * i + 2), 16);
  return key.some((b) => b !== 0) ? key : null;
}

/** The login answer: hex HMAC-SHA256(key, "auth" + nonce). */
export async function authMac(key, nonceHex) {
  const nonce = new Uint8Array((nonceHex.match(/../g) || []).map((x) => parseInt(x, 16)));
  const k = await crypto.subtle.importKey("raw", key, { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const mac = await crypto.subtle.sign("HMAC", k, concat(enc.encode("auth"), nonce));
  return hex(new Uint8Array(mac));
}
