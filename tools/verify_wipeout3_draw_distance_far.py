#!/usr/bin/env python3
"""Verify the authenticated complete Draw Distance selector and native plugin."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys
import tomllib


STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
PATCHED_SHA256 = "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"
LOAD_ADDRESS = 0x80010000
EXE_HEADER = 0x800
HOOK_SITE = 0x801422F4
PLUGIN_ID = "wipeout3.enhancement.draw-distance-far"


def word(image: bytes, address: int) -> int:
    offset = EXE_HEADER + address - LOAD_ADDRESS
    return struct.unpack_from("<I", image, offset)[0]


def verify(root: Path, exe_path: Path) -> list[str]:
    errors: list[str] = []
    stock = exe_path.read_bytes()
    if hashlib.sha256(stock).hexdigest() != STOCK_SHA256:
        errors.append("stock executable SHA-256 mismatch")
        return errors
    if word(stock, HOOK_SITE) != 0x03E00008:
        errors.append("hook site is not the authenticated stock `jr ra`")
    if word(stock, HOOK_SITE + 4) != 0x27BD0028:
        errors.append("hook delay slot is not authenticated `addiu sp,sp,0x28`")

    # The pinned Hue executable is cleanroom evidence only. Release hygiene
    # intentionally deletes every installable legacy AOT/overlay package, so
    # this verifier must never reconstruct it from a shipping patch table.
    reference = (root / "aot/SCES_028.45").read_bytes()
    if hashlib.sha256(reference).hexdigest() != PATCHED_SHA256:
        errors.append("reconstructed Hueponik PAL executable digest mismatch")
    # The authenticated menu descriptor contains the complete five-profile
    # table in menu order: LOW, STOCK, NEAR, MID, FAR.
    profiles_offset = EXE_HEADER + 0x800D2390 - LOAD_ADDRESS
    profiles = tuple(
        struct.unpack_from("<10H", reference, profiles_offset + 20 * i)
        for i in range(5)
    )
    expected_profiles = (
        (0x3E80, 0, 0x36B0, 0, 0x2EE0, 0, 2, 0, 1, 0),
        (0x9C40, 0, 0x84D0, 0, 0x7D00, 0, 1, 0, 1, 0),
        (0x9C40, 0, 0x84D0, 0, 0x7D00, 0, 0, 0, 1, 0),
        (0xDAC0, 0, 0xBB80, 0, 0xABE0, 0, 0, 0, 1, 0),
        (0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 1, 0),
    )
    if profiles != expected_profiles:
        errors.append(f"Hueponik DRAW DIST descriptors changed: {profiles!r}")

    source = (root / "src/mods/wipeout3_draw_distance_far_plugin.c").read_text(
        encoding="utf-8"
    )
    for required in (
        "WIP3_DRAW_FAR_VERTICAL       = 0x7FFEu",
        "WIP3_SCENE_MASK_OWNER_RETURN = 0x801422F4u",
        "{16000u, 14000u, 12000u, WIP3_DRAW_MODE_GEOMETRY_ONLY}",
        "{40000u, 34000u, 32000u, WIP3_DRAW_MODE_STOCK}",
        "{40000u, 34000u, 32000u, WIP3_DRAW_MODE_UNION}",
        "{56000u, 48000u, 44000u, WIP3_DRAW_MODE_UNION}",
        "{65535u, 65535u, 65535u, WIP3_DRAW_MODE_UNION}",
        "cpu->gpr[29] += 0x28u",
        "return continuation",
    ):
        if required not in source:
            errors.append(f"native plugin is missing recovered contract: {required}")
    if re.search(r"0x805F|0x8060|psx_mod_alloc_guest_memory", source, re.I):
        errors.append("native plugin must not depend on Hueponik high RAM")
    if re.search(r"WIP3_(?:TRACK_4|4X4)|0x805FF7(?:C4|D4)", source, re.I):
        errors.append("native plugin appears to own Track 4x4 state")

    manifest_path = (
        root
        / "mods/preloaded/packages/wipeout3.enhancement.draw-distance-far"
        / "1.0.0/manifest.toml"
    )
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("id") != PLUGIN_ID:
        errors.append("package id mismatch")
    targets = manifest.get("target", [])
    if len(targets) != 1 or targets[0].get("exe_sha256") != STOCK_SHA256:
        errors.append("package does not pin the authenticated stock executable")
    features = manifest.get("feature", [])
    if len(features) != 1 or features[0].get("default_enabled") is not True:
        errors.append("Draw Distance must be default-enabled")
    options = [o for o in manifest.get("option", []) if o.get("id") == "distance"]
    choices = ()
    if len(options) == 1:
        choices = tuple(c.get("value") for c in options[0].get("choice", []))
    if len(options) != 1 or options[0].get("default") != "far" or choices != (
        "low", "stock", "near", "mid", "far"
    ):
        errors.append("Draw Distance option/default/choice list mismatch")
    plugins = manifest.get("plugin", [])
    if len(plugins) != 1 or plugins[0].get("id") != PLUGIN_ID:
        errors.append("manifest/native plugin id mismatch")

    state = tomllib.loads((root / "mods/preloaded/state.toml").read_text("utf-8"))
    selected = [
        f for f in state.get("feature", [])
        if f.get("package_id") == PLUGIN_ID and f.get("id") == "draw-distance-far"
    ]
    if len(selected) != 1 or selected[0].get("enabled") is not True or \
            selected[0].get("values") != {"distance": "far"}:
        errors.append("preloaded Draw Distance selection must be enabled Far")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--exe", type=Path, default=Path("disc/SCES_028.45"))
    args = parser.parse_args()
    root = args.root.resolve()
    exe = args.exe if args.exe.is_absolute() else root / args.exe
    errors = verify(root, exe.resolve())
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(
        "PASS: stock JR guard, authenticated Hueponik Low/Stock/Near/Mid/Far "
        "profiles, native no-high-RAM selector, and default Far wiring"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
