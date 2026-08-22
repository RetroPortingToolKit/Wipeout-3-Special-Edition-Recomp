#!/usr/bin/env python3
"""Boundedly decode the bit-packed LZSS streams found in local asset containers.

This is an independent format implementation.  It deliberately exposes strict
bounds and consumption receipts so malformed or misidentified payloads fail
closed instead of becoming inputs to the static compiler.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from dataclasses import dataclass
from pathlib import Path

INDEX_BITS = 13
LENGTH_BITS = 4
WINDOW_BYTES = 1 << INDEX_BITS
INITIAL_WRITE_POSITION = 1
MINIMUM_MATCH_BYTES = 3
END_MARKER = 0
DEFAULT_MAX_OUTPUT_BYTES = 64 * 1024 * 1024


class LzssFormatError(RuntimeError):
    pass


class _BitReader:
    def __init__(self, data: bytes) -> None:
        self._data = data
        self.bit_offset = 0

    def read(self, width: int) -> int:
        if width <= 0:
            raise ValueError("bit width must be positive")
        if self.bit_offset + width > len(self._data) * 8:
            raise LzssFormatError("compressed stream ends inside a token")
        value = 0
        for _ in range(width):
            byte = self._data[self.bit_offset // 8]
            shift = 7 - (self.bit_offset % 8)
            value = (value << 1) | ((byte >> shift) & 1)
            self.bit_offset += 1
        return value

    def require_zero_padding_to_end(self) -> int:
        padding_bits = len(self._data) * 8 - self.bit_offset
        while self.bit_offset < len(self._data) * 8:
            if self.read(1) != 0:
                raise LzssFormatError("non-zero data follows the end marker")
        return padding_bits


@dataclass(frozen=True)
class DecodeReceipt:
    input_bytes: int
    output_bytes: int
    tokens: int
    literal_tokens: int
    match_tokens: int
    consumed_bits: int
    padding_bits: int

    def as_dict(self) -> dict[str, int]:
        return {
            "input_bytes": self.input_bytes,
            "output_bytes": self.output_bytes,
            "tokens": self.tokens,
            "literal_tokens": self.literal_tokens,
            "match_tokens": self.match_tokens,
            "consumed_bits": self.consumed_bits,
            "padding_bits": self.padding_bits,
        }


def decode(
    data: bytes,
    *,
    expected_size: int | None = None,
    max_output_bytes: int = DEFAULT_MAX_OUTPUT_BYTES,
) -> tuple[bytes, DecodeReceipt]:
    """Decode one stream and require an explicit end marker and zero padding."""

    if expected_size is not None and expected_size < 0:
        raise LzssFormatError("expected output size cannot be negative")
    if max_output_bytes <= 0:
        raise LzssFormatError("maximum output size must be positive")
    if expected_size is not None and expected_size > max_output_bytes:
        raise LzssFormatError("expected output exceeds the configured bound")

    reader = _BitReader(data)
    window = bytearray(WINDOW_BYTES)
    initialized = bytearray(WINDOW_BYTES)
    write_position = INITIAL_WRITE_POSITION
    output = bytearray()
    literals = 0
    matches = 0

    def append(value: int) -> None:
        nonlocal write_position
        if len(output) >= max_output_bytes:
            raise LzssFormatError("decoded output exceeds the configured bound")
        if expected_size is not None and len(output) >= expected_size:
            raise LzssFormatError("decoded output exceeds the expected size")
        output.append(value)
        window[write_position] = value
        initialized[write_position] = 1
        write_position = (write_position + 1) & (WINDOW_BYTES - 1)

    while True:
        is_literal = reader.read(1)
        if is_literal:
            append(reader.read(8))
            literals += 1
            continue

        match_position = reader.read(INDEX_BITS)
        if match_position == END_MARKER:
            break
        match_bytes = reader.read(LENGTH_BITS) + MINIMUM_MATCH_BYTES
        for index in range(match_bytes):
            source = (match_position + index) & (WINDOW_BYTES - 1)
            if not initialized[source]:
                raise LzssFormatError(
                    f"back-reference reads unwritten window byte {source}"
                )
            append(window[source])
        matches += 1

    consumed_bits = reader.bit_offset
    padding_bits = reader.require_zero_padding_to_end()
    if expected_size is not None and len(output) != expected_size:
        raise LzssFormatError(
            f"decoded size {len(output)} does not match expected {expected_size}"
        )
    receipt = DecodeReceipt(
        input_bytes=len(data),
        output_bytes=len(output),
        tokens=literals + matches,
        literal_tokens=literals,
        match_tokens=matches,
        consumed_bits=consumed_bits,
        padding_bits=padding_bits,
    )
    return bytes(output), receipt


def _write_new(path: Path, payload: bytes) -> None:
    if path.exists():
        raise LzssFormatError(f"refusing to overwrite output: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".partial")
    if temporary.exists():
        raise LzssFormatError(f"refusing to overwrite partial output: {temporary}")
    try:
        temporary.write_bytes(payload)
        temporary.replace(path)
    except Exception:
        if temporary.exists():
            temporary.unlink()
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--expected-size", type=int)
    parser.add_argument(
        "--max-output-bytes", type=int, default=DEFAULT_MAX_OUTPUT_BYTES
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        compressed = args.input.read_bytes()
        decoded, receipt = decode(
            compressed,
            expected_size=args.expected_size,
            max_output_bytes=args.max_output_bytes,
        )
        _write_new(args.output, decoded)
        report: dict[str, object] = {
            "schema": "psxrecomp.bounded-lzss.v1",
            "status": "ok",
            "input": {
                "name": args.input.name,
                "sha256": hashlib.sha256(compressed).hexdigest(),
            },
            "output": {
                "name": args.output.name,
                "sha256": hashlib.sha256(decoded).hexdigest(),
            },
            "decode": receipt.as_dict(),
        }
        payload = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode()
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.buffer.write(payload)
        return 0
    except (OSError, LzssFormatError) as error:
        sys.stderr.write(f"LZSS decode failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
