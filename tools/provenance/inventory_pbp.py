#!/usr/bin/env python3
"""Inventory the simple 12-byte-entry PBP containers used by PS1 game data.

The second table word is intentionally named ``tag``: its semantics are not
assumed. The tool validates only observed container structure and can
optionally extract opaque entries for local analysis.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import struct
import sys
from pathlib import Path
from typing import Any

COUNT_OFFSET = 80
TABLE_OFFSET = 88
ENTRY_BYTES = 12
MAX_ENTRIES = 4096
MAX_EXPANDED_ENTRY_BYTES = 64 * 1024 * 1024


class PbpFormatError(RuntimeError):
    pass


def _load_lzss_decoder() -> Any:
    path = Path(__file__).with_name("decompress_lzss.py")
    spec = importlib.util.spec_from_file_location("provenance_decompress_lzss", path)
    if spec is None or spec.loader is None:
        raise PbpFormatError(f"cannot load bounded decoder: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def align4(value: int) -> int:
    return (value + 3) & ~3


def parse(data: bytes) -> list[dict[str, Any]]:
    if len(data) < TABLE_OFFSET:
        raise PbpFormatError("container is shorter than its fixed header")
    count = struct.unpack_from("<I", data, COUNT_OFFSET)[0]
    if count == 0 or count > MAX_ENTRIES:
        raise PbpFormatError(f"invalid entry count: {count}")
    table_end = TABLE_OFFSET + count * ENTRY_BYTES
    if table_end > len(data):
        raise PbpFormatError("entry table extends beyond the container")

    entries: list[dict[str, Any]] = []
    previous_end = align4(table_end)
    for index in range(count):
        offset, tag, length = struct.unpack_from(
            "<III", data, TABLE_OFFSET + index * ENTRY_BYTES
        )
        if offset % 4:
            raise PbpFormatError(f"entry {index}: offset is not 4-byte aligned")
        if offset < previous_end:
            raise PbpFormatError(f"entry {index}: overlaps its table or predecessor")
        end = offset + length
        if end < offset or end > len(data):
            raise PbpFormatError(f"entry {index}: payload extends beyond the container")
        payload = data[offset:end]
        entries.append(
            {
                "index": index,
                "offset": offset,
                "tag": tag,
                "bytes": length,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "prefix_hex": payload[:16].hex(),
                "padding_before": offset - previous_end,
            }
        )
        previous_end = align4(end)
    return entries


def _report_entries(report: dict[str, Any]) -> list[dict[str, Any]]:
    entries = report.get("entries")
    if not isinstance(entries, list) or not all(
        isinstance(entry, dict) for entry in entries
    ):
        raise PbpFormatError("report entries must be an array of objects")
    return entries


def _expand_entry(
    entry: dict[str, Any], data: bytes, decoder: Any
) -> tuple[bytes, dict[str, Any] | None]:
    offset = int(entry["offset"])
    length = int(entry["bytes"])
    expanded_size = int(entry["tag"])
    payload = data[offset : offset + length]
    if expanded_size < length:
        raise PbpFormatError(
            f"entry {entry['index']}: expanded-size field is smaller than payload"
        )
    if expanded_size == length:
        return payload, None
    try:
        expanded, receipt = decoder.decode(
            payload,
            expected_size=expanded_size,
            max_output_bytes=MAX_EXPANDED_ENTRY_BYTES,
        )
    except decoder.LzssFormatError as error:
        raise PbpFormatError(
            f"entry {entry['index']}: bounded LZSS verification failed: {error}"
        ) from error
    return expanded, receipt.as_dict()


def verify_expanded_entries(report: dict[str, Any], data: bytes) -> None:
    decoder = _load_lzss_decoder()
    stored_count = 0
    compressed_count = 0
    for entry in _report_entries(report):
        length = int(entry["bytes"])
        expanded_size = int(entry["tag"])
        expanded, decode_receipt = _expand_entry(entry, data, decoder)
        if expanded_size == length:
            entry["encoding"] = "stored"
            stored_count += 1
        else:
            entry["encoding"] = "lzss"
            entry["decode"] = decode_receipt
            compressed_count += 1
        entry["expanded_bytes"] = len(expanded)
        entry["expanded_sha256"] = hashlib.sha256(expanded).hexdigest()
    report["schema"] = "psxrecomp.pbp-inventory.v2"
    report["expanded_verification"] = {
        "status": "exact",
        "stored_entries": stored_count,
        "lzss_entries": compressed_count,
    }


def inventory(
    path: Path, verify_expanded: bool = False
) -> tuple[dict[str, Any], bytes]:
    if not path.is_file():
        raise PbpFormatError(f"missing container: {path}")
    data = path.read_bytes()
    entries = parse(data)
    last = entries[-1]
    trailing = len(data) - align4(int(last["offset"]) + int(last["bytes"]))
    trailing = max(trailing, 0)
    report: dict[str, Any] = {
        "schema": "psxrecomp.pbp-inventory.v1",
        "status": "ok",
        "container": {
            "name": path.name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
        "entry_count": len(entries),
        "table_offset": TABLE_OFFSET,
        "trailing_bytes": trailing,
        "entries": entries,
    }
    if verify_expanded:
        verify_expanded_entries(report, data)
    return report, data


def extract(report: dict[str, Any], data: bytes, output: Path) -> None:
    if output.exists():
        raise PbpFormatError(f"refusing to overwrite extraction directory: {output}")
    temporary = output.with_name(output.name + ".partial")
    if temporary.exists():
        raise PbpFormatError(f"refusing to overwrite partial extraction: {temporary}")
    temporary.mkdir(parents=True)
    try:
        for entry in _report_entries(report):
            index = int(entry["index"])
            offset = int(entry["offset"])
            length = int(entry["bytes"])
            digest = str(entry["sha256"])
            name = f"{index:04d}-{digest[:16]}.bin"
            (temporary / name).write_bytes(data[offset : offset + length])
        temporary.replace(output)
    except Exception:
        for path in temporary.glob("*"):
            if path.is_file():
                path.unlink()
        temporary.rmdir()
        raise


def extract_expanded(report: dict[str, Any], data: bytes, output: Path) -> None:
    verification = report.get("expanded_verification")
    if not isinstance(verification, dict) or verification.get("status") != "exact":
        raise PbpFormatError("expanded extraction requires exact expanded verification")
    if output.exists():
        raise PbpFormatError(f"refusing to overwrite extraction directory: {output}")
    temporary = output.with_name(output.name + ".partial")
    if temporary.exists():
        raise PbpFormatError(f"refusing to overwrite partial extraction: {temporary}")
    decoder = _load_lzss_decoder()
    temporary.mkdir(parents=True)
    try:
        for entry in _report_entries(report):
            expanded, _ = _expand_entry(entry, data, decoder)
            digest = hashlib.sha256(expanded).hexdigest()
            if digest != entry.get("expanded_sha256"):
                raise PbpFormatError(
                    f"entry {entry['index']}: expanded payload digest changed"
                )
            index = int(entry["index"])
            name = f"{index:04d}-{digest[:16]}.bin"
            (temporary / name).write_bytes(expanded)
        temporary.replace(output)
    except Exception:
        for path in temporary.glob("*"):
            if path.is_file():
                path.unlink()
        temporary.rmdir()
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--extract", type=Path)
    parser.add_argument(
        "--extract-expanded",
        type=Path,
        help="extract exact logical payloads; requires --verify-expanded",
    )
    parser.add_argument(
        "--verify-expanded",
        action="store_true",
        help="verify stored/expanded sizes and hash exact logical payloads",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.extract_expanded and not args.verify_expanded:
            raise PbpFormatError("--extract-expanded requires --verify-expanded")
        report, data = inventory(args.input, args.verify_expanded)
        if args.extract:
            extract(report, data, args.extract)
            report["extracted_directory"] = args.extract.name
        if args.extract_expanded:
            extract_expanded(report, data, args.extract_expanded)
            report["expanded_extracted_directory"] = args.extract_expanded.name
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            if args.json_out.exists():
                raise PbpFormatError(f"refusing to overwrite receipt: {args.json_out}")
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(payload, encoding="utf-8", newline="\n")
        sys.stdout.write(payload)
        return 0
    except (OSError, PbpFormatError) as error:
        sys.stderr.write(f"PBP inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
