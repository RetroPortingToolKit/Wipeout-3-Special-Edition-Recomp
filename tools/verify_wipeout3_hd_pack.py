#!/usr/bin/env python3
"""Fail-closed audit of WipEout 3's HD pack and font activation receipt."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path


EXPECTED_PNG_COUNT = 292
EXPECTED_MAPPING_COUNT = 390
FONT_KEYS = {
    "3dc173e1-3da5d410", "3dc173e1-41e26c01", "3dc173e1-7205c45b",
    "3dc173e1-8ec51178", "3dc173e1-94f476b1",
    "4cb2ec4e-3da5d410", "4cb2ec4e-7205c45b", "4cb2ec4e-8ec51178",
    "4cb2ec4e-94f476b1", "4cb2ec4e-b6e6caeb",
    "6b411011-273d3b40",
    "75f5e38b-3da5d410", "75f5e38b-7205c45b", "75f5e38b-8ec51178",
    "75f5e38b-94f476b1", "75f5e38b-b6e6caeb",
    "861ad9f1-273d3b40", "861ad9f1-5888b400",
    "d20dee94-273d3b40", "d20dee94-5888b400",
}
HELVETICA_KEYS = {
    "861ad9f1-273d3b40", "861ad9f1-5888b400",
    "d20dee94-273d3b40", "d20dee94-5888b400",
}
# These are exact 16x replacements for the two 256-wide source atlases. The
# texture hash selects the atlas geometry and the palette hash selects colour;
# both palette variants of one atlas must therefore have identical dimensions.
HELVETICA_HD_DIMENSIONS = {
    "861ad9f1": (4096, 512),  # 256x32 source atlas
    "d20dee94": (4096, 576),  # 256x36 source atlas
}
# Legacy Beetle packs omit leading zeroes; the runtime's %x parser accepts one
# through eight digits and indexes the normalized numeric key.
PNG_RE = re.compile(r"^([0-9a-fA-F]{1,8})-([0-9a-fA-F]{1,8})\.png$")
INDEX_RE = re.compile(
    r"\[tex_pack\] legacy root indexed: files=(\d+) unique=(\d+) "
    r"ambiguous=(\d+) mappings=(\d+) fonts=(\w+)"
)
SELECT_RE = re.compile(
    r"\[tex_pack\] replace=(\d+) dump=(\d+) pack=(.+) \((\d+) entries\) dump_dir="
)


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def parse_exclusions(path: Path) -> set[str]:
    values: set[str] = set()
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        value = raw.split("#", 1)[0].strip().lower()
        if value:
            values.add(value)
    return values


def png_dimensions(path: Path) -> tuple[int, int] | None:
    header = path.read_bytes()[:24]
    if (len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or
            header[12:16] != b"IHDR"):
        return None
    return (int.from_bytes(header[16:20], "big"),
            int.from_bytes(header[20:24], "big"))


def classify_font_activation(
    png_keys: set[str], exclusions: set[str]
) -> tuple[list[str], list[str], list[str]]:
    missing = sorted(FONT_KEYS - png_keys)
    excluded = sorted(
        key for key in FONT_KEYS if key.split("-", 1)[0] in exclusions
    )
    active = sorted(FONT_KEYS - set(excluded))
    return missing, excluded, active


def norm(path: Path | str) -> str:
    # Keep the lexical launch path. Path.resolve() follows this pack's Windows
    # directory redirection to an older authoring source, which is not the path
    # the runtime indexed and can have a different working-tree inventory.
    return os.path.abspath(os.fspath(path)).replace("\\", "/").casefold()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--runtime-log", type=Path, required=True)
    parser.add_argument("--run-json", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument(
        "--require-all-fonts-active", action="store_true",
        help="fail if any required font key is suppressed by exclude.txt",
    )
    args = parser.parse_args()

    repo = args.repo.resolve()
    errors: list[str] = []
    warnings: list[str] = []

    pack_candidates = sorted((repo / "hd_textures").glob("*-texture-replacements"))
    if len(pack_candidates) != 1:
        fail(errors, f"expected one replacement pack, found {len(pack_candidates)}")
        pack = pack_candidates[0] if pack_candidates else repo / "hd_textures" / "MISSING"
    else:
        pack = pack_candidates[0]

    png_keys: set[str] = set()
    png_paths: dict[str, Path] = {}
    png_file_count = 0
    if pack.is_dir():
        for item in pack.iterdir():
            if not item.is_file():
                continue
            match = PNG_RE.fullmatch(item.name)
            if match:
                png_file_count += 1
                key = f"{int(match.group(1), 16):08x}-{int(match.group(2), 16):08x}"
                png_keys.add(key)
                png_paths[key] = item
    if png_file_count != EXPECTED_PNG_COUNT:
        fail(errors, f"pack has {png_file_count} keyed PNGs, expected {EXPECTED_PNG_COUNT}")
    if len(png_keys) != EXPECTED_PNG_COUNT:
        fail(errors, f"pack has {len(png_keys)} unique keys, expected {EXPECTED_PNG_COUNT}")

    exclusions = parse_exclusions(pack / "exclude.txt")
    missing_font_keys, excluded_font_keys, active_font_keys = classify_font_activation(
        png_keys, exclusions
    )
    if missing_font_keys:
        fail(errors, f"missing {len(missing_font_keys)} required font keys")

    excluded_font_hashes = sorted({key.split("-", 1)[0] for key in excluded_font_keys})
    if excluded_font_keys:
        fail(errors, f"{len(excluded_font_keys)} required font keys are excluded")

    bad_helvetica_dimensions: dict[str, object] = {}
    for key in sorted(HELVETICA_KEYS & png_keys):
        texture_hash = key.split("-", 1)[0]
        expected = HELVETICA_HD_DIMENSIONS[texture_hash]
        actual = png_dimensions(png_paths[key])
        if actual != expected:
            bad_helvetica_dimensions[key] = {
                "actual": list(actual) if actual else None,
                "expected": list(expected),
            }
    if bad_helvetica_dimensions:
        fail(errors, "Helvetica atlas dimensions do not match their texture hashes")

    config_text = (repo / "game.toml").read_text(encoding="utf-8")
    hd_enabled = re.search(r"(?m)^hd_textures\s*=\s*true\s*$", config_text) is not None
    dir_match = re.search(r'(?m)^hd_texture_dir\s*=\s*"([^"]+)"\s*$', config_text)
    hd_dir = dir_match.group(1) if dir_match else None
    if not hd_enabled:
        fail(errors, "game.toml does not default hd_textures=true")
    if hd_dir != "hd_textures":
        fail(errors, f'game.toml hd_texture_dir is {hd_dir!r}, expected "hd_textures"')

    runtime_text = args.runtime_log.read_text(encoding="utf-8", errors="replace")
    index_matches = INDEX_RE.findall(runtime_text)
    select_matches = SELECT_RE.findall(runtime_text)
    if len(index_matches) != 1:
        fail(errors, f"expected one runtime index receipt, found {len(index_matches)}")
        index = None
    else:
        index = index_matches[0]
        expected = (str(EXPECTED_PNG_COUNT), str(EXPECTED_PNG_COUNT), "0",
                    str(EXPECTED_MAPPING_COUNT), "complete")
        if index != expected:
            fail(errors, f"unexpected runtime index receipt: {index}")
    if len(select_matches) != 1:
        fail(errors, f"expected one runtime selection receipt, found {len(select_matches)}")
        selected_pack = None
    else:
        replace, dump, selected_pack, entries = select_matches[0]
        if replace != "1" or dump != "0" or entries != str(EXPECTED_PNG_COUNT):
            fail(errors, f"unexpected runtime selection flags/count: {select_matches[0]}")
        if norm(selected_pack) != norm(pack):
            fail(errors, f"runtime selected wrong pack: {selected_pack}")

    isolated_state = None
    if args.run_json:
        run = json.loads(args.run_json.read_text(encoding="utf-8"))
        isolated_state = run.get("isolated_state_directory")
        arguments = run.get("arguments", [])
        memcard = None
        if "--memcard-dir" in arguments:
            index_arg = arguments.index("--memcard-dir")
            if index_arg + 1 < len(arguments):
                memcard = arguments[index_arg + 1]
        if not isolated_state or not memcard or norm(isolated_state) != norm(memcard):
            fail(errors, "run receipt does not bind --memcard-dir to isolated state")
        source_cwd = run.get("source_working_directory")
        if not source_cwd or norm(source_cwd) != norm(repo):
            fail(errors, f"run source working directory is not repo: {source_cwd}")
    else:
        warnings.append("no run.json supplied; isolated-state provenance not checked")

    result = {
        "schema": 1,
        "verdict": "PASS" if not errors else "FAIL",
        "repo": str(repo),
        "pack": os.path.abspath(pack),
        "pack_png_files": png_file_count,
        "pack_unique_keys": len(png_keys),
        "required_font_keys_present": len(FONT_KEYS) - len(missing_font_keys),
        "required_font_keys_total": len(FONT_KEYS),
        "active_font_keys": len(active_font_keys),
        "excluded_font_keys": len(excluded_font_keys),
        "excluded_font_hashes": excluded_font_hashes,
        "all_fonts_active": not excluded_font_keys,
        "active_helvetica_keys": len(HELVETICA_KEYS - set(excluded_font_keys)),
        "required_helvetica_keys": len(HELVETICA_KEYS),
        "bad_helvetica_dimensions": bad_helvetica_dimensions,
        "runtime_index_receipt": list(index) if index else None,
        "runtime_selected_pack": selected_pack,
        "isolated_state_directory": isolated_state,
        "errors": errors,
        "warnings": warnings,
    }
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
