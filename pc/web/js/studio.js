// Copyright (c) 2024-2026 Tuomas Terho
// SPDX-License-Identifier: MIT
//
// Star Studio's engine in the page: the stars connected over Web Bluetooth,
// the keys, and the operations the UI asks for ({op, ...} messages). It
// answers with the events the UI shows (stars, reply, toast, progress,
// preview), the same messages the Python studio used to send.

import { Star, SERVICE } from "./ble_star.js";
import * as sp from "./protocol.js";
import { StarLayout } from "../star_layout.js";

const FACTORY_NAME = "Dual-SPP";
const POLL_EVERY_MS = 5000;
const DEV_KEY = "studio-dev-key";
const PC_KEY = "studio-pc-key";

function storeGet(k) { try { return localStorage.getItem(k); } catch { return null; } }
function storeSet(k, v) { try { v === null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch { /* ignore */ } }

// ---- preview cache (IndexedDB, per firmware version) --------------------------------

function idb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open("star-studio", 1);
    req.onupgradeneeded = () => req.result.createObjectStore("previews");
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}
async function cacheGet(key) {
  try {
    const db = await idb();
    return await new Promise((res) => {
      const r = db.transaction("previews").objectStore("previews").get(key);
      r.onsuccess = () => res(r.result || null);
      r.onerror = () => res(null);
    });
  } catch { return null; }
}
async function cachePut(key, value) {
  try {
    const db = await idb();
    db.transaction("previews", "readwrite").objectStore("previews").put(value, key);
  } catch { /* the page works without the cache */ }
}

export const bluetoothAvailable = () => typeof navigator !== "undefined" && !!navigator.bluetooth;

export class Studio {
  constructor(onEvent) {
    this.onEvent = onEvent;
    this.stars = new Map();
    this.dirty = false;
    setInterval(() => { for (const s of this.stars.values()) s.refresh(); }, POLL_EVERY_MS);
    this.restore();
  }

  // ---- keys --------------------------------------------------------------------

  get devKey() { return sp.parseKey(storeGet(DEV_KEY)); }
  setDevKey(text) {
    const key = text === null ? null : sp.parseKey(text);
    if (text !== null && !key) return false;
    storeSet(DEV_KEY, key ? sp.hex(key) : null);
    this.changed();
    return true;
  }

  /** This browser's own owner key (created when it first pairs a star). */
  pcKey(create) {
    let key = sp.parseKey(storeGet(PC_KEY));
    if (!key && create) {
      key = crypto.getRandomValues(new Uint8Array(16));
      storeSet(PC_KEY, sp.hex(key));
    }
    return key;
  }

  keys(pair) { return [this.devKey, this.pcKey(pair)].filter(Boolean); }

  // ---- events ------------------------------------------------------------------

  changed() {
    if (this.dirty) return;
    this.dirty = true;
    setTimeout(() => { this.dirty = false; this.onEvent(this.snapshot()); }, 100);
  }

  snapshot() {
    const stars = [...this.stars.values()]
      .sort((a, b) => (a.status === "nearby") - (b.status === "nearby") || a.name.localeCompare(b.name))
      .map((s) => s.toJSON());
    return { ev: "stars", stars, devKey: this.devKey !== null };
  }

  toast(level, text) { this.onEvent({ ev: "toast", level, text }); }

  // ---- finding stars -------------------------------------------------------------

  add(device) {
    let s = this.stars.get(device.id);
    if (!s) {
      s = new Star(device, this);
      this.stars.set(device.id, s);
    }
    return s;
  }

  /** Ask the user to choose a star (must run from a click). */
  async addStar() {
    let device;
    try {
      device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE] }, { name: FACTORY_NAME }],
        optionalServices: [SERVICE],
      });
    } catch (e) {
      if (e.name !== "NotFoundError") this.toast("error", `Bluetooth: ${e.message}`);
      return; // NotFoundError: the user closed the chooser
    }
    const s = this.add(device);
    this.changed();
    await s.connect();
    this.afterConnect([s]);
  }

  /** Stars this site may use already (where the browser supports it). */
  async restore() {
    if (!navigator.bluetooth?.getDevices) return;
    try {
      for (const device of await navigator.bluetooth.getDevices()) {
        const s = this.add(device);
        try { await device.watchAdvertisements?.(); } catch { /* not everywhere */ }
        s.connect().then(() => this.changed());
      }
      this.changed();
    } catch { /* the user can still add stars */ }
  }

  afterConnect(stars) {
    for (const s of stars) {
      if (s.status === "locked") {
        this.toast("info", `${s.name}: paired to another device. Shake it for 5 s (or power it on, if released) and press Pair.`);
      }
    }
  }

  pick(msg) {
    const addrs = msg.addresses || (msg.address ? [msg.address] : []);
    return addrs.map((a) => this.stars.get(a)).filter(Boolean);
  }

  // ---- operations ------------------------------------------------------------------

  /** Handle one UI message. "scan" must be called straight from a click. */
  handle(msg) {
    const stars = this.pick(msg);
    switch (msg.op) {
      case "scan": return this.addStar();
      case "connect":
        return Promise.all(stars.map((s) => s.connect(!!msg.pair))).then(() => this.afterConnect(stars));
      case "disconnect":
        return Promise.all(stars.map(async (s) => { await s.stopLive(); s.close(); s.status = "nearby"; })).then(() => this.changed());
      case "cmd":
        return Promise.all(stars.filter((s) => s.status === "ready").map(async (s) => {
          let text;
          try { text = await s.run(msg.text, 1000 * Number(msg.timeout || 4), !msg.quiet); } catch (e) { text = `error: ${e.message}`; }
          this.onEvent({ ev: "reply", id: msg.id, address: s.address, name: s.name, text });
        }));
      case "live":
        for (const s of stars) if (s.status === "ready") s.setLive(msg.front);
        return null;
      case "live_stop": return Promise.all(stars.map((s) => s.stopLive()));
      case "upload": return this.upload(stars, msg);
      case "rename":
        return (async () => {
          for (let i = 0; i < stars.length; i++) {
            const name = msg.names ? msg.names[i] : msg.name;
            try { await stars[i].rename(name); this.toast("ok", `Renamed to ${stars[i].name}`); }
            catch (e) { this.toast("error", `${stars[i].name}: rename failed: ${e.message}`); }
          }
        })();
      case "unpair":
        return Promise.all(stars.map(async (s) => {
          const force = s.info.access === "dev" ? " force" : "";
          this.toast("info", `${s.name}: ${await s.run("star unpair" + force)}`);
        }));
      case "preview": return this.sendPreview(stars, msg.id);
      case "firmware": return this.firmware(stars, msg);
      case "pairing_open":
        return Promise.all(stars.map(async (s) => this.toast("info", `${s.name}: ${await s.run("star pairing open")}`)));
      default: return null;
    }
  }

  async sendPreview(stars, id) {
    const s = stars.find((x) => x.status === "ready");
    if (!s) return;
    const fw = s.info.fw || "0";
    let clip = await cacheGet(`${fw}/${id}`);
    if (!clip) {
      try {
        clip = await s.preview(id);
      } catch (e) {
        this.onEvent({ ev: "preview", fw, id, error: e.message });
        return;
      }
      cachePut(`${fw}/${id}`, clip);
    }
    this.onEvent({ ev: "preview", fw, id, ...clip });
  }

  async firmware(stars, msg) {
    const blob = new Uint8Array(msg.file);
    const version = sp.imageVersion(blob);
    if (!version) { this.toast("error", "Not a signed firmware image (use zephyr.signed.bin)"); return; }
    // One star at a time: each restarts and must come back before the next.
    for (const s of stars.filter((x) => x.status === "ready")) {
      const progress = (done, total) => this.onEvent({
        ev: "progress", address: s.address, done, total, label: `Firmware ${version} to ${s.name}:`,
      });
      try {
        this.toast("info", `${s.name}: updating to ${version}…`);
        const now = await s.updateFirmware(blob, !!msg.permanent, progress);
        const ok = now === version.split("+")[0];
        this.toast(ok ? "ok" : "error", `${s.name}: now running ${now}${ok ? "" : " (the update rolled back)"}`);
      } catch (e) {
        this.toast("error", `${s.name}: firmware update failed: ${e.message}`);
      }
      this.onEvent({ ev: "progress", address: s.address, done: -1, total: 0 });
    }
  }

  async upload(stars, msg) {
    let blob;
    if (msg.file) {
      blob = new Uint8Array(msg.file);
      if (String.fromCharCode(...blob.subarray(0, 4)) !== "CSFX") { this.toast("error", "Not a .csfx effect file"); return; }
    } else {
      const frames = msg.frames.map((front) => ({ front, ms: msg.durationMs }));
      blob = sp.csfx(msg.name || "PC effect", frames, StarLayout.partner, msg.loop !== false);
    }
    await Promise.all(stars.filter((s) => s.status === "ready").map(async (s) => {
      const progress = (done, total) => this.onEvent({ ev: "progress", address: s.address, done, total });
      try {
        const t0 = performance.now();
        await s.upload(blob, msg.play !== false, progress);
        this.toast("ok", `${s.name}: ${Math.floor(blob.length / 1024)} KB uploaded in ${Math.round((performance.now() - t0) / 1000)} s`);
      } catch (e) {
        this.toast("error", `${s.name}: upload failed: ${e.message}`);
      }
      this.onEvent({ ev: "progress", address: s.address, done: -1, total: 0 });
    }));
  }
}
