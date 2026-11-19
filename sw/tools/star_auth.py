#!/usr/bin/env python3
# Copyright (c) 2024-2026 Tuomas Terho
# SPDX-License-Identifier: MIT
"""Star pairing and authentication (firmware protocol 2).

The star's Bluetooth shell is locked until the client proves a key:

    app hello            -> ok proto=2 ... owner=free|paired pairing=open|closed
                            access=none nonce=<32 hex> name=<name>
    app auth <64 hex>    -> ok access=owner|dev   (HMAC-SHA256(key, "auth" + nonce))
    app pair <32 hex>    -> ok paired             (only while the pairing window is open)

Keys:
  developer key  sw/dev_key.txt (built into the firmware; every star accepts it)
  PC owner key   ~/.christmas-star/pc_key (made on first use), for stars paired to this PC
"""
import hashlib
import hmac
import os
import secrets
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEV_KEY_FILE = HERE.parent / "dev_key.txt"
PC_KEY_FILE = Path.home() / ".christmas-star" / "pc_key"


def _read_key(path):
    try:
        text = Path(path).read_text().strip()
    except OSError:
        return None
    key = bytes.fromhex(text)
    if len(key) != 16 or not any(key):
        raise ValueError(f"{path}: expected 32 hex digits")
    return key


def dev_key():
    return _read_key(os.environ.get("STAR_DEV_KEY_FILE", DEV_KEY_FILE))


def pc_key(create=True):
    key = _read_key(PC_KEY_FILE)
    if key is None and create:
        PC_KEY_FILE.parent.mkdir(parents=True, exist_ok=True)
        key = secrets.token_bytes(16)
        PC_KEY_FILE.write_text(key.hex())
    return key


def auth_mac(key, nonce):
    return hmac.new(key, b"auth" + nonce, hashlib.sha256).hexdigest()


def parse_hello(line):
    """Parse an "ok proto=..." line into a dict; name= is the rest of the line."""
    line = line.strip()
    if not line.startswith("ok "):
        return None
    head, sep, name = line.partition(" name=")
    out = {}
    for item in head[3:].split():
        k, _, v = item.partition("=")
        out[k] = v
    out["name"] = name if sep else ""
    out["proto"] = int(out.get("proto", "1"))
    return out


class AuthError(Exception):
    pass


def _line(link, timeout=3.0):
    """Read one reply line (the locked shell answers with a bare line)."""
    raw = link.read_until(b"\n", timeout).decode(errors="replace")
    lines = [ln.strip() for ln in raw.replace("\r", "").split("\n")]
    # After a successful auth the shell prompt can follow in the same chunk.
    lines = [ln for ln in lines if ln and not ln.startswith("star:~$")]
    return lines[0] if lines else ""


def hello(link):
    link.reset_input_buffer()
    link.write(b"app hello\r\n")
    for _ in range(3):
        line = _line(link)
        info = parse_hello(line)
        if info is not None:
            return info
    raise AuthError("the star did not answer 'app hello'")


def login(link, keys=None, pair=False):
    """Unlock the star's shell. Returns the hello info with 'access' updated.

    keys: list of 16-byte keys to try (default: developer key, then PC key).
    pair: if the pairing window is open and no key works, pair this PC.
    """
    info = hello(link)
    if info["proto"] < 2:
        info["access"] = "open"  # firmware without pairing
        return info
    if info.get("access") not in (None, "none"):
        return info  # already unlocked (same connection)
    if keys is None:
        keys = [k for k in (dev_key(), pc_key(create=pair)) if k]
    for i, key in enumerate(keys):
        if i > 0:
            info = hello(link)
        link.write(f"app auth {auth_mac(key, bytes.fromhex(info['nonce']))}\r\n".encode())
        reply = _line(link)
        if reply.startswith("ok access="):
            info["access"] = reply.split("=", 1)[1]
            return info
    if pair and info.get("pairing") == "open":
        link.write(f"app pair {pc_key().hex()}\r\n".encode())
        reply = _line(link)
        if reply.startswith("ok paired"):
            info["access"] = "owner"
            info["owner"] = "paired"
            return info
        raise AuthError(f"pairing refused: {reply}")
    raise AuthError("no key accepted (pair the star first: shake it for 5 s or power it on, "
                    "then use --pair)" if info.get("pairing") != "open" else
                    "no key accepted; the pairing window is open: use --pair")
