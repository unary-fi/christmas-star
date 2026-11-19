// Copyright (c) 2024-2026 Tuomas Terho
// SPDX-License-Identifier: MIT
//
// Star Studio front end. The stars are reached from the page itself with
// Web Bluetooth (js/studio.js); this file is the user interface.
import { StarLayout } from "../star_layout.js";
import { drawStar, fitCanvas, startSky, StarGeometry, rgbCss } from "./star_canvas.js";
import { EFFECTS, EffectPreview } from "./effects.js";
import { Studio, bluetoothAvailable } from "./studio.js";

const $ = (s) => document.querySelector(s);
const $$ = (s) => [...document.querySelectorAll(s)];
const FACE = StarLayout.FACE_LEDS;
const UPLOAD_BYTES_PER_S = 2300; // measured over BLE from Windows

// ---- connection ---------------------------------------------------------------

let stars = [];
let devKey = false;
const selected = new Set(JSON.parse(localStorage.getItem("studio-selected") || "[]"));
const pending = new Map();
let nextId = 1;
let studio = null;

function startStudio() {
  if (!bluetoothAvailable()) {
    $("#no-bluetooth").hidden = false;
    $("#empty").hidden = true;
    $("#scan").disabled = true;
    return;
  }
  studio = new Studio(onEvent);
  onEvent(studio.snapshot());
}

/** Hand a UI request to the studio (straight away: "scan" needs the click's gesture). */
function send(op, extra = {}) {
  if (!studio) return;
  const done = studio.handle({ op, ...extra });
  if (done && done.catch) done.catch((e) => toast("error", e.message || String(e)));
}

/** Run a command on stars; resolves when every star has answered. */
function command(text, { addresses = readyTargets(), quiet = false, timeout = 4 } = {}) {
  if (!addresses.length) return Promise.resolve([]);
  const id = nextId++;
  return new Promise((resolve) => {
    pending.set(id, { left: addresses.length, out: [], resolve });
    send("cmd", { id, text, addresses, quiet, timeout });
    setTimeout(() => {
      const p = pending.get(id);
      if (p) { pending.delete(id); p.resolve(p.out); }
    }, (timeout + 6) * 1000);
  });
}

/** Latest-wins sender: at most one command of a kind in flight. */
function paced(make) {
  let busy = false, next = null;
  const pump = async () => {
    if (busy || next === null) return;
    const text = next;
    next = null;
    busy = true;
    await command(text, { quiet: true });
    busy = false;
    pump();
  };
  return (...args) => { next = make(...args); pump(); };
}

function onEvent(m) {
  if (m.ev === "stars") {
    stars = m.stars;
    devKey = m.devKey;
    for (const a of [...selected]) {
      const s = stars.find((x) => x.address === a);
      if (!s || s.status !== "ready") selected.delete(a);
    }
    // Nothing chosen yet: control the first connected star.
    const firstReady = stars.find((s) => s.status === "ready");
    if (selected.size === 0 && firstReady) selected.add(firstReady.address);
    renderStars();
    syncFromPrimary();
  } else if (m.ev === "reply") {
    const p = pending.get(m.id);
    if (p) {
      p.out.push(m);
      if (--p.left === 0) { pending.delete(m.id); p.resolve(p.out); }
    }
    consoleReply(m);
  } else if (m.ev === "toast") {
    toast(m.level, m.text);
  } else if (m.ev === "progress") {
    showProgress(m);
  } else if (m.ev === "preview") {
    clipWaiting = null;
    if (m.frames) clips.set(`${m.fw}/${m.id}`, { step: m.step, frames: m.frames });
    else clipFailed.add(`${m.fw}/${m.id}`);
    requestNextClip();
  }
}

// ---- stars ---------------------------------------------------------------------

const byAddress = (a) => stars.find((s) => s.address === a);
const readyTargets = () => [...selected].filter((a) => byAddress(a)?.status === "ready");
const primary = () => byAddress(readyTargets()[0]);
const shortId = () => ""; // the browser hides Bluetooth addresses

function saveSelection() {
  localStorage.setItem("studio-selected", JSON.stringify([...selected]));
  renderStars();
  syncFromPrimary();
}

function badge(s) {
  const a = s.info?.access;
  switch (s.status) {
    case "ready": return a === "dev" ? ["Developer", "dev"] : a === "owner" ? ["Paired with this browser", "ok"] : ["Connected", "ok"];
    case "locked": return s.info?.pairing === "open" ? ["Ready to pair", "warn"] : ["Paired elsewhere", "warn"];
    case "connecting": return ["Connecting…", ""];
    case "renaming": return ["Renaming…", ""];
    case "updating": return ["Updating firmware…", "dev"];
    case "error": return ["Error", "err"];
    default: return ["Nearby", ""];
  }
}

function bars(rssi) {
  if (typeof rssi !== "number") return "";
  const n = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
  return `<span class="bars" title="${rssi} dBm">${[4, 7, 10, 12].map((h, i) =>
    `<i style="height:${h}px" class="${i < n ? "on" : ""}"></i>`).join("")}</span>`;
}

function renderStars() {
  const list = $("#star-list");
  $("#empty").hidden = stars.length > 0;
  list.innerHTML = "";
  for (const s of stars) {
    const [label, cls] = badge(s);
    const card = document.createElement("div");
    card.className = "star-card" + (selected.has(s.address) ? " selected" : "");
    card.setAttribute("role", "listitem");
    card.title = s.error || s.address;
    let action = "";
    if (s.status === "nearby" || s.status === "error") action = `<button class="ghost small" data-act="connect">Connect</button>`;
    else if (s.status === "locked") action = `<button class="primary small" data-act="pair" ${s.info?.pairing === "open" ? "" : "disabled title='Shake the star for 5 s first'"}>Pair</button><button class="ghost small" data-act="connect">Retry</button>`;
    else if (s.status === "ready") action = `<button class="ghost small" data-act="disconnect" title="Disconnect">×</button>`;
    else action = `<span class="spinner"></span>`;
    card.innerHTML = `<canvas></canvas>
      <div style="min-width:0"><div class="name">${escapeHtml(s.name || "Star")}</div>
      <div class="meta"><span>${shortId(s.address)}</span>${bars(s.rssi)}<span class="badge ${cls}">${label}</span></div></div>
      <div class="actions">${action}</div>`;
    card.addEventListener("click", (e) => {
      const act = e.target.closest("[data-act]")?.dataset.act;
      if (act === "connect") send("connect", { address: s.address });
      else if (act === "pair") send("connect", { address: s.address, pair: true });
      else if (act === "disconnect") { selected.delete(s.address); send("disconnect", { address: s.address }); saveSelection(); }
      else if (s.status === "ready") {
        if (e.ctrlKey || e.metaKey || e.shiftKey) selected.has(s.address) ? selected.delete(s.address) : selected.add(s.address);
        else { selected.clear(); selected.add(s.address); }
        saveSelection();
      } else if (s.status === "nearby" || s.status === "error") send("connect", { address: s.address });
    });
    list.appendChild(card);
    const [ctx, w, h] = fitCanvas(card.querySelector("canvas"));
    const lit = s.status === "ready" ? new Array(FACE).fill(0xF2B040) : null;
    drawStar(ctx, w, h, lit, { glow: 0.6, outline: s.status === "ready" ? 0.9 : 0.45, extent: 125 });
  }
  const ready = stars.filter((s) => s.status === "ready");
  $("#select-all").checked = ready.length > 0 && ready.every((s) => selected.has(s.address));
  const names = readyTargets().map((a) => byAddress(a).name);
  $("#target-name").textContent = names.length === 0 ? "No star selected" :
    names.length === 1 ? names[0] : `${names.length} stars: ${names.join(", ")}`;
  $("#keyinfo").textContent = devKey ? "Developer key in this browser: every star can be controlled." :
    "No developer key: only stars paired with this browser.";
  $("#devkey-state").textContent = devKey ? "saved in this browser" : "none";
  updateDevPanel();
}

const escapeHtml = (t) => t.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]);

$("#scan").onclick = () => send("scan");
$("#select-all").onchange = (e) => {
  selected.clear();
  if (e.target.checked) stars.filter((s) => s.status === "ready").forEach((s) => selected.add(s.address));
  saveSelection();
};

// ---- top bar: brightness, shake ------------------------------------------------

let syncing = false;
function syncFromPrimary() {
  const p = primary();
  if (!p || !p.state) return;
  syncing = true;
  if (document.activeElement !== $("#brightness") && p.state.bright) $("#brightness").value = p.state.bright;
  $("#shake").checked = p.state.shake === "1";
  if (document.activeElement !== $("#speed") && p.state.speed) {
    $("#speed").value = p.state.speed;
    $("#speed-out").textContent = `${p.state.speed} %`;
  }
  syncing = false;
  markActiveEffect();
}
const sendBrightness = paced((v) => `led brightness ${v}`);
$("#brightness").oninput = (e) => sendBrightness(e.target.value);
$("#shake").onchange = (e) => { if (!syncing) command(e.target.checked ? "shake on" : "shake off"); };

// ---- tabs ------------------------------------------------------------------------

let page = "colour";
for (const t of $$(".tab")) {
  t.onclick = () => {
    page = t.dataset.page;
    $$(".tab").forEach((x) => x.classList.toggle("active", x === t));
    $$(".page").forEach((p) => p.classList.toggle("active", p.id === `page-${page}`));
    if (page !== "picture" && $("#pic-live").checked) { $("#pic-live").checked = false; stopLive(); }
    if (page === "effects") requestNextClip();
    redrawAll();
  };
}

// ---- colour page -------------------------------------------------------------------

const PRESETS = [[0xFFB040, "Gold"], [0xFFE2C0, "Warm white"], [0xE0182A, "Red"], [0x10B040, "Green"], [0x7FC8FF, "Ice"], [0x8A4DFF, "Violet"]];
let front = 0xFFB040, back = 0xFFB040, target = "front";
const edited = () => (target === "back" ? back : front);
const sendColour = paced(() => {
  if (target === "both") return `set_all 0 0x${front.toString(16).padStart(6, "0")}`;
  if (target === "front") return `set_all 1 0x${front.toString(16).padStart(6, "0")}`;
  return `set_all 2 0x${back.toString(16).padStart(6, "0")}`;
});

function setEdited(c) {
  if (target === "back") back = c; else front = c;
  if (target === "both") back = front;
  syncSliders();
  drawColour();
  sendColour();
}
function syncSliders() {
  const c = edited();
  $$(".vslider").forEach((v) => {
    const val = (c >> (16 - 8 * Number(v.dataset.ch))) & 255;
    v.querySelector("input").value = val;
    v.querySelector("output").textContent = val;
  });
}
$$(".vslider input").forEach((inp) => {
  inp.oninput = () => {
    const ch = Number(inp.closest(".vslider").dataset.ch), shift = 16 - 8 * ch;
    setEdited((edited() & ~(0xFF << shift)) | (Number(inp.value) << shift));
  };
});
$$("#face-target button").forEach((b) => {
  b.onclick = () => {
    target = b.dataset.t;
    $$("#face-target button").forEach((x) => x.classList.toggle("active", x === b));
    if (target === "both") { back = front; sendColour(); }
    syncSliders();
    drawColour();
  };
});
$("#presets").innerHTML = PRESETS.map(([c, n]) =>
  `<button class="preset" data-c="${c}"><i style="background:${rgbCss(c)};color:${rgbCss(c)}"></i>${n}</button>`).join("");
$$(".preset").forEach((b) => (b.onclick = () => setEdited(Number(b.dataset.c))));

function drawColour() {
  for (const [id, c] of [["#colour-front", front], ["#colour-back", back]]) {
    const [ctx, w, h] = fitCanvas($(id));
    ctx.clearRect(0, 0, w, h);
    drawStar(ctx, w, h, new Array(FACE).fill(c));
  }
}

// ---- picture page ------------------------------------------------------------------

const pic = { el: null, video: false, w: 0, h: 0, scale: 1, ox: 0, oy: 0, front: null, fps: 25 };
const work = document.createElement("canvas");
let liveTimer = null;

function loadFile(file) {
  if (!file) return;
  const url = URL.createObjectURL(file);
  const isVideo = file.type.startsWith("video/");
  const el = isVideo ? document.createElement("video") : new Image();
  const ready = () => {
    pic.el = el;
    pic.video = isVideo;
    pic.w = isVideo ? el.videoWidth : el.naturalWidth;
    pic.h = isVideo ? el.videoHeight : el.naturalHeight;
    pic.scale = 1; pic.ox = 0; pic.oy = 0;
    $("#pic-drop").hidden = true;
    $("#pic-clear").hidden = false;
    $("#pic-keep").disabled = false;
    $("#video-controls").hidden = !isVideo;
    $("#pic-name").value = file.name.replace(/\.[^.]+$/, "").slice(0, 16);
    if (isVideo) {
      $("#trim-a").value = 0;
      $("#trim-b").value = Math.min(10, el.duration).toFixed(1);
      el.loop = true; el.muted = true; el.play();
    }
    updateEstimate();
  };
  if (isVideo) { el.onloadeddata = ready; el.src = url; el.playsInline = true; }
  else { el.onload = ready; el.src = url; }
}

$("#pic-open").onclick = () => $("#pic-file").click();
$("#pic-file").onchange = (e) => loadFile(e.target.files[0]);
$("#pic-clear").onclick = () => {
  if (pic.video) pic.el.pause();
  pic.el = null;
  $("#pic-drop").hidden = false;
  $("#pic-clear").hidden = true;
  $("#pic-keep").disabled = true;
  $("#video-controls").hidden = true;
  $("#pic-file").value = "";
};
const stage = $("#pic-stage");
stage.addEventListener("dragover", (e) => { e.preventDefault(); $("#pic-drop").classList.add("over"); });
stage.addEventListener("dragleave", () => $("#pic-drop").classList.remove("over"));
stage.addEventListener("drop", (e) => { e.preventDefault(); $("#pic-drop").classList.remove("over"); loadFile(e.dataTransfer.files[0]); });
let drag = null;
stage.addEventListener("pointerdown", (e) => {
  if (!pic.el || e.target.closest(".drop")) return;
  drag = { x: e.clientX, y: e.clientY, ox: pic.ox, oy: pic.oy };
  stage.setPointerCapture(e.pointerId);
  stage.classList.add("dragging");
});
stage.addEventListener("pointermove", (e) => {
  if (!drag) return;
  pic.ox = drag.ox + e.clientX - drag.x;
  pic.oy = drag.oy + e.clientY - drag.y;
});
stage.addEventListener("pointerup", () => { drag = null; stage.classList.remove("dragging"); });
stage.addEventListener("wheel", (e) => {
  if (!pic.el) return;
  e.preventDefault();
  pic.scale = Math.max(0.2, Math.min(12, pic.scale * Math.exp(-e.deltaY * 0.0015)));
}, { passive: false });

$("#vid-play").onclick = () => {
  if (!pic.video) return;
  if (pic.el.paused) { pic.el.play(); $("#vid-play").textContent = "Pause"; }
  else { pic.el.pause(); $("#vid-play").textContent = "Play"; }
};
$("#vid-seek").oninput = (e) => { if (pic.video) pic.el.currentTime = (e.target.value / 1000) * pic.el.duration; };
$$("#vid-fps button").forEach((b) => (b.onclick = () => {
  pic.fps = Number(b.dataset.v);
  $$("#vid-fps button").forEach((x) => x.classList.toggle("active", x === b));
  updateEstimate();
}));
["#trim-a", "#trim-b"].forEach((s) => ($(s).oninput = updateEstimate));

function frameCount() {
  if (!pic.video) return 1;
  const a = Number($("#trim-a").value), b = Number($("#trim-b").value);
  return Math.max(1, Math.round(Math.max(0, b - a) * pic.fps));
}
function updateEstimate() {
  if (!pic.el) { $("#pic-estimate").textContent = ""; return; }
  const n = frameCount(), bytes = 32 + n * (2 + 3 * StarLayout.LED_COUNT);
  const s = Math.ceil(bytes / UPLOAD_BYTES_PER_S);
  $("#pic-estimate").textContent = `${n} frame${n > 1 ? "s" : ""}, ${(bytes / 1024).toFixed(0)} KB, about ${s < 90 ? s + " s" : Math.ceil(s / 60) + " min"} to upload`;
}

/** Draw the media and the star; returns the 66 sampled front colours. */
function drawPicture() {
  const canvas = $("#pic-canvas");
  const [ctx, w, h] = fitCanvas(canvas);
  ctx.clearRect(0, 0, w, h);
  if (!pic.el) return null;
  const fit = Math.min(w / pic.w, h / pic.h), k = fit * pic.scale;
  const dw = pic.w * k, dh = pic.h * k;
  const dx = w / 2 + pic.ox - dw / 2, dy = h / 2 + pic.oy - dh / 2;

  // Sample from a plain rendering of the media at the display scale.
  work.width = w; work.height = h;
  const wctx = work.getContext("2d", { willReadFrequently: true });
  wctx.fillStyle = "#000";
  wctx.fillRect(0, 0, w, h);
  wctx.drawImage(pic.el, dx, dy, dw, dh);
  const px = wctx.getImageData(0, 0, w, h).data;
  const g = new StarGeometry(w, h);
  const spot = Math.max(1, Math.round(3.5 * g.pxPerMm)), step = Math.max(1, Math.round(spot / 1.5));
  const out = new Array(FACE);
  for (let i = 0; i < FACE; i++) {
    const [cx, cy] = g.led(i).map(Math.round);
    let r = 0, gg = 0, b = 0, n = 0;
    for (let yy = -spot; yy <= spot; yy += step) {
      for (let xx = -spot; xx <= spot; xx += step) {
        const x = cx + xx, y = cy + yy;
        if (x >= 0 && x < w && y >= 0 && y < h) {
          const o = (y * w + x) * 4;
          r += px[o]; gg += px[o + 1]; b += px[o + 2];
        }
        n++;
      }
    }
    out[i] = (Math.round(r / n) << 16) | (Math.round(gg / n) << 8) | Math.round(b / n);
  }

  // What the user sees: the media slightly dimmed, the star on top.
  ctx.globalAlpha = 0.55;
  ctx.drawImage(pic.el, dx, dy, dw, dh);
  ctx.globalAlpha = 1;
  const vignette = ctx.createRadialGradient(w / 2, h / 2, Math.min(w, h) * 0.3, w / 2, h / 2, Math.max(w, h) * 0.75);
  vignette.addColorStop(0, "rgba(7,11,30,0)");
  vignette.addColorStop(1, "rgba(7,11,30,0.75)");
  ctx.fillStyle = vignette;
  ctx.fillRect(0, 0, w, h);
  drawStar(ctx, w, h, out, { outline: 0.85 });
  if (pic.video && document.activeElement !== $("#vid-seek")) $("#vid-seek").value = Math.round((pic.el.currentTime / pic.el.duration) * 1000) || 0;
  pic.front = out;
  return out;
}

function stopLive() {
  clearInterval(liveTimer);
  liveTimer = null;
  send("live_stop", { addresses: readyTargets() });
  $("#pic-fps").textContent = "";
}
$("#pic-live").onchange = (e) => {
  if (!e.target.checked) { stopLive(); return; }
  if (!readyTargets().length) { toast("info", "Select a connected star first."); e.target.checked = false; return; }
  liveTimer = setInterval(() => {
    if (pic.front) send("live", { addresses: readyTargets(), front: pic.front });
    const fps = readyTargets().map((a) => byAddress(a)?.liveFps || 0);
    $("#pic-fps").textContent = fps.length ? `Star updates: ${Math.min(...fps).toFixed(0)} frames/s` : "";
  }, 50);
};

$("#pic-keep").onclick = async () => {
  const targets = readyTargets();
  if (!pic.el || !targets.length) { toast("info", "Select a connected star first."); return; }
  if ($("#pic-live").checked) { $("#pic-live").checked = false; stopLive(); }
  const name = $("#pic-name").value.trim() || "PC picture";
  let frames = [], duration = 1000;
  if (!pic.video) {
    frames = [drawPicture()];
  } else {
    const v = pic.el, wasPaused = v.paused;
    v.pause();
    const a = Number($("#trim-a").value), n = frameCount();
    duration = Math.round(1000 / pic.fps);
    for (let i = 0; i < n; i++) {
      v.currentTime = Math.min(v.duration, a + i / pic.fps);
      await new Promise((r) => v.addEventListener("seeked", r, { once: true }));
      frames.push(drawPicture());
      showProgress({ address: "prepare", done: i + 1, total: n, label: "Preparing frames" });
    }
    showProgress({ address: "prepare", done: -1 });
    if (!wasPaused) v.play();
  }
  send("upload", { addresses: targets, name, frames, durationMs: duration, loop: true, play: true });
};

// ---- effects page -------------------------------------------------------------------

const previews = [];
let effectTime = 0, lastFrame = performance.now();

// Preview clips rendered by the star (`app preview`), per firmware version.
const clips = new Map();
const clipFailed = new Set();
let clipWaiting = null;
const localPreview = new Set(EFFECTS.map((e) => e.id));

function requestNextClip() {
  const p = primary();
  if (!p || clipWaiting || p.status !== "ready" || !p.effects?.length || page !== "effects") return;
  const fw = p.info?.fw || "0";
  const next = p.effects.find((e) => !clips.has(`${fw}/${e.id}`) && !clipFailed.has(`${fw}/${e.id}`));
  if (!next) return;
  clipWaiting = next.id;
  send("preview", { address: p.address, id: next.id });
  setTimeout(() => { if (clipWaiting === next.id) { clipWaiting = null; requestNextClip(); } }, 60000);
}

const TURN_MS = 8000;

/**
 * A two-faced frame (132 LEDs) seen on a star turning around its vertical
 * axis: the front while it faces us, then the back (each back LED sits at the
 * same place as its front partner; the mirroring comes from scaleX < 0).
 */
function turning(frame, nowMs) {
  const c = Math.cos((2 * Math.PI * (nowMs % TURN_MS)) / TURN_MS);
  const back = c < 0;
  const colours = new Array(FACE);
  for (let i = 0; i < FACE; i++) colours[i] = frame[back ? StarLayout.partner[i] : i];
  return { colours, scaleX: c, glow: 0.55 + 0.45 * Math.abs(c) };
}

function buildEffects() {
  const p = primary();
  // The star's own list (firmware 1.4+), else the effects this page knows.
  const list = p?.effects?.length ? p.effects : EFFECTS;
  const items = [...(p?.state?.flash ? [{ id: "flash", title: "Your video", subtitle: p.state.flash }] : []), ...list];
  const key = items.map((e) => e.id).join(",");
  if ($("#effect-grid").dataset.key === key) return;
  $("#effect-grid").dataset.key = key;
  $("#effect-grid").innerHTML = "";
  previews.length = 0;
  for (const e of items) {
    const card = document.createElement("button");
    card.className = "effect-card";
    card.dataset.id = e.id;
    const chips = [e.tilt ? "Tilt" : "", e.twoFaces ? "Two faces" : ""].filter(Boolean)
      .map((c) => `<span class="chip">${c}</span>`).join("");
    card.innerHTML = `<canvas></canvas><h3>${escapeHtml(e.title)}${chips}</h3><p>${escapeHtml(e.subtitle)}</p>`;
    card.onclick = () => {
      command(`effect play ${e.id}`);
      $$(".effect-card").forEach((c) => c.classList.toggle("active", c === card));
    };
    $("#effect-grid").appendChild(card);
    previews.push({
      id: e.id,
      canvas: card.querySelector("canvas"),
      fx: localPreview.has(e.id) || e.id === "flash" ? new EffectPreview(e.id) : null,
    });
  }
  markActiveEffect();
  requestNextClip();
}
function markActiveEffect() {
  const fx = primary()?.state?.effect;
  $$(".effect-card").forEach((c) => c.classList.toggle("active", c.dataset.id === fx));
}
const sendSpeed = paced((v) => `effect speed ${v}`);
$("#speed").oninput = (e) => { $("#speed-out").textContent = `${e.target.value} %`; sendSpeed(e.target.value); };

// ---- developer page --------------------------------------------------------------------

const history = [];
let histPos = 0;
function consoleWrite(html) {
  const c = $("#console");
  c.insertAdjacentHTML("beforeend", html);
  c.scrollTop = c.scrollHeight;
}
function consoleReply(m) {
  if (!m.id || !consoleIds.has(m.id)) return;
  const multi = consoleIds.get(m.id) > 1;
  const who = multi ? `<span class="who">[${escapeHtml(m.name)}]</span>\n` : "";
  consoleWrite(`${who}${escapeHtml(m.text || "")}\n`.replace(/^(error:.*)$/m, '<span class="err">$1</span>'));
}
const consoleIds = new Map();
$("#console-form").onsubmit = (e) => {
  e.preventDefault();
  const text = $("#console-line").value.trim();
  if (!text) return;
  const targets = readyTargets();
  if (!targets.length) { toast("info", "Select a connected star first."); return; }
  history.push(text);
  histPos = history.length;
  $("#console-line").value = "";
  consoleWrite(`<span class="cmd">star:~$ ${escapeHtml(text)}</span>\n`);
  const id = nextId;
  consoleIds.set(id, targets.length);
  command(text, { addresses: targets, timeout: /upload|test/.test(text) ? 30 : 6 });
};
$("#console-line").onkeydown = (e) => {
  if (e.key === "ArrowUp" && histPos > 0) { $("#console-line").value = history[--histPos]; e.preventDefault(); }
  if (e.key === "ArrowDown") { histPos = Math.min(history.length, histPos + 1); $("#console-line").value = history[histPos] || ""; e.preventDefault(); }
};

let missing = new Set(), missFace = 1;
async function loadMissing() {
  const p = primary();
  if (!p) return;
  const [r] = await command("led missing list", { addresses: [p.address], quiet: true });
  missing = new Set((r?.text.match(/D\d{3}/g) || []));
  drawMissing();
}
/** Board reference of an LED: D2xx on the front (top layer), D3xx on the back. */
function designatorOf(frontIndex) {
  const i = missFace === 1 ? frontIndex : StarLayout.partner[frontIndex];
  return `D${missFace === 1 ? 2 : 3}${String(StarLayout.designator[i]).padStart(2, "0")}`;
}
function drawMissing() {
  const [ctx, w, h] = fitCanvas($("#miss-canvas"));
  ctx.clearRect(0, 0, w, h);
  const colours = [], marks = new Set();
  for (let i = 0; i < FACE; i++) {
    const m = missing.has(designatorOf(i));
    colours.push(m ? 0 : 0xF2B040);
    if (m) marks.add(i);
  }
  return drawStar(ctx, w, h, colours, { glow: 0.5, marks });
}
$("#miss-canvas").onclick = async (e) => {
  if (!primary()) return;
  const r = $("#miss-canvas").getBoundingClientRect();
  const g = new StarGeometry(r.width, r.height);
  const i = g.hit(e.clientX - r.left, e.clientY - r.top);
  if (i < 0) return;
  const d = designatorOf(i);
  await command(`led missing ${missing.has(d) ? "del" : "add"} ${d}`);
  loadMissing();
};
$$("#miss-face button").forEach((b) => (b.onclick = () => {
  missFace = Number(b.dataset.f);
  $$("#miss-face button").forEach((x) => x.classList.toggle("active", x === b));
  drawMissing();
}));

function updateDevPanel() {
  const p = primary();
  if (!p) {
    $("#dev-status").textContent = "Select a connected star.";
    $("#pair-info").textContent = "";
    return;
  }
  const i = p.info || {}, s = p.state || {};
  $("#fw-version").textContent = i.fw ? `running ${i.fw}` : "";
  $("#dev-status").textContent =
    `${p.name}  (${p.address})\nfirmware ${i.fw || "?"}, protocol ${i.proto || "?"}, access ${i.access || "?"}\n` +
    `effect ${s.effect || "?"}, ${s.fps || "?"} fps, ${s.missing ?? "?"} LEDs missing\n` +
    `stored effect: ${s.flash || "none"} (${s.frames || 0} frames of ${s.cap || "?"})\n\n${p.led || ""}`;
  $("#pair-info").textContent = s.owner === "paired" || i.owner === "paired"
    ? (i.access === "owner" ? "Paired with this browser." : "Paired with a phone or another computer. Developer access works anyway.")
    : "Not paired: the next phone or browser to pair becomes its owner.";
  if (document.activeElement !== $("#limit")) {
    const m = (p.led || "").match(/limit (\d+)\)/);
    if (m) $("#limit").value = m[1];
  }
}
$("#dev-refresh").onclick = () => { command("app state", { quiet: false }); loadMissing(); };
$("#rename").oninput = (e) => ($("#rename-count").textContent = `${e.target.value.length}/16`);
$("#rename-go").onclick = () => {
  const base = $("#rename").value.trim(), targets = readyTargets();
  if (!base || !targets.length) return;
  if (!/^[\x20-\x7e]{1,16}$/.test(base)) { toast("error", "Use 1–16 plain letters, digits or punctuation."); return; }
  const names = targets.length === 1 ? [base] : targets.map((_, k) => `${base}-${k + 1}`.slice(0, 16));
  send("rename", { addresses: targets, names });
  toast("info", "Renaming… the star restarts its Bluetooth for a few seconds.");
};
$("#pair-open").onclick = () => send("pairing_open", { addresses: readyTargets() });
$("#pair-release").onclick = () => {
  const b = $("#pair-release");
  if (b.dataset.confirm !== "1") {
    b.dataset.confirm = "1";
    b.textContent = "Click again to release";
    setTimeout(() => { b.dataset.confirm = ""; b.textContent = "Release from owner"; }, 4000);
    return;
  }
  b.dataset.confirm = "";
  b.textContent = "Release from owner";
  send("unpair", { addresses: readyTargets() });
};
$("#limit-go").onclick = () => command(`led limit ${$("#limit").value}`);
$("#fw-go").onclick = () => {
  const f = $("#fw-file").files[0], targets = readyTargets();
  if (!f) { toast("info", "Choose zephyr.signed.bin first."); return; }
  if (!targets.length) { toast("info", "Select a connected star first."); return; }
  const b = $("#fw-go");
  if (b.dataset.confirm !== "1") {
    b.dataset.confirm = "1";
    b.textContent = `Update ${targets.length} star${targets.length > 1 ? "s" : ""}?`;
    setTimeout(() => { b.dataset.confirm = ""; b.textContent = "Update"; }, 4000);
    return;
  }
  b.dataset.confirm = "";
  b.textContent = "Update";
  f.arrayBuffer().then((file) => send("firmware", { addresses: targets, file, permanent: $("#fw-permanent").checked }));
};

$("#csfx-go").onclick = () => {
  const f = $("#csfx-file").files[0];
  if (!f || !readyTargets().length) return;
  f.arrayBuffer().then((file) => send("upload", { addresses: readyTargets(), file, play: true }));
};

$("#devkey-form").onsubmit = (e) => {
  e.preventDefault();
  if (!studio) return;
  if (studio.setDevKey($("#devkey").value)) {
    $("#devkey").value = "";
    toast("ok", "Developer key saved in this browser. Reconnect a star to use it.");
  } else {
    toast("error", "The developer key is 32 hex digits.");
  }
};
$("#devkey-forget").onclick = () => {
  if (!studio) return;
  studio.setDevKey(null);
  toast("info", "Developer key removed from this browser.");
};

// ---- progress and toasts ---------------------------------------------------------------

const progress = new Map();
function showProgress(m) {
  if (m.done < 0) progress.delete(m.address);
  else progress.set(m.address, m);
  const box = $("#progress");
  if (!progress.size) { box.hidden = true; return; }
  let done = 0, total = 0, label = "";
  for (const p of progress.values()) { done += p.done; total += p.total; label = p.label || label; }
  box.hidden = false;
  const names = [...progress.keys()].map((a) => byAddress(a)?.name).filter(Boolean);
  const amount = total > 4096 ? `${(done / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KB` : `${done}/${total}`;
  $("#progress-text").textContent = label ? `${label} ${amount}` :
    `Uploading to ${names.join(", ")}: ${(done / 1024).toFixed(0)} / ${(total / 1024).toFixed(0)} KB`;
  $("#progress-bar").style.width = `${total ? (100 * done) / total : 0}%`;
}
function toast(level, text) {
  const t = document.createElement("div");
  t.className = `toast ${level}`;
  t.textContent = text;
  $("#toasts").appendChild(t);
  setTimeout(() => t.remove(), level === "error" ? 9000 : 5000);
}

// ---- drawing loop ----------------------------------------------------------------------

function redrawAll() {
  if (page === "colour") drawColour();
  if (page === "effects") buildEffects();
  if (page === "dev") { drawMissing(); loadMissing(); }
}
function loop(now) {
  const speed = Number($("#speed").value);
  effectTime += (now - lastFrame) * speed / 100;
  lastFrame = now;
  if (page === "picture") drawPicture();
  if (page === "effects") {
    buildEffects();
    const fw = primary()?.info?.fw || "0";
    for (const p of previews) {
      const [ctx, w, h] = fitCanvas(p.canvas);
      ctx.clearRect(0, 0, w, h);
      // The star's own clip when it has arrived; else the built-in preview,
      // or the plain outline for an effect this page does not know.
      const clip = clips.get(`${fw}/${p.id}`);
      const colours = clip ? clip.frames[Math.floor(effectTime / clip.step) % clip.frames.length]
        : p.fx ? p.fx.render(Math.round(effectTime)) : null;
      if (colours && colours.length === StarLayout.LED_COUNT) {
        // Two different faces: turn slowly around the vertical axis.
        const { colours: seen, scaleX, glow } = turning(colours, performance.now());
        drawStar(ctx, w, h, seen, { extent: 125, scaleX, glow, outline: 0.45 + 0.4 * Math.abs(scaleX) });
      } else {
        drawStar(ctx, w, h, colours, { extent: 125 });
      }
    }
  }
  requestAnimationFrame(loop);
}
window.addEventListener("resize", redrawAll);

// ---- Comet, the game ---------------------------------------------------------------------

let gameMode = "single", gameState = "off", gamePoll = null;
$$("#game-mode button").forEach((b) => (b.onclick = () => {
  gameMode = b.dataset.m;
  $$("#game-mode button").forEach((x) => x.classList.toggle("active", x === b));
}));
const gameCmd = (arg) => command(arg ? `app game ${arg}` : "app game", { quiet: true, timeout: 2 });
async function pollGame() {
  const [r] = await gameCmd("");
  const m = (r?.text || "").match(/game state=.*/);
  if (!m) return;
  const kv = Object.fromEntries(m[0].slice(5).trim().split(/\s+/).map((t) => t.split("=")));
  gameState = kv.state;
  const both = kv.mode === "double";
  $("#game-status").textContent = kv.state === "off"
    ? `Not playing. Best: one side ${kv.best1}, both sides ${kv.best2}.`
    : `${kv.state}  ·  ${both ? `both sides, comet on the ${kv.face}` : "one side"}\n` +
      `score ${kv.score}  ·  level ${kv.level}  ·  length ${kv.len}\n` +
      `best: one side ${kv.best1}, both sides ${kv.best2}`;
  $("#game-pause").textContent = kv.state === "paused" ? "Resume" : "Pause";
}
function pollGameSoon() {
  if (!gamePoll) gamePoll = setInterval(() => { if (page === "dev" && primary()) pollGame(); }, 400);
  pollGame();
}
$("#game-start").onclick = async () => { await gameCmd(`start ${gameMode}`); pollGameSoon(); };
$("#game-pause").onclick = () => gameCmd(gameState === "paused" ? "resume" : "pause");
$("#game-stop").onclick = async () => { await gameCmd("stop"); pollGame(); };
document.addEventListener("keydown", (e) => {
  if (page !== "dev" || !["countdown", "play", "paused"].includes(gameState)) return;
  if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return;
  const key = e.key.length === 1 ? e.key.toLowerCase() : e.key;
  const steer = { ArrowLeft: "l", a: "l", ArrowRight: "r", d: "r", ArrowUp: "f", w: "f", ArrowDown: "f", s: "f" }[key];
  if (steer) gameCmd(steer);
  else if (e.key === " ") gameCmd(gameState === "paused" ? "resume" : "pause");
  else if (e.key === "Escape") gameCmd("stop");
  else return;
  e.preventDefault();
});

const startTab = $$(".tab").find((t) => `#${t.dataset.page}` === location.hash);
if (startTab) startTab.click();
startSky($("#sky"));
syncSliders();
startStudio();
redrawAll();
requestAnimationFrame(loop);
