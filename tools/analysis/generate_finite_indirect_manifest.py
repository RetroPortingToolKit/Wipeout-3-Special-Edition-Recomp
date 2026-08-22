#!/usr/bin/env python3
"""Generate hash-bound finite indirect-target evidence from authenticated bytes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from .authorize_executable_regions import resolve_indirect, sha256
except ImportError:  # Direct script execution.
    from authorize_executable_regions import resolve_indirect, sha256


def parse_site(value: str) -> tuple[int, int]:
    source, separator, register = value.partition(":")
    if not separator:
        raise argparse.ArgumentTypeError("site must be PC:REGISTER")
    try:
        return int(source, 0), int(register, 0)
    except ValueError as error:
        raise argparse.ArgumentTypeError("site contains an invalid number") from error


def build_manifest(
    image: bytes, expected_sha256: str, base: int, name: str,
    sites: list[tuple[int, int]],
) -> dict[str, object]:
    digest = sha256(image)
    if digest != expected_sha256:
        raise ValueError("authenticated image SHA-256 mismatch")
    if not sites or sites != sorted(set(sites)):
        raise ValueError("sites must be non-empty, sorted, and unique")
    resolved = []
    for source, register in sites:
        if source % 4 or not base <= source < base + len(image):
            raise ValueError("site is outside the aligned image range")
        resolution = resolve_indirect(image, base, source, register)
        if resolution is None:
            raise ValueError(f"site 0x{source:08x} has no finite byte proof")
        resolved.append({
            "proof": resolution[1],
            "source": source,
            "source_register": register,
            "targets": resolution[0],
        })
    return {
        "image": {
            "base": base,
            "byte_count": len(image),
            "name": name,
            "sha256": digest,
        },
        "sites": resolved,
        "schema": "wo3se.finite-indirect-targets.v1",
        "status": "authenticated-finite",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--image-sha256", required=True)
    parser.add_argument("--base", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--site", action="append", type=parse_site, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        manifest = build_manifest(
            args.image.read_bytes(), args.image_sha256, args.base, args.name,
            args.site,
        )
        payload = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8", newline="\n")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(2, f"finite indirect manifest rejected: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
