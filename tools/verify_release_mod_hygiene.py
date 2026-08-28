#!/usr/bin/env python3
"""Fail closed if a release can stage legacy Hueponik patch machinery.

The shipping WipEout 3 catalog is intentionally declarative and plugin-only.
Hueponik's published binaries remain an analysis oracle, not release payloads.
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from pathlib import Path
from typing import Any


LEGACY_CATALOGS = (
    "wipeout3.enhancement.framerate/1.0.14",
    "wipeout3.enhancement.framerate-120/1.0.0",
    "wipeout3.enhancement.framerate-pal100/1.0.0",
    "wipeout3.enhancement.framerate-pal100/1.1.0",
)
RETIRED_SOURCES = (
    "src/mods/wipeout3_framerate_30_plugin.c",
    "src/mods/wipeout3_framerate_120_plugin.c",
    "tools/gen_hueponik_framerate_mod.py",
)
FORBIDDEN_PAYLOAD_SUFFIXES = {
    ".overlay",
    ".xdelta",
    ".xdelta3",
    ".vcdiff",
    ".ppf",
}
FORBIDDEN_TABLE_KEYS = {
    "overlay",
    "overlays",
    "patch",
    "patches",
    "aot_patch",
    "aot_patches",
    "memory_patch",
    "memory_patches",
}
ADDRESS_KEYS = {
    "address",
    "addr",
    "base_address",
    "destination",
    "end_address",
    "load_address",
    "start_address",
}
SEMANTIC_TEXT_KEYS = {
    "feature",
    "file",
    "id",
    "label",
    "name",
    "path",
    "plugin",
    "source",
    "value",
}
HIGH_RAM_START = 0x80200000
HIGH_RAM_END = 0x80800000


def _normalized(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.casefold())


def _walk_manifest(value: Any, location: str, errors: list[str]) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            key_text = str(key)
            key_norm = key_text.casefold()
            child_location = f"{location}.{key_text}"

            if key_norm in FORBIDDEN_TABLE_KEYS:
                errors.append(f"{child_location}: forbidden guest patch/overlay table")

            if _normalized(key_text) == "track4x4":
                errors.append(f"{child_location}: forbidden Track 4x4 key")

            if key_norm in ADDRESS_KEYS and isinstance(child, int):
                if HIGH_RAM_START <= child < HIGH_RAM_END:
                    errors.append(
                        f"{child_location}: forbidden high-RAM guest address 0x{child:08X}"
                    )

            if key_norm in SEMANTIC_TEXT_KEYS and isinstance(child, str):
                normalized = _normalized(child)
                if "track4x4" in normalized:
                    errors.append(f"{child_location}: forbidden Track 4x4 selector")
                if "xdelta" in normalized or "vcdiff" in normalized:
                    errors.append(f"{child_location}: forbidden binary-delta reference")
                if "aotpatch" in normalized or "aotoverlay" in normalized:
                    errors.append(f"{child_location}: forbidden AOT patch selector")

            _walk_manifest(child, child_location, errors)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _walk_manifest(child, f"{location}[{index}]", errors)


def verify_project(root: Path) -> list[str]:
    root = root.resolve()
    catalog = root / "mods" / "preloaded" / "packages"
    errors: list[str] = []

    if not catalog.is_dir():
        return [f"{catalog}: package catalog is missing"]

    for relative in LEGACY_CATALOGS:
        path = catalog / relative
        if path.exists():
            errors.append(f"{path}: retired legacy package still exists")

    for relative in RETIRED_SOURCES:
        path = root / relative
        if path.exists():
            errors.append(f"{path}: retired legacy source/generator still exists")

    for path in sorted(p for p in catalog.rglob("*") if p.is_file()):
        relative = path.relative_to(root).as_posix()
        normalized_path = _normalized(relative)
        if path.suffix.casefold() in FORBIDDEN_PAYLOAD_SUFFIXES:
            errors.append(f"{relative}: forbidden patch/overlay payload")
        if "track4x4" in normalized_path:
            errors.append(f"{relative}: forbidden Track 4x4 payload path")

    manifests = sorted(catalog.rglob("manifest.toml"))
    if not manifests:
        errors.append(f"{catalog}: no manifests found")
    for manifest in manifests:
        relative = manifest.relative_to(root).as_posix()
        try:
            data = tomllib.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, tomllib.TOMLDecodeError) as exc:
            errors.append(f"{relative}: cannot validate manifest: {exc}")
            continue
        _walk_manifest(data, relative, errors)

    state = root / "mods" / "preloaded" / "state.toml"
    if state.exists():
        try:
            state_data = tomllib.loads(state.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, tomllib.TOMLDecodeError) as exc:
            errors.append(f"{state.relative_to(root).as_posix()}: cannot validate state: {exc}")
        else:
            for package in state_data.get("package", []):
                package_id = package.get("id")
                version = package.get("version")
                if package_id in {
                    "wipeout3.enhancement.framerate",
                    "wipeout3.enhancement.framerate-120",
                }:
                    errors.append(f"state.toml: retired package selected: {package_id}")
                if package_id == "wipeout3.enhancement.framerate-pal100" and version in {
                    "1.0.0",
                    "1.1.0",
                }:
                    errors.append(f"state.toml: retired PAL package selected: {version}")

    game_toml = root / "game.toml"
    if not game_toml.is_file():
        errors.append(f"{game_toml}: game configuration is missing")
    else:
        try:
            game_data = tomllib.loads(game_toml.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, tomllib.TOMLDecodeError) as exc:
            errors.append(f"game.toml: cannot validate configuration: {exc}")
        else:
            recompiler = game_data.get("recompiler", {})
            if isinstance(recompiler, dict) and recompiler.get("patch"):
                errors.append("game.toml: forbidden recompiler/AOT patch catalog")
            if game_data.get("overlays"):
                errors.append("game.toml: forbidden declared guest overlay catalog")

            runtime = game_data.get("runtime", {})
            if not isinstance(runtime, dict):
                errors.append("game.toml: [runtime] must be a table")
            else:
                if runtime.get("overlay_cache") is not True:
                    errors.append("game.toml: stock race overlay cache must be enabled")
                if runtime.get("overlay_backend") != "gcc":
                    errors.append("game.toml: stock race overlay backend must be gcc")
                floor = runtime.get("overlay_region_floor")
                try:
                    floor_value = int(floor, 0) if isinstance(floor, str) else int(floor)
                except (TypeError, ValueError):
                    floor_value = -1
                if floor_value != 0x0016F000:
                    errors.append(
                        "game.toml: overlay_region_floor must pin the authenticated "
                        "stock race page at 0x16F000"
                    )
                if runtime.get("main_ram_mib") != 8:
                    errors.append("game.toml: direct Hue title requires exactly 8 MiB RAM")

            if isinstance(recompiler, dict):
                for key in ("mod_function_entry_funcs", "mod_instruction_sites"):
                    for raw in recompiler.get(key, []):
                        try:
                            address = int(raw, 0)
                        except (TypeError, ValueError):
                            errors.append(f"game.toml: invalid {key} address {raw!r}")
                            continue
                        if HIGH_RAM_START <= address < HIGH_RAM_END:
                            errors.append(
                                f"game.toml: {key} contains forbidden Hue/high-RAM "
                                f"address 0x{address:08X}"
                            )

    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="project root (defaults to the parent of tools/)",
    )
    args = parser.parse_args(argv)
    errors = verify_project(args.root)
    if errors:
        print("FAIL: release mod hygiene")
        for error in errors:
            print(f" - {error}")
        return 1
    print("PASS: direct-plugin catalog only; no legacy overlay, AOT patch, high-RAM, xdelta, or Track 4x4 payloads")
    return 0


if __name__ == "__main__":
    sys.exit(main())
