#!/usr/bin/env python3
"""Create a strict metadata-only inventory of an external texture pack."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import zlib
from pathlib import Path, PurePosixPath
from typing import Any

OUTPUT_SCHEMA = "wo3se.external-texture-pack.v1"
REPLACEMENT_DIRECTORY = "(cue name)-texture-replacements"
MAX_PNG_BYTES = 64 * 1024 * 1024
_KEY_RE = re.compile(r"([0-9a-f]{1,8}-[0-9a-f]{1,8})")
_COMMIT_RE = re.compile(r"[0-9a-f]{40}")
_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class AssetInventoryError(RuntimeError):
    pass


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _logical_path(value: str, path: str) -> str:
    if not value or "\\" in value or any(ord(character) < 32 for character in value):
        raise AssetInventoryError(f"{path} is not a safe logical path")
    result = PurePosixPath(value)
    if result.is_absolute() or any(part in {"", ".", ".."} for part in result.parts):
        raise AssetInventoryError(f"{path} is not a relative normalized path")
    if result.suffix.lower() != ".png":
        raise AssetInventoryError(f"{path} must name a PNG")
    return result.as_posix()


def parse_mappings(payload: str) -> dict[str, str]:
    mappings: dict[str, str] = {}
    for line_number, raw_line in enumerate(payload.splitlines(), 1):
        line = raw_line.strip()
        if (
            not line
            or line.startswith("#")
            or (line.startswith("[") and line.endswith("]"))
        ):
            continue
        if "=" not in line:
            continue
        raw_key, raw_path = (part.strip() for part in line.split("=", 1))
        if _KEY_RE.fullmatch(raw_key) is None:
            raise AssetInventoryError(
                f"Hashes.ini:{line_number} has a malformed texture key"
            )
        if raw_key in mappings:
            raise AssetInventoryError(
                f"Hashes.ini:{line_number} repeats texture key {raw_key}"
            )
        mappings[raw_key] = _logical_path(raw_path, f"Hashes.ini:{line_number} mapping")
    if not mappings:
        raise AssetInventoryError("Hashes.ini contains no texture mappings")
    return mappings


def parse_png(payload: bytes, name: str) -> dict[str, Any]:
    if len(payload) < 8 or payload[:8] != _PNG_SIGNATURE:
        raise AssetInventoryError(f"{name} has an invalid PNG signature")
    offset = 8
    ihdr: dict[str, Any] | None = None
    saw_iend = False
    while offset < len(payload):
        if len(payload) - offset < 12:
            raise AssetInventoryError(f"{name} has a truncated PNG chunk")
        length = int.from_bytes(payload[offset : offset + 4], "big")
        chunk_type = payload[offset + 4 : offset + 8]
        data_start = offset + 8
        data_end = data_start + length
        crc_end = data_end + 4
        if data_end < data_start or crc_end > len(payload):
            raise AssetInventoryError(f"{name} has an out-of-bounds PNG chunk")
        expected_crc = int.from_bytes(payload[data_end:crc_end], "big")
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(payload[data_start:data_end], actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise AssetInventoryError(f"{name} has a PNG chunk CRC mismatch")
        if ihdr is None and chunk_type != b"IHDR":
            raise AssetInventoryError(f"{name} does not begin with IHDR")
        if chunk_type == b"IHDR":
            if ihdr is not None or length != 13:
                raise AssetInventoryError(f"{name} has an invalid IHDR")
            width = int.from_bytes(payload[data_start : data_start + 4], "big")
            height = int.from_bytes(payload[data_start + 4 : data_start + 8], "big")
            bit_depth = payload[data_start + 8]
            color_type = payload[data_start + 9]
            compression = payload[data_start + 10]
            filter_method = payload[data_start + 11]
            interlace = payload[data_start + 12]
            valid_depths = {
                0: {1, 2, 4, 8, 16},
                2: {8, 16},
                3: {1, 2, 4, 8},
                4: {8, 16},
                6: {8, 16},
            }
            if (
                not 0 < width <= 16384
                or not 0 < height <= 16384
                or color_type not in valid_depths
                or bit_depth not in valid_depths[color_type]
                or compression != 0
                or filter_method != 0
                or interlace not in {0, 1}
            ):
                raise AssetInventoryError(f"{name} has unsupported IHDR fields")
            ihdr = {
                "width": width,
                "height": height,
                "bit_depth": bit_depth,
                "color_type": color_type,
                "interlaced": bool(interlace),
            }
        if chunk_type == b"IEND":
            if length != 0 or crc_end != len(payload):
                raise AssetInventoryError(f"{name} has an invalid IEND")
            saw_iend = True
            offset = crc_end
            break
        offset = crc_end
    if ihdr is None or not saw_iend or offset != len(payload):
        raise AssetInventoryError(f"{name} is not a complete PNG")
    return ihdr


def inventory(root: Path, expected_commit: str) -> dict[str, Any]:
    if _COMMIT_RE.fullmatch(expected_commit) is None:
        raise AssetInventoryError(
            "expected commit must be 40 lowercase hexadecimal digits"
        )
    hashes_path = root / "Hashes.ini"
    readme_path = root / "README.md"
    replacements = root / REPLACEMENT_DIRECTORY
    if not hashes_path.is_file() or not readme_path.is_file():
        raise AssetInventoryError("texture pack is missing Hashes.ini or README.md")
    if not replacements.is_dir():
        raise AssetInventoryError("texture pack is missing its replacement directory")

    hashes_bytes = hashes_path.read_bytes()
    try:
        hashes_text = hashes_bytes.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise AssetInventoryError("Hashes.ini is not UTF-8") from error
    mappings = parse_mappings(hashes_text)

    textures: list[dict[str, Any]] = []
    seen_files: set[str] = set()
    sha_to_keys: dict[str, list[str]] = {}
    total_bytes = 0
    for image_path in sorted(replacements.iterdir(), key=lambda path: path.name):
        if not image_path.is_file() or image_path.suffix != ".png":
            raise AssetInventoryError(
                f"replacement directory contains unexpected entry: {image_path.name}"
            )
        key = image_path.stem
        if _KEY_RE.fullmatch(key) is None or key not in mappings:
            raise AssetInventoryError(f"unmapped replacement image: {image_path.name}")
        folded = image_path.name.casefold()
        if folded in seen_files:
            raise AssetInventoryError("replacement filenames collide by case")
        seen_files.add(folded)
        size = image_path.stat().st_size
        if not 0 < size <= MAX_PNG_BYTES:
            raise AssetInventoryError(
                f"replacement image size is invalid: {image_path.name}"
            )
        payload = image_path.read_bytes()
        if len(payload) != size:
            raise AssetInventoryError(
                f"replacement image changed while read: {image_path.name}"
            )
        digest = _sha256(payload)
        sha_to_keys.setdefault(digest, []).append(key)
        total_bytes += size
        textures.append(
            {
                "key": key,
                "logical_path": mappings[key],
                "file": image_path.name,
                "size": size,
                "sha256": digest,
                "png": parse_png(payload, image_path.name),
            }
        )

    available = {entry["key"] for entry in textures}
    missing = [
        {"key": key, "logical_path": mappings[key]}
        for key in sorted(set(mappings) - available)
    ]
    duplicates = [
        {"sha256": digest, "keys": sorted(keys)}
        for digest, keys in sorted(sha_to_keys.items())
        if len(keys) > 1
    ]
    return {
        "schema": OUTPUT_SCHEMA,
        "status": "valid-partial-pack" if missing else "valid-complete-pack",
        "classification": (
            "external permitted asset metadata; replacement-key semantics require "
            "independent runtime validation"
        ),
        "source": {
            "repository": "princepeter64/Wip3out-HD-Texture-Pack",
            "commit": expected_commit,
            "hashes_ini_sha256": _sha256(hashes_bytes),
            "readme_sha256": _sha256(readme_path.read_bytes()),
        },
        "mapping_count": len(mappings),
        "texture_count": len(textures),
        "missing_mapping_count": len(missing),
        "total_texture_bytes": total_bytes,
        "textures": textures,
        "missing_mappings": missing,
        "duplicate_payloads": duplicates,
    }


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise AssetInventoryError(f"refusing to overwrite output: {path}") from error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack-root", type=Path, required=True)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.json_out and args.json_out.exists():
            raise AssetInventoryError(f"refusing to overwrite output: {args.json_out}")
        report = inventory(args.pack_root, args.expected_commit)
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, UnicodeError, ValueError, AssetInventoryError) as error:
        sys.stderr.write(f"texture-pack inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
