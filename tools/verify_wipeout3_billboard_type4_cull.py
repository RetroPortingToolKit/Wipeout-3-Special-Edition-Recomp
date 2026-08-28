#!/usr/bin/env python3
"""Verify the widescreen type-4 billboard guard is native and stock-safe."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import tomllib


STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
LOAD_ADDRESS = 0x80010000
EXE_HEADER = 0x800
SITES = {
    0x8013AD58: 0x00000000,
    0x8013AD6C: 0x3C031F80,
    0x8013AD70: 0x8C630010,
    0x8013AD74: 0x00000000,
}


def word(image: bytes, address: int) -> int:
    return struct.unpack_from("<I", image, EXE_HEADER + address - LOAD_ADDRESS)[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    errors: list[str] = []
    exe = (root / "disc/SCES_028.45").read_bytes()
    if hashlib.sha256(exe).hexdigest() != STOCK_SHA256:
        errors.append("stock executable SHA-256 mismatch")
    else:
        for address, expected in SITES.items():
            if word(exe, address) != expected:
                errors.append(f"stock word mismatch at 0x{address:08X}")
    config = (root / "game.toml").read_text(encoding="utf-8")
    if "[[recompiler.patch]]" in config:
        errors.append("generated AOT patches remain enabled")
    for address in SITES:
        if f'"0x{address:08X}"' not in config:
            errors.append(f"missing billboard site 0x{address:08X}")
    source = (root / "src/mods/wipeout3_billboard_type4_cull_plugin.c").read_text(
        encoding="utf-8"
    )
    for token in (
        "gte_read_ctrl(cpu, 31u)", "0x80460000u", "cpu->gpr[13] &= cpu->gpr[3]",
        "cpu->gpr[13] < 1u", "cpu->read_word(address)",
    ):
        if token not in source:
            errors.append(f"native billboard guard missing: {token}")
    manifest = tomllib.loads((
        root / "mods/preloaded/packages/wipeout3.enhancement.widescreen/1.0.0/manifest.toml"
    ).read_text(encoding="utf-8"))
    plugins = {p.get("id") for p in manifest.get("plugin", [])}
    if "wipeout3.widescreen.billboard-type4-cull" not in plugins:
        errors.append("billboard guard is not tied to the widescreen feature")
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: four stock words, four native sites, exact sibling FLAG mask, no AOT patch")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
