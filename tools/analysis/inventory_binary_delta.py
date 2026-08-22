#!/usr/bin/env python3
"""Inventory exact byte/word changes between equal-sized executable images."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections.abc import Iterable
from pathlib import Path
from typing import Any

OUTPUT_SCHEMA = "wo3se.binary-delta-inventory.v1"
MAX_IMAGE_BYTES = 64 * 1024 * 1024
MAX_REPORTED_RANGES = 1_000_000
ADDRESS_LIMIT = 1 << 32


class DeltaInventoryError(RuntimeError):
    pass


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _collect_ranges(differences: Iterable[bool]) -> tuple[int, list[tuple[int, int]]]:
    changed = 0
    open_start: int | None = None
    length = 0
    ranges: list[tuple[int, int]] = []
    for index, differs in enumerate(differences):
        length = index + 1
        if differs:
            changed += 1
            if open_start is None:
                open_start = index
        elif open_start is not None:
            ranges.append((open_start, index))
            open_start = None
            if len(ranges) > MAX_REPORTED_RANGES:
                raise DeltaInventoryError("delta has too many disjoint ranges")
    if open_start is not None:
        ranges.append((open_start, length))
    return changed, ranges


def inventory(source: bytes, target: bytes, *, guest_base: int) -> dict[str, Any]:
    if not source or len(source) != len(target):
        raise DeltaInventoryError("images must be non-empty and exactly equal-sized")
    if len(source) > MAX_IMAGE_BYTES:
        raise DeltaInventoryError("images exceed the size limit")
    if len(source) % 4:
        raise DeltaInventoryError("images must be word-aligned")
    if (
        guest_base < 0
        or guest_base % 4
        or guest_base >= ADDRESS_LIMIT
        or guest_base + len(source) > ADDRESS_LIMIT
    ):
        raise DeltaInventoryError("guest range must be aligned and 32-bit")

    changed_byte_count, byte_ranges = _collect_ranges(
        source_byte != target_byte for source_byte, target_byte in zip(source, target)
    )
    changed_word_count, word_ranges = _collect_ranges(
        source[offset : offset + 4] != target[offset : offset + 4]
        for offset in range(0, len(source), 4)
    )

    byte_receipts = []
    for start, end in byte_ranges:
        byte_receipts.append(
            {
                "start_offset": start,
                "end_offset_exclusive": end,
                "size": end - start,
                "source_sha256": _sha256(source[start:end]),
                "target_sha256": _sha256(target[start:end]),
            }
        )
    word_receipts = []
    for first_word, end_word in word_ranges:
        start = first_word * 4
        end = end_word * 4
        word_receipts.append(
            {
                "start_offset": start,
                "end_offset_exclusive": end,
                "guest_start": guest_base + start,
                "guest_end_exclusive": guest_base + end,
                "word_count": end_word - first_word,
                "source_sha256": _sha256(source[start:end]),
                "target_sha256": _sha256(target[start:end]),
            }
        )

    return {
        "schema": OUTPUT_SCHEMA,
        "status": "identical" if changed_byte_count == 0 else "different",
        "classification": "exact byte delta; no code/data or semantic claims",
        "guest_range": {
            "start": guest_base,
            "end_exclusive": guest_base + len(source),
        },
        "image_bytes": len(source),
        "source_sha256": _sha256(source),
        "target_sha256": _sha256(target),
        "changed_byte_count": changed_byte_count,
        "changed_byte_range_count": len(byte_ranges),
        "changed_word_count": changed_word_count,
        "changed_word_range_count": len(word_ranges),
        "changed_byte_ranges": byte_receipts,
        "changed_word_ranges": word_receipts,
    }


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise DeltaInventoryError(f"refusing to overwrite output: {path}") from error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--guest-base", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.json_out and args.json_out.exists():
            raise DeltaInventoryError(f"refusing to overwrite output: {args.json_out}")
        for path in (args.source, args.target):
            if not path.is_file():
                raise DeltaInventoryError(f"missing image: {path}")
            if path.stat().st_size > MAX_IMAGE_BYTES:
                raise DeltaInventoryError(f"image exceeds the size limit: {path}")
        report = inventory(
            args.source.read_bytes(),
            args.target.read_bytes(),
            guest_base=args.guest_base,
        )
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, ValueError, DeltaInventoryError) as error:
        sys.stderr.write(f"binary-delta inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
