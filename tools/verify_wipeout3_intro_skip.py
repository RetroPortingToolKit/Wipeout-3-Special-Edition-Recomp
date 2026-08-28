#!/usr/bin/env python3
"""Verify the stock SCES-02845 native intro-presentation bypass."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import struct
import sys
import tomllib


EXE_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
LOAD_ADDRESS = 0x80010000
PSX_EXE_HEADER_SIZE = 0x800
INTRO_PLAYER = 0x80115F34
JAL_INTRO_PLAYER = 0x0C000000 | ((INTRO_PLAYER >> 2) & 0x03FFFFFF)
INTRO_SITES = {
    0x80100390: 0x24040003,  # addiu a0,zero,3 -- CURLY44
    0x80100398: 0x24040004,  # addiu a0,zero,4 -- CURLY44B
    0x801163C0: 0x24040002,  # addiu a0,zero,2 -- PSYG
    0x801163C8: 0x00002021,  # move a0,zero     -- PSYGLEED
    0x801163D0: 0x24040001,  # addiu a0,zero,1 -- WO3
}

# These calls only scan the five STR files and fill the LBA tables. They are
# deliberately verified as stock and deliberately absent from the hook list.
INTRO_FILE_SCAN = 0x80118EA4
JAL_INTRO_FILE_SCAN = 0x0C000000 | ((INTRO_FILE_SCAN >> 2) & 0x03FFFFFF)
FILE_SCAN_SITES = {
    0x801162CC: 0x02003021,
    0x801162E0: 0x26060004,
    0x801162F4: 0x26060008,
    0x80116308: 0x2606000C,
    0x8011631C: 0x26060010,
}
PLUGIN_ID = "wipeout3.convenience.skip-intro"


def _word(image: bytes, address: int) -> int:
    offset = PSX_EXE_HEADER_SIZE + address - LOAD_ADDRESS
    if offset < PSX_EXE_HEADER_SIZE or offset + 4 > len(image):
        raise ValueError(f"address 0x{address:08X} is outside the PS-X EXE")
    return struct.unpack_from("<I", image, offset)[0]


def _all_jal_sites(image: bytes, jal_word: int) -> set[int]:
    payload = image[PSX_EXE_HEADER_SIZE:]
    return {
        LOAD_ADDRESS + offset
        for offset in range(0, len(payload) - 3, 4)
        if struct.unpack_from("<I", payload, offset)[0] == jal_word
    }


def verify(root: Path, exe: Path) -> list[str]:
    errors: list[str] = []
    image = exe.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != EXE_SHA256:
        errors.append(f"stock EXE SHA-256 mismatch: {digest}")

    actual_player_sites = _all_jal_sites(image, JAL_INTRO_PLAYER)
    if actual_player_sites != set(INTRO_SITES):
        errors.append(
            "intro-player direct-call set differs from the five authenticated "
            f"sites: {[f'0x{x:08X}' for x in sorted(actual_player_sites)]}"
        )
    for site, delay_word in INTRO_SITES.items():
        if _word(image, site + 4) != delay_word:
            errors.append(
                f"0x{site + 4:08X}: expected player delay word "
                f"0x{delay_word:08X}, found 0x{_word(image, site + 4):08X}"
            )

    actual_scan_sites = _all_jal_sites(image, JAL_INTRO_FILE_SCAN)
    if actual_scan_sites != set(FILE_SCAN_SITES):
        errors.append(
            "intro file-scan direct-call set differs from the authenticated "
            f"sites: {[f'0x{x:08X}' for x in sorted(actual_scan_sites)]}"
        )
    for site, delay_word in FILE_SCAN_SITES.items():
        if _word(image, site + 4) != delay_word:
            errors.append(
                f"0x{site + 4:08X}: expected file-scan delay word "
                f"0x{delay_word:08X}, found 0x{_word(image, site + 4):08X}"
            )

    source_path = root / "src/mods/wipeout3_intro_skip_plugin.c"
    source = source_path.read_text(encoding="utf-8")
    declared = {
        int(value, 16)
        for value in re.findall(
            r"WIP3_INTRO_PLAY_[A-Z0-9_]+\s*=\s*(0x[0-9A-Fa-f]+)u", source
        )
    }
    if declared != set(INTRO_SITES):
        errors.append(
            "plugin site set differs from authenticated player-call set: "
            f"{[f'0x{x:08X}' for x in sorted(declared)]}"
        )
    if any(f"0x{site:08X}u" in source for site in FILE_SCAN_SITES):
        errors.append("plugin hooks an intro file-scan/LBA initialization call")
    if "psx_mod_set_auto_skip_fmv" in source:
        errors.append("intro plugin uses global runtime FMV suppression")
    if "cpu->gpr[4] = index" not in source:
        errors.append("plugin does not emulate each JAL delay-slot argument")
    if "cpu->gpr[31] = address + 8u" not in source:
        errors.append("plugin does not emulate the architectural JAL link")
    if "return address + 8u" not in source:
        errors.append("plugin does not resume after the JAL and delay slot")

    config = tomllib.loads((root / "game.toml").read_text(encoding="utf-8"))
    configured = {
        int(value, 0) if isinstance(value, str) else int(value)
        for value in config.get("recompiler", {}).get("mod_instruction_sites", [])
    }
    missing = set(INTRO_SITES) - configured
    if missing:
        errors.append(
            "game.toml lacks intro-player sites: "
            f"{[f'0x{x:08X}' for x in sorted(missing)]}"
        )
    hooked_scans = set(FILE_SCAN_SITES) & configured
    if hooked_scans:
        errors.append(
            "game.toml hooks file-scan/LBA sites: "
            f"{[f'0x{x:08X}' for x in sorted(hooked_scans)]}"
        )

    manifest_path = (
        root
        / "mods/preloaded/packages/wipeout3.convenience.skip-intro/1.0.0/manifest.toml"
    )
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    targets = manifest.get("target", [])
    features = manifest.get("feature", [])
    plugins = manifest.get("plugin", [])
    if manifest.get("id") != PLUGIN_ID:
        errors.append("manifest package id mismatch")
    if len(targets) != 1 or targets[0].get("exe_sha256") != EXE_SHA256:
        errors.append("manifest does not pin the authenticated stock EXE")
    if len(features) != 1 or features[0].get("default_enabled") is not True:
        errors.append("Always Skip Intro must be enabled by default")
    if len(plugins) != 1 or plugins[0].get("id") != PLUGIN_ID:
        errors.append("manifest plugin id does not match the static implementation")
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
        "PASS: all five intro-player JALs are native-skipped with delay/link "
        "semantics; all five file-scan/LBA calls remain stock and unhooked."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
