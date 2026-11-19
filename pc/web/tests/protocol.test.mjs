// Copyright (c) 2024-2026 Tuomas Terho
// SPDX-License-Identifier: MIT
//
// Unit tests for js/protocol.js:  node --test pc/web/tests
// The expected bytes come from pc/star_protocol.py, so both stay in step.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import * as sp from "../js/protocol.js";
import { StarLayout } from "../star_layout.js";

const hex = sp.hex;

test("CRC-16/XMODEM and CRC-32 check values", () => {
  assert.equal(sp.crc16Xmodem(new TextEncoder().encode("123456789")), 0x31c3);
  assert.equal(sp.crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("stream frames", () => {
  assert.equal(hex(sp.streamFrame([0xff0000, 0x00ff00, 0x0000ff, 0x123456])), "a55a010400f8e0071f00aa119c9f");
  assert.equal(hex(sp.streamFrame(null, true)), "a55a80001b98");
  // Decoding finds the frame among other bytes and expands RGB565.
  const buf = sp.concat(new TextEncoder().encode("preview x\r\n"), sp.streamFrame([0xffffff, 0x000000]));
  const [f] = sp.decodeFrames(buf);
  assert.deepEqual(f.colors, [0xffffff, 0x000000]);
});

test("csfx files match the Python builder", () => {
  const front = Array.from({ length: 66 }, (_, i) => (i * 0x030507) & 0xffffff);
  const blob = sp.csfx("Test", [{ front, ms: 40 }, { front: new Array(66).fill(0xabcdef), ms: 100 }], StarLayout.partner, true);
  assert.equal(blob.length, 828);
  assert.equal(hex(blob.subarray(0, 32)), "43534658018401005465737400000000000000000000000002000000b1e63d7a");
  assert.equal(sp.crc32(blob), 0xbf158b53);
});

test("XMODEM-1K blocks", () => {
  const data = new TextEncoder().encode("hello star".repeat(120));
  const blocks = sp.xmodemBlocks(data);
  assert.equal(blocks.length, 2);
  assert.equal(hex(blocks[0].subarray(0, 3)), "0201fe");
  assert.equal(hex(blocks[1].subarray(0, 3)), "0202fd");
  assert.equal(hex(blocks[1].subarray(-2)), "b6ff");
  assert.equal(sp.crc32(sp.concat(...blocks)), 0x42b173ab);
});

test("the app's bundled firmware is a signed image with a version", () => {
  const bin = new Uint8Array(readFileSync(new URL("../../../android/app/src/main/assets/firmware/star-firmware.bin", import.meta.url)));
  assert.match(sp.imageVersion(bin), /^\d+\.\d+\.\d+/);
  assert.equal(sp.imageVersion(new Uint8Array(64)), null);
});

test("shell lines", () => {
  const hello = sp.parseHello("ok proto=4 leds=132 fw=1.6.1 owner=paired pairing=open access=none nonce=00112233445566778899aabbccddeeff name=Living room");
  assert.equal(hello.proto, 4);
  assert.equal(hello.fw, "1.6.1");
  assert.equal(hello.pairing, "open");
  assert.equal(hello.name, "Living room");
  assert.equal(sp.parseHello("locked"), null);

  const st = sp.parseState("state effect=golden bright=169 shake=1 speed=119 fps=50 missing=1 cap=655040 flash=My video 2 frames=161 owner=paired name=Living room\r\nstar:~$ ");
  assert.equal(st.effect, "golden");
  assert.equal(st.flash, "My video 2");
  assert.equal(st.frames, "161");
  assert.equal(st.name, "Living room");
  assert.equal(sp.parseState("state effect=off flash=- frames=0 name=Star").flash, "");

  const fx = sp.parseEffects("fx golden - Golden Star|Warm gold\r\nfx liquidgold t Liquid Gold|Flows\r\nfx hearthfrost 2 Hearth & Frost|Two faces\r\nstar:~$ ");
  assert.deepEqual(fx.map((e) => e.id), ["golden", "liquidgold", "hearthfrost"]);
  assert.equal(fx[1].tilt, true);
  assert.equal(fx[2].twoFaces, true);
  assert.equal(fx[2].title, "Hearth & Frost");
});

test("keys and the login answer (vector shared with the firmware and the app)", async () => {
  assert.equal(sp.parseKey("00 01 02 03-04050607 08090a0b0c0d0e0f").length, 16);
  assert.equal(sp.parseKey("00".repeat(16)), null, "an all-zero key is never accepted");
  assert.equal(sp.parseKey("123"), null);
  const key = Uint8Array.from({ length: 16 }, (_, i) => i);
  const nonce = hex(Uint8Array.from({ length: 16 }, (_, i) => 0x10 + i));
  assert.equal(await sp.authMac(key, nonce), "8b9b4fe50baada231361d9491a71d3ef7ae699bc95c7366a34c5784d99370d9a");
});
