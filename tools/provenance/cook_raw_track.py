#!/usr/bin/env python3
"""Convert a raw 2352-byte data track to a sector-aligned 2048-byte ISO view.

The command preserves one output logical block per input sector. Mode 2 Form 2
sectors are counted explicitly and contribute their first 2048 user bytes; the
result is suitable for ISO9660 inventory, not for reconstructing XA payloads.
Source media is never modified and existing outputs are refused.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

RAW_SECTOR_BYTES = 2352
LOGICAL_BLOCK_BYTES = 2048
SYNC = b"\x00" + (b"\xff" * 10) + b"\x00"


class TrackFormatError(RuntimeError):
    pass


def user_block(sector: bytes, index: int) -> tuple[bytes, str]:
    if len(sector) != RAW_SECTOR_BYTES:
        raise TrackFormatError(f"sector {index}: truncated raw sector")
    if sector[:12] != SYNC:
        raise TrackFormatError(f"sector {index}: invalid sync pattern")

    mode = sector[15]
    if mode == 1:
        return sector[16 : 16 + LOGICAL_BLOCK_BYTES], "mode1"
    if mode != 2:
        raise TrackFormatError(f"sector {index}: unsupported mode {mode}")

    subheader = sector[16:20]
    if subheader != sector[20:24]:
        raise TrackFormatError(f"sector {index}: Mode 2 subheader copies disagree")
    form = "mode2_form2" if (subheader[2] & 0x20) else "mode2_form1"
    return sector[24 : 24 + LOGICAL_BLOCK_BYTES], form


def cook(source: Path, output: Path) -> dict[str, object]:
    if not source.is_file():
        raise TrackFormatError(f"missing source track: {source}")
    if output.exists():
        raise TrackFormatError(f"refusing to overwrite output: {output}")
    source_size = source.stat().st_size
    if source_size == 0 or source_size % RAW_SECTOR_BYTES:
        raise TrackFormatError("raw track size is not a non-zero multiple of 2352")

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".partial")
    if temporary.exists():
        raise TrackFormatError(f"refusing to overwrite partial output: {temporary}")

    counts = {"mode1": 0, "mode2_form1": 0, "mode2_form2": 0}
    source_hash = hashlib.sha256()
    output_hash = hashlib.sha256()
    sectors = source_size // RAW_SECTOR_BYTES
    try:
        with source.open("rb") as reader, temporary.open("xb") as writer:
            for index in range(sectors):
                sector = reader.read(RAW_SECTOR_BYTES)
                source_hash.update(sector)
                block, kind = user_block(sector, index)
                counts[kind] += 1
                writer.write(block)
                output_hash.update(block)
            if reader.read(1):
                raise TrackFormatError("source grew while it was being read")
        temporary.replace(output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise

    return {
        "schema": "psxrecomp.raw-track-cook.v1",
        "status": "ok",
        "input": {
            "name": source.name,
            "bytes": source_size,
            "sha256": source_hash.hexdigest(),
            "raw_sector_bytes": RAW_SECTOR_BYTES,
        },
        "output": {
            "name": output.name,
            "bytes": output.stat().st_size,
            "sha256": output_hash.hexdigest(),
            "logical_block_bytes": LOGICAL_BLOCK_BYTES,
        },
        "sectors": sectors,
        "sector_kinds": counts,
        "xa_form2_payload_note": "first 2048 of 2324 user bytes retained for ISO inventory",
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        report = cook(args.input, args.output)
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            if args.json_out.exists():
                raise TrackFormatError(
                    f"refusing to overwrite receipt: {args.json_out}"
                )
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(payload, encoding="utf-8", newline="\n")
        sys.stdout.write(payload)
        return 0
    except (OSError, TrackFormatError) as error:
        sys.stderr.write(f"raw track conversion failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
