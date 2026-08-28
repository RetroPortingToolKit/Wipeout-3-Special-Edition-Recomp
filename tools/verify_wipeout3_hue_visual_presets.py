#!/usr/bin/env python3
"""Verify direct-native Hueponik visual selectors and stock-text invariants."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import tomllib


STOCK_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
LOAD_ADDRESS = 0x80010000
EXE_HEADER = 0x800

STOCK_WORDS = {
    0x80130A20: 0x3C040015,
    0x80130A24: 0x3484F8FF,
    0x80130A0C: 0x3C020080,
    0x80130A10: 0x3442538F,
    0x801308E4: 0x28420BB9,
    0x801308FC: 0x28420BB9,
    0x8013085C: 0x3C110089,
    0x8013086C: 0x36315440,
    0x80152974: 0x3C020080,
    0x80152978: 0x3442538F,
    0x800FA224: 0x8E020010,
}

HOOK_SITES = {
    0x80130A20,
    0x80130A0C,
    0x801308E4,
    0x801308FC,
    0x8013085C,
    0x8013086C,
    0x80152974,
    0x800FA224,
}

PAIR_CONTINUATIONS = {
    0x80130A14,
    0x80130A28,
    0x8015297C,
}

LOD_CHOICES = (
    "0", "600", "stock", "1800", "2400", "3000", "3600", "4200",
    "4800", "5400", "6000", "6600", "7200", "7800", "8400",
    "9000", "9600", "10200", "10800", "11400", "12000", "max",
)


def word(image: bytes, address: int) -> int:
    offset = EXE_HEADER + address - LOAD_ADDRESS
    return struct.unpack_from("<I", image, offset)[0]


def manifest_choices(manifest: dict, option_id: str) -> tuple[str, ...]:
    options = [o for o in manifest.get("option", []) if o.get("id") == option_id]
    if len(options) != 1:
        return ()
    return tuple(choice["value"] for choice in options[0].get("choice", []))


def load_manifest(root: Path, package_id: str) -> dict:
    path = root / "mods/preloaded/packages" / package_id / "1.0.0/manifest.toml"
    return tomllib.loads(path.read_text(encoding="utf-8"))


def verify(root: Path) -> list[str]:
    errors: list[str] = []
    exe = (root / "disc/SCES_028.45").read_bytes()
    if hashlib.sha256(exe).hexdigest() != STOCK_SHA256:
        return ["stock executable SHA-256 mismatch"]
    for address, expected in STOCK_WORDS.items():
        if word(exe, address) != expected:
            errors.append(f"stock word mismatch at 0x{address:08X}")

    config = (root / "game.toml").read_text(encoding="utf-8")
    if "[[recompiler.patch]]" in config:
        errors.append("game.toml still contains generated AOT word replacements")
    for address in HOOK_SITES:
        if f'"0x{address:08X}"' not in config:
            errors.append(f"missing native instruction site 0x{address:08X}")
    if "pgxp_cpu_mode = false" not in config or "pgxp_depth = false" not in config:
        errors.append("direct visual selectors require the shipping PGXP-off invariant")

    seeds = {
        int(line, 16)
        for line in (root / "seeds/ghidra_funcs.txt").read_text(
            encoding="utf-8"
        ).splitlines()
        if line.startswith("0x")
    }
    for address in sorted(PAIR_CONTINUATIONS):
        if address not in seeds:
            errors.append(
                f"missing native two-word continuation seed 0x{address:08X}"
            )

    source = (root / "src/mods/wipeout3_hue_visual_presets_plugin.c").read_text(
        encoding="utf-8"
    )
    required = (
        "0x7FFFFFFFu", "0x03FFC001u", "0x1FFFu", "0x0080538Fu",
        "0x00895440u", "0x017D7840u", "0x0284AF10u", "0x1964u",
        "0x7FFF0000u", "0x00003000u", "return address + 8u",
    )
    for token in required:
        if token not in source:
            errors.append(f"native selector source missing exact contract: {token}")
    if source.count("WIP3_SKIPPED_ORI_STEP(cpu, 4u);") != 1:
        errors.append("LOD pair must retire exactly one skipped a0 ORI")
    if source.count("WIP3_SKIPPED_ORI_STEP(cpu, 2u);") != 1:
        errors.append("two Ship Distance pairs must share one v0 ORI retire site")
    if re.search(r"0x80(?:5F|60|78)[0-9A-Fa-f]{4}", source):
        errors.append("native selectors reference Hueponik replacement-engine RAM")
    if re.search(r"psx_mod_(?:write_code_word|alloc_guest_memory)", source):
        errors.append("native selectors write code or allocate a guest engine")
    for address in (0x805FF7C4, 0x805FF7C6, 0x805FF7D4, 0x805FF7D6):
        if f"0x{address:08X}" in source:
            errors.append("Track 4x4 state leaked into native selectors")

    packages = {
        "wipeout3.enhancement.ship-lod": ("ship-lod", "distance", "max", LOD_CHOICES),
        "wipeout3.enhancement.ship-distance": (
            "ship-distance", "distance", "65k",
            ("stock", "24k", "40k", "52k", "65k"),
        ),
        "wipeout3.enhancement.birds": (
            "birds", "detail", "high", ("low", "stock", "high"),
        ),
    }
    for package_id, (feature_id, option_id, default, choices) in packages.items():
        manifest = load_manifest(root, package_id)
        if manifest.get("id") != package_id:
            errors.append(f"{package_id}: package id mismatch")
            continue
        targets = manifest.get("target", [])
        if len(targets) != 1 or targets[0].get("exe_sha256") != STOCK_SHA256:
            errors.append(f"{package_id}: stock executable is not pinned")
        features = manifest.get("feature", [])
        if len(features) != 1 or features[0].get("id") != feature_id or \
                features[0].get("default_enabled") is not True:
            errors.append(f"{package_id}: default feature wiring mismatch")
        options = [o for o in manifest.get("option", []) if o.get("id") == option_id]
        if len(options) != 1 or options[0].get("default") != default:
            errors.append(f"{package_id}: option default must be {default}")
        if manifest_choices(manifest, option_id) != choices:
            errors.append(f"{package_id}: authentic choice list mismatch")
        plugins = manifest.get("plugin", [])
        if len(plugins) != 1 or plugins[0].get("id") != package_id:
            errors.append(f"{package_id}: native plugin id mismatch")

    state = tomllib.loads((root / "mods/preloaded/state.toml").read_text("utf-8"))
    expected_values = {
        ("wipeout3.enhancement.ship-lod", "ship-lod"): {"distance": "max"},
        ("wipeout3.enhancement.ship-distance", "ship-distance"): {"distance": "65k"},
        ("wipeout3.enhancement.birds", "birds"): {"detail": "high"},
    }
    selected = {
        (item["package_id"], item["id"]): item
        for item in state.get("feature", [])
    }
    for key, values in expected_values.items():
        item = selected.get(key)
        if not item or item.get("enabled") is not True or item.get("values") != values:
            errors.append(f"preloaded target selection mismatch for {key[0]}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors = verify(args.root.resolve())
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: authentic LOD/Distance/Birds choices, Max/65K/High defaults, ")
    print("      eight guarded native sites, no AOT patches/high-RAM/menu/Track 4x4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
