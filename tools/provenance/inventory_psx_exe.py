#!/usr/bin/env python3
"""Inventory a PS-X EXE and emit hash-only executable-identity evidence.

The aligned-word control scan is intentionally labelled as untrusted: data can
look like MIPS instructions.  Its roots are discovery hints, never executable
coverage or permission to compile a range.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path
from typing import Any

HEADER_BYTES = 2048
MAGIC = b"PS-X EXE"
ADDRESS_LIMIT = 1 << 32


class ExeInventoryError(RuntimeError):
    pass


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _jump_target(pc: int, word: int) -> int:
    return ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)


def scan_control_words(text: bytes, load_address: int) -> dict[str, Any]:
    load_end = load_address + len(text)
    calls: dict[int, int] = {}
    stats = {
        "aligned_words": len(text) // 4,
        "direct_jumps": 0,
        "direct_calls": 0,
        "conditional_branches": 0,
        "indirect_jumps": 0,
        "indirect_calls": 0,
        "in_range_direct_calls": 0,
        "out_of_range_direct_calls": 0,
    }
    for offset in range(0, len(text), 4):
        word = _u32(text, offset)
        pc = load_address + offset
        opcode = word >> 26
        if opcode == 2:
            stats["direct_jumps"] += 1
        elif opcode == 3:
            stats["direct_calls"] += 1
            target = _jump_target(pc, word)
            if load_address <= target < load_end:
                stats["in_range_direct_calls"] += 1
                calls[target] = calls.get(target, 0) + 1
            else:
                stats["out_of_range_direct_calls"] += 1
        elif opcode in (4, 5, 6, 7):
            stats["conditional_branches"] += 1
        elif opcode == 1 and ((word >> 16) & 0x1F) in (0, 1, 16, 17):
            stats["conditional_branches"] += 1
        elif opcode in (16, 17, 18, 19) and ((word >> 21) & 0x1F) == 8:
            stats["conditional_branches"] += 1
        elif opcode == 0 and (word & 0x3F) == 8:
            stats["indirect_jumps"] += 1
        elif opcode == 0 and (word & 0x3F) == 9:
            stats["indirect_calls"] += 1

    call_records = [
        {"target": target, "occurrences": calls[target]} for target in sorted(calls)
    ]
    packed_targets = b"".join(struct.pack("<I", target) for target in sorted(calls))
    return {
        "classification": "untrusted aligned-word discovery hints; data words may match",
        "statistics": stats,
        "unique_in_range_call_targets": call_records,
        "target_set_sha256": _sha256(packed_targets),
    }


def parse(data: bytes, name: str = "input.exe") -> dict[str, Any]:
    if len(data) < HEADER_BYTES:
        raise ExeInventoryError("file is shorter than the fixed PS-X EXE header")
    if data[: len(MAGIC)] != MAGIC:
        raise ExeInventoryError("PS-X EXE magic is not present")

    fields = {
        "initial_pc": _u32(data, 0x10),
        "initial_gp": _u32(data, 0x14),
        "load_address": _u32(data, 0x18),
        "text_bytes": _u32(data, 0x1C),
        "data_address": _u32(data, 0x20),
        "data_bytes": _u32(data, 0x24),
        "bss_address": _u32(data, 0x28),
        "bss_bytes": _u32(data, 0x2C),
        "stack_base": _u32(data, 0x30),
        "stack_offset": _u32(data, 0x34),
    }
    load_address = fields["load_address"]
    text_bytes = fields["text_bytes"]
    initial_pc = fields["initial_pc"]
    if text_bytes == 0 or text_bytes % 4:
        raise ExeInventoryError("text size must be a non-zero multiple of four")
    if load_address % 4 or initial_pc % 4:
        raise ExeInventoryError("load address and initial PC must be 4-byte aligned")
    text_end = load_address + text_bytes
    if text_end > ADDRESS_LIMIT:
        raise ExeInventoryError("text range wraps the 32-bit address space")
    if not load_address <= initial_pc < text_end:
        raise ExeInventoryError("initial PC is outside the text image")
    file_text_end = HEADER_BYTES + text_bytes
    if file_text_end > len(data):
        raise ExeInventoryError("declared text extends beyond the file")

    header = data[:HEADER_BYTES]
    text = data[HEADER_BYTES:file_text_end]
    trailing = data[file_text_end:]
    return {
        "schema": "psxrecomp.ps-x-exe-inventory.v1",
        "status": "ok",
        "file": {
            "name": name,
            "bytes": len(data),
            "sha256": _sha256(data),
        },
        "header": {
            **fields,
            "text_end_exclusive": text_end,
            "sha256": _sha256(header),
        },
        "executable_identity": {
            "load_range": {
                "start": load_address,
                "end_exclusive": text_end,
            },
            "sha256": _sha256(text),
            "relocation_metadata": None,
        },
        "trailing": {
            "bytes": len(trailing),
            "sha256": _sha256(trailing),
        },
        "control_word_scan": scan_control_words(text, load_address),
    }


def inventory(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ExeInventoryError(f"missing input: {path}")
    return parse(path.read_bytes(), path.name)


def _write_text_new(path: Path, value: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(value)
    except FileExistsError as error:
        raise ExeInventoryError(f"refusing to overwrite output: {path}") from error


def _write_bytes_new(path: Path, value: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("xb") as stream:
            stream.write(value)
    except FileExistsError as error:
        raise ExeInventoryError(f"refusing to overwrite output: {path}") from error


def _preflight_outputs(paths: list[Path]) -> None:
    identities: set[str] = set()
    for path in paths:
        identity = os.path.normcase(str(path.resolve(strict=False)))
        if identity in identities:
            raise ExeInventoryError("output paths must be distinct")
        identities.add(identity)
        if path.exists():
            raise ExeInventoryError(f"refusing to overwrite output: {path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--roots-out", type=Path)
    parser.add_argument("--text-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        outputs = [
            path
            for path in (args.json_out, args.roots_out, args.text_out)
            if path is not None
        ]
        _preflight_outputs(outputs)
        if not args.input.is_file():
            raise ExeInventoryError(f"missing input: {args.input}")
        source = args.input.read_bytes()
        report = parse(source, args.input.name)
        if args.text_out:
            text_bytes = int(report["header"]["text_bytes"])
            text = source[HEADER_BYTES : HEADER_BYTES + text_bytes]
            _write_bytes_new(args.text_out, text)
            report["text_output"] = {
                "name": args.text_out.name,
                "bytes": len(text),
                "sha256": _sha256(text),
            }
        if args.roots_out:
            scan = report["control_word_scan"]
            if not isinstance(scan, dict):
                raise ExeInventoryError("control-word scan is not an object")
            targets = scan["unique_in_range_call_targets"]
            if not isinstance(targets, list) or not all(
                isinstance(item, dict) and "target" in item for item in targets
            ):
                raise ExeInventoryError("control-word targets are malformed")
            entry = int(report["header"]["initial_pc"])
            roots = sorted({entry} | {int(item["target"]) for item in targets})
            payload = "".join(f"0x{root:08X}\n" for root in roots)
            _write_text_new(args.roots_out, payload)
            report["untrusted_roots_output"] = {
                "name": args.roots_out.name,
                "count": len(roots),
                "sha256": _sha256(payload.encode()),
            }
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_text_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, ExeInventoryError) as error:
        sys.stderr.write(f"PS-X EXE inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
