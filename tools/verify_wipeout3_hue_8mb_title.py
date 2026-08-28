#!/usr/bin/env python3
"""Verify the exact direct-native Hueponik 8 MB title handshake."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys
import tomllib


LOAD_ADDRESS = 0x80010000
HEADER_SIZE = 0x800
STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
HUE_SHA256 = "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"
PACKAGE_ID = "wipeout3.enhancement.hue-8mb-title"
WORDS = {
    0x8014BAD0: (0x8C449804, 0x24040001),
    0x8015FA80: (0x00052BC0, 0x00052D40),
}


def word(image: bytes, address: int) -> int:
    offset = HEADER_SIZE + address - LOAD_ADDRESS
    if offset < HEADER_SIZE or offset + 4 > len(image):
        raise ValueError(f"0x{address:08X} outside executable")
    return struct.unpack_from("<I", image, offset)[0]


def verify(root: Path, stock_path: Path, hue_path: Path) -> list[str]:
    errors: list[str] = []
    stock = stock_path.read_bytes()
    hue = hue_path.read_bytes()
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA256:
        errors.append("stock executable SHA-256 mismatch")
    if hashlib.sha256(hue).hexdigest() != HUE_SHA256:
        errors.append("Hueponik executable SHA-256 mismatch")
    for address, (expected_stock, expected_hue) in WORDS.items():
        if word(stock, address) != expected_stock:
            errors.append(f"0x{address:08X}: stock word mismatch")
        if word(hue, address) != expected_hue:
            errors.append(f"0x{address:08X}: Hueponik word mismatch")

    source = (root / "src/mods/wipeout3_hue_8mb_title_plugin.c").read_text(
        encoding="utf-8"
    )
    declared = {
        int(value, 16)
        for value in re.findall(r"WIP3_8MB_[A-Z_]+\s*=\s*(0x[0-9A-Fa-f]+)u", source)
    }
    if declared != set(WORDS):
        errors.append(f"native site set mismatch: {sorted(declared)}")
    if source.count("psx_mod_register_instruction_site_plugin(") != 1:
        errors.append("plugin must register one guarded site table")
    forbidden = ("0x8078", "8017B8", "TRACK_4X4", "write_word(", "write_half(")
    for token in forbidden:
        if token in source:
            errors.append(f"forbidden menu/high-RAM/code-write token in plugin: {token}")

    game = tomllib.loads((root / "game.toml").read_text(encoding="utf-8"))
    sites = {int(value, 0) for value in game["recompiler"]["mod_instruction_sites"]}
    if not set(WORDS).issubset(sites):
        errors.append("game.toml does not expose both native instruction sites")
    if game["runtime"].get("main_ram_mib") != 8:
        errors.append("runtime main_ram_mib is not 8")
    if game["recompiler"].get("patch"):
        errors.append("generated AOT patches are forbidden")

    manifest_path = root / (
        "mods/preloaded/packages/wipeout3.enhancement.hue-8mb-title/"
        "1.0.0/manifest.toml"
    )
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("id") != PACKAGE_ID:
        errors.append("manifest package id mismatch")
    dependencies = manifest.get("dependency", [])
    if not any(dep.get("id") == "psx.enhancement.8mb-ram" for dep in dependencies):
        errors.append("manifest lacks framework 8 MB dependency")
    features = manifest.get("feature", [])
    if len(features) != 1 or features[0].get("default_enabled") is not True:
        errors.append("native 8 MB title feature is not default-enabled")
    plugins = manifest.get("plugin", [])
    if len(plugins) != 1 or plugins[0].get("id") != PACKAGE_ID:
        errors.append("manifest plugin id mismatch")

    state = tomllib.loads((root / "mods/preloaded/state.toml").read_text(encoding="utf-8"))
    if not any(p.get("id") == PACKAGE_ID for p in state.get("package", [])):
        errors.append("preloaded state omits native 8 MB title package")
    if not any(
        f.get("package_id") == PACKAGE_ID
        and f.get("id") == "hue-8mb-title"
        and f.get("enabled") is True
        for f in state.get("feature", [])
    ):
        errors.append("preloaded state does not enable native 8 MB title feature")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--stock", type=Path, default=Path("disc/SCES_028.45"))
    parser.add_argument("--hue", type=Path, default=Path("aot/SCES_028.45"))
    args = parser.parse_args()
    root = args.root.resolve()
    stock = args.stock if args.stock.is_absolute() else root / args.stock
    hue = args.hue if args.hue.is_absolute() else root / args.hue
    errors = verify(root, stock.resolve(), hue.resolve())
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "PASS: authenticated Hueponik 8 MB handshake, two guarded native sites, "
        "host 8 MB dependency, no AOT/menu/high-RAM/Track 4x4"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
