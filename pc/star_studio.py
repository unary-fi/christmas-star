#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Star Studio: control every Christmas Star in the room from the PC.

    python pc/star_studio.py            # opens the studio in your browser

The studio keeps Bluetooth LE links to several stars at once (bleak) and serves
its user interface (pc/web) to the browser on this computer only. Stars are
unlocked with the developer key (sw/dev_key.txt) or this PC's own key
(~/.christmas-star/pc_key), see sw/tools/star_auth.py.

Requires: bleak, aiohttp
"""
import argparse
import asyncio
import base64
import json
import secrets
import sys
import time
import webbrowser
from pathlib import Path

try:
    from aiohttp import WSMsgType, web
    from bleak import BleakClient, BleakScanner
except ImportError as e:
    sys.exit(f"Star Studio needs the packages bleak and aiohttp ({e}).\n"
             "Start it with pc\\star_studio.cmd (uses the project's .venv), or install them:\n"
             f"  \"{sys.executable}\" -m pip install bleak aiohttp")

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "sw" / "tools"))
sys.path.insert(0, str(HERE))
import star_auth  # noqa: E402
import star_protocol as sp  # noqa: E402

SERVICE = "49535343-fe7d-4ae5-8fa9-9fafd205e455"
TX_CHAR = "49535343-1e4d-4bd9-ba61-23c647249616"  # star -> PC (notify)
RX_CHAR = "49535343-8841-43f4-a8d4-ecbe34729bb3"  # PC -> star (write)
FACTORY_NAME = "Dual-SPP"
PROMPT = b"star:~$ "
SCAN_EVERY_S = 12
POLL_EVERY_S = 5
PREVIEW_CACHE = Path.home() / ".christmas-star" / "previews"
LIVE_WINDOW = 2
LIVE_KEEPALIVE_S = 1.0
LIVE_ACK_TIMEOUT_S = 0.7


def partner_table():
    import make_effect  # sw/tools; reads the LED positions

    return [e["partner"] for e in make_effect.layout()]


def parse_kv(line):
    out = {}
    for item in line.split():
        k, sep, v = item.partition("=")
        if sep:
            out[k] = v
    return out


def parse_state(text):
    """Parse 'state effect=.. flash=<name> frames=N owner=.. name=<name>' (names may hold spaces)."""
    line = next((ln for ln in text.split("\n") if ln.startswith("state ")), "")
    if not line:
        return {}
    head, _, name = line[6:].partition(" name=")
    flash = ""
    if " flash=" in head:
        before, _, rest = head.partition(" flash=")
        flash, _, after = rest.partition(" frames=")
        head = before + " frames=" + after
    out = parse_kv(head)
    out["flash"] = "" if flash == "-" else flash
    out["name"] = name.strip()
    return out


class Star:
    def __init__(self, studio, address, name, rssi):
        self.studio = studio
        self.address = address
        self.name = name
        self.rssi = rssi
        self.seen = time.time()
        self.client = None
        self.rx = bytearray()
        self.rx_event = asyncio.Event()
        self.lock = asyncio.Lock()
        self.status = "nearby"   # nearby, connecting, ready, locked, renaming, error
        self.error = ""
        self.info = {}
        self.state = {}
        self.led = ""
        self.effects = []  # from `app effects` (protocol 3), else the apps' own list
        self.live_frame = None
        self.live_task = None
        self.live_fps = 0.0
        self.chunk = 20

    # ---- view ----------------------------------------------------------------

    def to_json(self):
        return {
            "address": self.address, "name": self.name, "rssi": self.rssi,
            "status": self.status, "error": self.error, "info": self.info,
            "state": self.state, "led": self.led, "liveFps": round(self.live_fps, 1),
            "effects": self.effects,
            "seenAgo": round(time.time() - self.seen),
        }

    def changed(self):
        self.studio.changed()

    # ---- link ----------------------------------------------------------------

    def _on_data(self, _h, data):
        self.rx.extend(data)
        self.rx_event.set()

    def _on_disconnect(self, _client):
        if self.status not in ("renaming", "updating"):
            self.status = "nearby"
            self.info, self.live_frame = {}, None
            self.changed()

    async def open(self):
        last = None
        for attempt in range(3):
            client = BleakClient(self.address, timeout=20, disconnected_callback=self._on_disconnect)
            try:
                await client.connect()
                await client.start_notify(TX_CHAR, self._on_data)
                self.client = client
                self.chunk = max(20, min(client.mtu_size - 3, 128))
                return
            except Exception as e:  # Windows sometimes drops a fresh link
                last = e
                try:
                    await client.disconnect()
                except Exception:
                    pass
                await asyncio.sleep(1.5)
        raise RuntimeError(f"cannot connect: {last}")

    async def close(self):
        self.live_frame = None
        if self.client is not None:
            try:
                await self.client.disconnect()
            except Exception:
                pass
        self.client = None

    @property
    def connected(self):
        return self.client is not None and self.client.is_connected

    async def write(self, data):
        for i in range(0, len(data), self.chunk):
            await self.client.write_gatt_char(RX_CHAR, bytes(data[i:i + self.chunk]), response=True)

    def drain(self):
        out = bytes(self.rx)
        self.rx.clear()
        self.rx_event.clear()
        return out

    async def read_until(self, token, timeout):
        end = time.time() + timeout
        while token not in self.rx and time.time() < end:
            self.rx_event.clear()
            try:
                await asyncio.wait_for(self.rx_event.wait(), max(0.01, end - time.time()))
            except asyncio.TimeoutError:
                break
        i = self.rx.find(token)
        cut = len(self.rx) if i < 0 else i + len(token)
        out = bytes(self.rx[:cut])
        del self.rx[:cut]
        return out

    async def read_byte(self, timeout):
        if not self.rx:
            self.rx_event.clear()
            try:
                await asyncio.wait_for(self.rx_event.wait(), timeout)
            except asyncio.TimeoutError:
                return None
        if not self.rx:
            return None
        b = self.rx[0]
        del self.rx[0]
        return b

    async def line(self, timeout=3.0):
        raw = (await self.read_until(b"\n", timeout)).decode(errors="replace")
        lines = [ln.strip() for ln in raw.replace("\r", "").split("\n")]
        lines = [ln for ln in lines if ln and not ln.startswith("star:~$")]
        return lines[0] if lines else ""

    async def command(self, text, timeout=4.0):
        """Run a shell command (link lock must be held); returns its output."""
        self.drain()
        await self.write((text + "\r\n").encode())
        out = (await self.read_until(PROMPT, timeout)).decode(errors="replace")
        lines = [ln.rstrip() for ln in out.replace("\r", "").split("\n")]
        lines = [ln for ln in lines if ln.strip() and not ln.startswith("star:~$")]
        if lines and lines[0].strip() == text:
            lines = lines[1:]
        return "\n".join(lines)

    # ---- session ---------------------------------------------------------------

    async def hello(self):
        self.drain()
        await self.write(b"app hello\r\n")
        for _ in range(3):
            info = star_auth.parse_hello(await self.line())
            if info:
                return info
        raise RuntimeError("the star does not answer")

    async def login(self, pair=False):
        info = await self.hello()
        if info["proto"] < 2:
            info["access"] = "open"
            return info
        keys = [k for k in (star_auth.dev_key(), star_auth.pc_key(create=pair)) if k]
        for i, key in enumerate(keys):
            if i:
                info = await self.hello()
            await self.write(f"app auth {star_auth.auth_mac(key, bytes.fromhex(info['nonce']))}\r\n".encode())
            reply = await self.line()
            if reply.startswith("ok access="):
                info["access"] = reply.split("=", 1)[1]
                return info
        if pair and info.get("pairing") == "open":
            await self.write(f"app pair {star_auth.pc_key().hex()}\r\n".encode())
            if (await self.line()).startswith("ok paired"):
                info.update(access="owner", owner="paired", pairing="closed")
                return info
        info["access"] = "none"
        return info

    async def connect(self, pair=False):
        async with self.lock:
            self.status, self.error = "connecting", ""
            self.changed()
            try:
                if not self.connected:
                    await self.open()
                self.info = await self.login(pair)
                if self.info.get("name"):
                    self.name = self.info["name"]
                if self.info.get("access") == "none":
                    self.status = "locked"
                else:
                    self.status = "ready"
                    await self._refresh()
                    if self.info.get("proto", 1) >= 3:
                        self.effects = sp.parse_effects(await self.command("app effects"))
            except Exception as e:
                self.status, self.error = "error", str(e)
                await self.close()
        self.changed()

    async def _refresh(self):
        self.state = parse_state(await self.command("app state"))
        if self.state.get("name"):
            self.name = self.state["name"]
        self.led = await self.command("led status")

    async def refresh(self):
        if self.status != "ready" or self.lock.locked() or self.live_frame is not None:
            return
        async with self.lock:
            try:
                await self._refresh()
            except Exception as e:
                self.error = str(e)
        self.changed()

    async def run(self, text, timeout=4.0, refresh=True):
        await self.stop_live()
        async with self.lock:
            out = await self.command(text, timeout)
            if refresh:
                try:
                    await self._refresh()
                except Exception:
                    pass
        if refresh:
            self.changed()
        return out

    # ---- live frames -------------------------------------------------------------

    def set_live(self, front):
        self.live_frame = sp.stream_frame(front)
        if self.live_task is None or self.live_task.done():
            self.live_task = asyncio.create_task(self._pump())

    async def stop_live(self):
        self.live_frame = None
        if self.live_task is not None:
            try:
                await self.live_task
            except Exception:
                pass
            self.live_task = None

    async def _pump(self):
        async with self.lock:
            self.drain()
            await self.write(b"stream\r\n")
            if await self._wait_ack(2.0) is None:
                self.live_frame = None
                return
            inflight, last_sent, last_at, acks, window = 0, None, 0.0, 0, time.time()
            while self.live_frame is not None and self.connected:
                now = time.time()
                frame = self.live_frame
                if frame is not last_sent and inflight < LIVE_WINDOW:
                    await self.write(frame)
                    last_sent, last_at, inflight = frame, now, inflight + 1
                elif last_sent and inflight == 0 and now - last_at > LIVE_KEEPALIVE_S:
                    await self.write(last_sent)  # the star leaves stream mode after 2 s
                    last_at, inflight = now, inflight + 1
                b = await self.read_byte(0.015)
                if b == sp.ACK:
                    inflight, acks = max(0, inflight - 1), acks + 1
                elif b == sp.NAK:
                    inflight = max(0, inflight - 1)
                if inflight and now - last_at > LIVE_ACK_TIMEOUT_S:
                    inflight = 0
                if now - window >= 1.0:
                    self.live_fps, acks, window = acks / (now - window), 0, now
                    self.changed()
            if self.connected:
                await self.write(sp.stream_frame(end=True))
                await self._wait_ack(0.8)
                await asyncio.sleep(0.1)
                self.drain()
            self.live_fps = 0.0
        self.changed()

    async def _wait_ack(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            b = await self.read_byte(end - time.time())
            if b == sp.ACK:
                return b
        return None

    # ---- effect upload -------------------------------------------------------------

    async def upload(self, blob, play, progress, command="effect upload"):
        await self.stop_live()
        async with self.lock:
            self.drain()
            await self.write(command.encode() + b"\r\n")
            reply = await self.read_until(b"now...", 8)
            if b"XMODEM" not in reply:
                raise RuntimeError(reply.decode(errors="replace").strip() or "upload refused")
            # Wait for the receiver's 'C'.
            end = time.time() + 10
            while time.time() < end and await self.read_byte(1) != ord("C"):
                pass
            blocks = sp.xmodem_blocks(blob)
            for k, block in enumerate(blocks):
                for _ in range(10):
                    await self.write(block)
                    b = await self.read_byte(5)
                    while b == ord("C"):
                        b = await self.read_byte(5)
                    if b == sp.ACK:
                        break
                    if b == sp.CAN:
                        raise RuntimeError("the star cancelled the upload")
                else:
                    await self.write(bytes([sp.CAN, sp.CAN]))
                    raise RuntimeError("too many retries")
                await progress(min((k + 1) * 1024, len(blob)), len(blob))
            for _ in range(5):
                await self.write(bytes([sp.EOT]))
                if await self.read_byte(3) == sp.ACK:
                    break
            result = (await self.read_until(PROMPT, 15)).decode(errors="replace")
            if "upload OK" not in result:
                raise RuntimeError(result.strip() or "upload failed")
            if play:
                await self.command("effect play flash")
            if command == "effect upload":
                await self._refresh()
        self.changed()

    # ---- effect previews -------------------------------------------------------------

    async def preview(self, effect_id):
        """Preview clip rendered by the star: (step_ms, [[0xRRGGBB] * 66 or 132])."""
        await self.stop_live()
        async with self.lock:
            self.drain()
            await self.write(f"app preview {effect_id}\r\n".encode())
            data = await self.read_until(b"preview end", 40)
            data += await self.read_until(PROMPT, 2)
        head = data.decode("latin-1")
        step = 100
        if " step=" in head:
            step = int(head.split(" step=", 1)[1].split()[0])
        frames = [c for _, c in sp.decode_frames(data)]
        if not frames:
            raise RuntimeError(head.strip()[-80:] or "no preview")
        return step, frames

    # ---- firmware update -------------------------------------------------------------

    async def update_firmware(self, blob, permanent, progress):
        """Upload a signed image, restart into it and reconnect. Returns the new version."""
        await self.upload(blob, False, progress, command="fw upload")
        async with self.lock:
            out = await self.command("fw apply permanent" if permanent else "fw apply")
            if "ok restarting" not in out:
                raise RuntimeError(out or "the star refused the update")
            self.status = "updating"
            self.changed()
            await asyncio.sleep(0.5)
            await self.close()
        # MCUboot checks the signature and swaps the slots, then the
        # firmware starts and the Bluetooth module comes back.
        await asyncio.sleep(15)
        for _ in range(8):
            await self.connect()
            if self.status in ("ready", "locked"):
                return self.info.get("fw", "?")
            await asyncio.sleep(5)
        raise RuntimeError("the star did not come back after the update")

    # ---- name and pairing -------------------------------------------------------------

    async def rename(self, name):
        await self.stop_live()
        async with self.lock:
            out = await self.command(f"star name {name}")
            if "ok renaming" not in out:
                raise RuntimeError(out or "rename refused")
            self.status = "renaming"
            self.changed()
            await asyncio.sleep(0.5)
            await self.close()
        # The module restarts with its new name; reconnect to the same address.
        await asyncio.sleep(5)
        for _ in range(5):
            await self.connect()
            if self.status in ("ready", "locked"):
                return
            await asyncio.sleep(3)


class Studio:
    def __init__(self):
        self.stars = {}
        self.sockets = set()
        self.partner = partner_table()
        self._dirty = asyncio.Event()

    def changed(self):
        self._dirty.set()

    async def broadcast(self, msg):
        text = json.dumps(msg)
        for ws in list(self.sockets):
            try:
                await ws.send_str(text)
            except Exception:
                self.sockets.discard(ws)

    def snapshot(self):
        stars = sorted(self.stars.values(), key=lambda s: (s.status == "nearby", s.name.lower()))
        return {"ev": "stars", "stars": [s.to_json() for s in stars],
                "devKey": star_auth.dev_key() is not None}

    async def publisher(self):
        while True:
            await self._dirty.wait()
            self._dirty.clear()
            await self.broadcast(self.snapshot())
            await asyncio.sleep(0.2)

    async def scan_once(self, seconds=4.0):
        try:
            found = await BleakScanner.discover(timeout=seconds, return_adv=True)
        except Exception as e:
            await self.toast("error", f"Bluetooth scan failed: {e}")
            return
        for d, adv in found.values():
            name = adv.local_name or d.name or ""
            if SERVICE not in [u.lower() for u in adv.service_uuids] and name != FACTORY_NAME:
                continue
            s = self.stars.get(d.address)
            if s is None:
                s = self.stars[d.address] = Star(self, d.address, name, adv.rssi)
            elif name and s.status == "nearby":
                s.name = name
            s.rssi, s.seen = adv.rssi, time.time()
        self.changed()

    async def scanner(self):
        while True:
            await self.scan_once()
            await asyncio.sleep(SCAN_EVERY_S)

    async def poller(self):
        while True:
            await asyncio.sleep(POLL_EVERY_S)
            for s in list(self.stars.values()):
                await s.refresh()

    async def toast(self, level, text):
        await self.broadcast({"ev": "toast", "level": level, "text": text})

    def pick(self, msg):
        addrs = msg.get("addresses") or ([msg["address"]] if msg.get("address") else [])
        return [self.stars[a] for a in addrs if a in self.stars]

    async def handle(self, msg):
        op = msg.get("op")
        stars = self.pick(msg)
        if op == "scan":
            await self.scan_once()
        elif op == "connect":
            await asyncio.gather(*(s.connect(pair=bool(msg.get("pair"))) for s in stars))
            for s in stars:
                if s.status == "locked":
                    await self.toast("info", f"{s.name}: paired to another device. Shake it for "
                                     "5 s (or power it on, if released) and press Pair.")
        elif op == "disconnect":
            for s in stars:
                await s.stop_live()
                await s.close()
                s.status = "nearby"
            self.changed()
        elif op == "cmd":
            async def one(s):
                try:
                    text = await s.run(msg["text"], float(msg.get("timeout", 4)),
                                       refresh=not msg.get("quiet"))
                except Exception as e:
                    text = f"error: {e}"
                await self.broadcast({"ev": "reply", "id": msg.get("id"), "address": s.address,
                                      "name": s.name, "text": text})
            await asyncio.gather(*(one(s) for s in stars if s.status == "ready"))
        elif op == "live":
            for s in stars:
                if s.status == "ready":
                    s.set_live(msg["front"])
        elif op == "live_stop":
            await asyncio.gather(*(s.stop_live() for s in stars))
        elif op == "upload":
            await self.upload(stars, msg)
        elif op == "rename":
            for i, s in enumerate(stars):
                name = msg["names"][i] if msg.get("names") else msg["name"]
                try:
                    await s.rename(name)
                    await self.toast("ok", f"Renamed to {s.name}")
                except Exception as e:
                    await self.toast("error", f"{s.name}: rename failed: {e}")
        elif op == "unpair":
            for s in stars:
                force = " force" if s.info.get("access") == "dev" else ""
                await self.toast("info", f"{s.name}: " + (await s.run("star unpair" + force)))
        elif op == "preview":
            await self.send_preview(stars, msg["id"])
        elif op == "firmware":
            await self.firmware(stars, msg)
        elif op == "pairing_open":
            for s in stars:
                await self.toast("info", f"{s.name}: " + (await s.run("star pairing open")))

    async def send_preview(self, stars, effect_id):
        """Send a preview clip to the browser, from the disk cache or the star."""
        s = next((s for s in stars if s.status == "ready"), None)
        if s is None:
            return
        fw = s.info.get("fw", "0")
        path = PREVIEW_CACHE / fw / f"{effect_id}.json"
        try:
            clip = json.loads(path.read_text())
        except (OSError, ValueError):
            try:
                step, frames = await s.preview(effect_id)
            except Exception as e:
                await self.broadcast({"ev": "preview", "fw": fw, "id": effect_id, "error": str(e)})
                return
            clip = {"step": step, "frames": frames}
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(clip))
        await self.broadcast({"ev": "preview", "fw": fw, "id": effect_id, **clip})

    async def firmware(self, stars, msg):
        blob = base64.b64decode(msg["file"])
        version = sp.image_version(blob)
        if version is None:
            await self.toast("error", "Not a signed firmware image (use zephyr.signed.bin)")
            return
        # One star at a time: each restarts and must come back before the next.
        for s in [s for s in stars if s.status == "ready"]:
            async def progress(done, total, s=s):
                await self.broadcast({"ev": "progress", "address": s.address, "done": done,
                                      "total": total, "label": f"Firmware {version} to {s.name}:"})
            try:
                await self.toast("info", f"{s.name}: updating to {version}…")
                now = await s.update_firmware(blob, bool(msg.get("permanent")), progress)
                ok = now == version.split("+")[0]
                await self.toast("ok" if ok else "error",
                                 f"{s.name}: now running {now}" + ("" if ok else " (the update rolled back)"))
            except Exception as e:
                await self.toast("error", f"{s.name}: firmware update failed: {e}")
            await self.broadcast({"ev": "progress", "address": s.address, "done": -1, "total": 0})

    async def upload(self, stars, msg):
        if msg.get("file"):
            blob = base64.b64decode(msg["file"])
            if blob[:4] != b"CSFX":
                await self.toast("error", "Not a .csfx effect file")
                return
        else:
            frames = [(f, msg["durationMs"]) for f in msg["frames"]]
            blob = sp.csfx(msg.get("name", "PC effect"), frames, self.partner, msg.get("loop", True))

        async def one(s):
            async def progress(done, total):
                await self.broadcast({"ev": "progress", "address": s.address, "done": done, "total": total})
            try:
                t0 = time.time()
                await s.upload(blob, msg.get("play", True), progress)
                await self.toast("ok", f"{s.name}: {len(blob) // 1024} KB uploaded in {time.time() - t0:.0f} s")
            except Exception as e:
                await self.toast("error", f"{s.name}: upload failed: {e}")
            await self.broadcast({"ev": "progress", "address": s.address, "done": -1, "total": 0})
        await asyncio.gather(*(one(s) for s in stars if s.status == "ready"))


def make_app(studio, token):
    web_dir = HERE / "web"

    async def index(request):
        return web.FileResponse(web_dir / "index.html")

    async def ws_handler(request):
        if not secrets.compare_digest(request.query.get("t", ""), token):
            raise web.HTTPForbidden()
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        studio.sockets.add(ws)
        await ws.send_str(json.dumps(studio.snapshot()))
        try:
            async for m in ws:
                if m.type != WSMsgType.TEXT:
                    continue
                msg = json.loads(m.data)
                task = asyncio.create_task(studio.handle(msg))
                task.add_done_callback(lambda t: t.exception() and print("op failed:", t.exception()))
        finally:
            studio.sockets.discard(ws)
        return ws

    app = web.Application(client_max_size=64 * 1024 * 1024)
    app.router.add_get("/", index)
    app.router.add_get("/ws", ws_handler)
    app.router.add_static("/", web_dir)
    return app


async def main_async(port, open_browser):
    studio = Studio()
    token = secrets.token_urlsafe(16)
    runner = web.AppRunner(make_app(studio, token))
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", port)
    await site.start()
    url = f"http://127.0.0.1:{port}/?t={token}"
    print(f"Star Studio: {url}\nPress Ctrl+C to quit.")
    if star_auth.dev_key() is None:
        print("No developer key (sw/dev_key.txt): only stars paired to this PC can be controlled.")
    if open_browser:
        webbrowser.open(url)
    await asyncio.gather(studio.publisher(), studio.scanner(), studio.poller())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8642)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    try:
        asyncio.run(main_async(args.port, not args.no_browser))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
