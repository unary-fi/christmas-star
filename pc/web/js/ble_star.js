// Copyright (c) 2024-2026 Tuomas Terho
// SPDX-License-Identifier: MIT
//
// One star over Web Bluetooth: the BM78 Transparent UART link, the shell
// session (hello / auth / pair), live frames, XMODEM uploads, preview clips,
// firmware updates and renaming. Everything runs in the page.

import * as sp from "./protocol.js";

export const SERVICE = "49535343-fe7d-4ae5-8fa9-9fafd205e455";
const TX_CHAR = "49535343-1e4d-4bd9-ba61-23c647249616"; // star -> page (notify)
const RX_CHAR = "49535343-8841-43f4-a8d4-ecbe34729bb3"; // page -> star (write)
const PROMPT = new TextEncoder().encode("star:~$ ");
const NL = new Uint8Array([0x0a]);
const LIVE_WINDOW = 2;
const LIVE_KEEPALIVE_MS = 1000;
const LIVE_ACK_TIMEOUT_MS = 700;

const enc = new TextEncoder();
const dec = new TextDecoder("utf-8", { fatal: false });
const latin1 = new TextDecoder("latin1");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function indexOf(buf, token) {
  outer: for (let i = 0; i + token.length <= buf.length; i++) {
    for (let k = 0; k < token.length; k++) if (buf[i + k] !== token[k]) continue outer;
    return i;
  }
  return -1;
}

/** Bytes received from the star, with an awaitable "more arrived". */
class Rx {
  constructor() { this.buf = new Uint8Array(0); this.waiters = []; }
  push(bytes) {
    this.buf = sp.concat(this.buf, bytes);
    const w = this.waiters;
    this.waiters = [];
    w.forEach((f) => f());
  }
  drain() { const b = this.buf; this.buf = new Uint8Array(0); return b; }
  wait(ms) {
    return new Promise((resolve) => {
      const done = () => { clearTimeout(t); resolve(true); };
      const t = setTimeout(() => { this.waiters = this.waiters.filter((f) => f !== done); resolve(false); }, Math.max(1, ms));
      this.waiters.push(done);
    });
  }
}

/** One operation at a time on a star's link. */
class Lock {
  constructor() { this.tail = Promise.resolve(); this.busy = 0; }
  get locked() { return this.busy > 0; }
  async run(fn) {
    const prev = this.tail;
    let release;
    this.tail = new Promise((r) => (release = r));
    this.busy++;
    try {
      await prev;
      return await fn();
    } finally {
      this.busy--;
      release();
    }
  }
}

export class Star {
  /**
   * @param {BluetoothDevice} device
   * @param {{changed: Function, keys: Function, pcKey: Function}} host
   */
  constructor(device, host) {
    this.device = device;
    this.host = host;
    this.address = device.id;
    this.name = device.name || "Star";
    this.rssi = null;
    this.status = "nearby"; // nearby, connecting, ready, locked, renaming, updating, error
    this.error = "";
    this.info = {};
    this.state = {};
    this.led = "";
    this.effects = [];
    this.liveFrame = null;
    this.liveTask = null;
    this.liveFps = 0;
    this.rx = new Rx();
    this.lock = new Lock();
    this.rxChar = null;
    this.chunk = 128;
    this.withResponse = false;
    device.addEventListener("gattserverdisconnected", () => {
      this.rxChar = null;
      if (this.status !== "renaming" && this.status !== "updating") {
        this.status = "nearby";
        this.info = {};
        this.liveFrame = null;
        this.changed();
      }
    });
    device.addEventListener("advertisementreceived", (e) => {
      if (typeof e.rssi === "number") { this.rssi = e.rssi; this.changed(); }
    });
  }

  toJSON() {
    return {
      address: this.address, name: this.name, rssi: this.rssi, status: this.status,
      error: this.error, info: this.info, state: this.state, led: this.led,
      liveFps: Math.round(this.liveFps * 10) / 10, effects: this.effects,
    };
  }

  changed() { this.host.changed(); }

  // ---- link ------------------------------------------------------------------

  get connected() { return this.rxChar !== null && this.device.gatt.connected; }

  async open() {
    let last = null;
    for (let attempt = 0; attempt < 3; attempt++) {
      try {
        const server = await this.device.gatt.connect();
        const service = await server.getPrimaryService(SERVICE);
        const tx = await service.getCharacteristic(TX_CHAR);
        this.rxChar = await service.getCharacteristic(RX_CHAR);
        tx.addEventListener("characteristicvaluechanged", (e) => {
          const v = e.target.value;
          this.rx.push(new Uint8Array(v.buffer, v.byteOffset, v.byteLength).slice());
        });
        await tx.startNotifications();
        return;
      } catch (e) {
        last = e;
        try { this.device.gatt.disconnect(); } catch { /* ignore */ }
        await sleep(1500);
      }
    }
    throw new Error(`cannot connect: ${last?.message || last}`);
  }

  close() {
    this.liveFrame = null;
    try { this.device.gatt.disconnect(); } catch { /* ignore */ }
    this.rxChar = null;
  }

  async write(data) {
    for (let i = 0; i < data.length; i += this.chunk) {
      const part = data.subarray(i, i + this.chunk);
      if (this.withResponse) {
        await this.rxChar.writeValueWithResponse(part);
        continue;
      }
      try {
        await this.rxChar.writeValueWithoutResponse(part);
      } catch (e) {
        // A link that does not take long writes: small acknowledged chunks.
        if (!this.connected) throw e;
        this.chunk = 20;
        this.withResponse = true;
        for (let k = 0; k < part.length; k += 20) await this.rxChar.writeValueWithResponse(part.subarray(k, k + 20));
      }
    }
  }

  drain() { return this.rx.drain(); }

  async readUntil(token, timeoutMs) {
    const end = performance.now() + timeoutMs;
    while (indexOf(this.rx.buf, token) < 0 && performance.now() < end) {
      if (!(await this.rx.wait(end - performance.now()))) break;
    }
    const i = indexOf(this.rx.buf, token);
    const cut = i < 0 ? this.rx.buf.length : i + token.length;
    const out = this.rx.buf.slice(0, cut);
    this.rx.buf = this.rx.buf.slice(cut);
    return out;
  }

  async readByte(timeoutMs) {
    if (!this.rx.buf.length) await this.rx.wait(timeoutMs);
    if (!this.rx.buf.length) return null;
    const b = this.rx.buf[0];
    this.rx.buf = this.rx.buf.slice(1);
    return b;
  }

  async line(timeoutMs = 3000) {
    const raw = dec.decode(await this.readUntil(NL, timeoutMs));
    const lines = raw.replace(/\r/g, "").split("\n").map((l) => l.trim()).filter((l) => l && !l.startsWith("star:~$"));
    return lines[0] || "";
  }

  /** Run a shell command (hold the lock); its output without the prompt and echo. */
  async command(text, timeoutMs = 4000) {
    this.drain();
    await this.write(enc.encode(text + "\r\n"));
    const out = dec.decode(await this.readUntil(PROMPT, timeoutMs));
    let lines = out.replace(/\r/g, "").split("\n").map((l) => l.replace(/\s+$/, ""))
      .filter((l) => l.trim() && !l.startsWith("star:~$"));
    if (lines.length && lines[0].trim() === text) lines = lines.slice(1);
    return lines.join("\n");
  }

  // ---- session -----------------------------------------------------------------

  async hello() {
    this.drain();
    await this.write(enc.encode("app hello\r\n"));
    for (let i = 0; i < 3; i++) {
      const info = sp.parseHello(await this.line());
      if (info) return info;
    }
    throw new Error("the star does not answer");
  }

  async login(pair) {
    let info = await this.hello();
    if (info.proto < 2) return { ...info, access: "open" };
    const keys = this.host.keys(pair);
    for (let i = 0; i < keys.length; i++) {
      if (i) info = await this.hello();
      await this.write(enc.encode(`app auth ${await sp.authMac(keys[i], info.nonce)}\r\n`));
      const reply = await this.line();
      if (reply.startsWith("ok access=")) return { ...info, access: reply.split("=")[1] };
    }
    if (pair && info.pairing === "open") {
      await this.write(enc.encode(`app pair ${sp.hex(this.host.pcKey(true))}\r\n`));
      if ((await this.line()).startsWith("ok paired")) return { ...info, access: "owner", owner: "paired", pairing: "closed" };
    }
    return { ...info, access: "none" };
  }

  async connect(pair = false) {
    await this.lock.run(async () => {
      this.status = "connecting";
      this.error = "";
      this.changed();
      try {
        if (!this.connected) await this.open();
        this.info = await this.login(pair);
        if (this.info.name) this.name = this.info.name;
        if (this.info.access === "none") {
          this.status = "locked";
        } else {
          this.status = "ready";
          await this.refreshLocked();
          if ((this.info.proto || 1) >= 3) this.effects = sp.parseEffects(await this.command("app effects"));
        }
      } catch (e) {
        this.status = "error";
        this.error = e.message || String(e);
        this.close();
      }
    });
    this.changed();
  }

  async refreshLocked() {
    this.state = sp.parseState(await this.command("app state"));
    if (this.state.name) this.name = this.state.name;
    this.led = await this.command("led status");
  }

  async refresh() {
    if (this.status !== "ready" || this.lock.locked || this.liveFrame !== null) return;
    await this.lock.run(async () => {
      try { await this.refreshLocked(); } catch (e) { this.error = e.message; }
    });
    this.changed();
  }

  async run(text, timeoutMs = 4000, refresh = true) {
    await this.stopLive();
    const out = await this.lock.run(async () => {
      const o = await this.command(text, timeoutMs);
      if (refresh) { try { await this.refreshLocked(); } catch { /* ignore */ } }
      return o;
    });
    if (refresh) this.changed();
    return out;
  }

  // ---- live frames ----------------------------------------------------------------

  setLive(front) {
    this.liveFrame = sp.streamFrame(front);
    if (!this.liveTask) this.liveTask = this.pump().finally(() => { this.liveTask = null; });
  }

  async stopLive() {
    this.liveFrame = null;
    if (this.liveTask) { try { await this.liveTask; } catch { /* ignore */ } }
  }

  async pump() {
    await this.lock.run(async () => {
      this.drain();
      await this.write(enc.encode("stream\r\n"));
      if ((await this.waitAck(2000)) === null) { this.liveFrame = null; return; }
      let inflight = 0, lastSent = null, lastAt = 0, acks = 0, window = performance.now();
      while (this.liveFrame !== null && this.connected) {
        const now = performance.now();
        const frame = this.liveFrame;
        if (frame !== lastSent && inflight < LIVE_WINDOW) {
          await this.write(frame);
          lastSent = frame; lastAt = now; inflight++;
        } else if (lastSent && inflight === 0 && now - lastAt > LIVE_KEEPALIVE_MS) {
          await this.write(lastSent); // the star leaves stream mode after 2 s
          lastAt = now; inflight++;
        }
        const b = await this.readByte(15);
        if (b === sp.ACK) { inflight = Math.max(0, inflight - 1); acks++; }
        else if (b === sp.NAK) inflight = Math.max(0, inflight - 1);
        if (inflight && now - lastAt > LIVE_ACK_TIMEOUT_MS) inflight = 0;
        if (now - window >= 1000) { this.liveFps = (acks * 1000) / (now - window); acks = 0; window = now; this.changed(); }
      }
      if (this.connected) {
        await this.write(sp.streamFrame(null, true));
        await this.waitAck(800);
        await sleep(100);
        this.drain();
      }
      this.liveFps = 0;
    });
    this.changed();
  }

  async waitAck(timeoutMs) {
    const end = performance.now() + timeoutMs;
    while (performance.now() < end) {
      if ((await this.readByte(end - performance.now())) === sp.ACK) return sp.ACK;
    }
    return null;
  }

  // ---- uploads ----------------------------------------------------------------------

  async upload(blob, play, progress, command = "effect upload") {
    await this.stopLive();
    await this.lock.run(async () => {
      this.drain();
      await this.write(enc.encode(command + "\r\n"));
      const reply = dec.decode(await this.readUntil(enc.encode("now..."), 8000));
      if (!reply.includes("XMODEM")) throw new Error(reply.trim() || "upload refused");
      const until = performance.now() + 10000;
      while (performance.now() < until && (await this.readByte(1000)) !== 0x43) { /* wait for 'C' */ }
      const blocks = sp.xmodemBlocks(blob);
      for (let k = 0; k < blocks.length; k++) {
        let ok = false;
        for (let tries = 0; tries < 10 && !ok; tries++) {
          await this.write(blocks[k]);
          let b = await this.readByte(5000);
          while (b === 0x43) b = await this.readByte(5000);
          if (b === sp.ACK) ok = true;
          else if (b === sp.CAN) throw new Error("the star cancelled the upload");
        }
        if (!ok) {
          await this.write(new Uint8Array([sp.CAN, sp.CAN]));
          throw new Error("too many retries");
        }
        progress(Math.min((k + 1) * 1024, blob.length), blob.length);
      }
      for (let i = 0; i < 5; i++) {
        await this.write(new Uint8Array([sp.EOT]));
        if ((await this.readByte(3000)) === sp.ACK) break;
      }
      const result = dec.decode(await this.readUntil(PROMPT, 15000));
      if (!result.includes("upload OK")) throw new Error(result.trim() || "upload failed");
      if (play) await this.command("effect play flash");
      if (command === "effect upload") await this.refreshLocked();
    });
    this.changed();
  }

  /** A preview clip rendered by the star: {step, frames: [[0xRRGGBB] x 66 or 132]}. */
  async preview(effectId) {
    await this.stopLive();
    const data = await this.lock.run(async () => {
      this.drain();
      await this.write(enc.encode(`app preview ${effectId}\r\n`));
      const d = await this.readUntil(enc.encode("preview end"), 40000);
      return sp.concat(d, await this.readUntil(PROMPT, 2000));
    });
    const head = latin1.decode(data);
    const m = head.match(/ step=(\d+)/);
    const frames = sp.decodeFrames(data).map((f) => f.colors);
    if (!frames.length) throw new Error(head.trim().slice(-80) || "no preview");
    return { step: m ? Number(m[1]) : 100, frames };
  }

  /** Upload a signed image, restart into it and reconnect. Returns the new version. */
  async updateFirmware(blob, permanent, progress) {
    await this.upload(blob, false, progress, "fw upload");
    await this.lock.run(async () => {
      const out = await this.command(permanent ? "fw apply permanent" : "fw apply");
      if (!out.includes("ok restarting")) throw new Error(out || "the star refused the update");
      this.status = "updating";
      this.changed();
      await sleep(500);
      this.close();
    });
    // MCUboot checks the signature and swaps the slots; then the module comes back.
    await sleep(15000);
    for (let i = 0; i < 8; i++) {
      await this.connect();
      if (this.status === "ready" || this.status === "locked") return this.info.fw || "?";
      await sleep(5000);
    }
    throw new Error("the star did not come back after the update");
  }

  async rename(name) {
    await this.stopLive();
    await this.lock.run(async () => {
      const out = await this.command(`star name ${name}`);
      if (!out.includes("ok renaming")) throw new Error(out || "rename refused");
      this.status = "renaming";
      this.changed();
      await sleep(500);
      this.close();
    });
    // The module restarts with its new name; reconnect to the same device.
    await sleep(5000);
    for (let i = 0; i < 5; i++) {
      await this.connect();
      if (this.status === "ready" || this.status === "locked") return;
      await sleep(3000);
    }
  }
}
