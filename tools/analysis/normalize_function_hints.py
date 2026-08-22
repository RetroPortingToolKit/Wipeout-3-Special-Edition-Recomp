#!/usr/bin/env python3
"""Validate and normalize untrusted function-analysis hints.

The result remains a discovery receipt.  It cannot establish executed-PC
coverage or authorize compilation without clean compiler/runtime evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any, cast

INPUT_SCHEMA = "psxrecomp.ghidra-function-hints.v1"
OUTPUT_SCHEMA = "psxrecomp.normalized-function-hints.v1"
ADDRESS_LIMIT = 1 << 32


class HintValidationError(RuntimeError):
    pass


def _strict_dict(value: object, keys: set[str], path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise HintValidationError(f"{path} must be an object")
    if set(value) != keys:
        raise HintValidationError(f"{path} has unexpected or missing keys")
    return cast(dict[str, Any], value)


def _integer(value: object, path: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise HintValidationError(f"{path} must be an integer >= {minimum}")
    return value


def _aligned_address(value: object, path: str) -> int:
    result = _integer(value, path)
    if result >= ADDRESS_LIMIT or result % 4:
        raise HintValidationError(f"{path} must be an aligned 32-bit address")
    return result


def _coalesce(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    result: list[tuple[int, int]] = []
    for start, end in sorted(ranges):
        if result and start <= result[-1][1]:
            result[-1] = (result[-1][0], max(result[-1][1], end))
        else:
            result.append((start, end))
    return result


def normalize(
    document: object,
    *,
    image_start: int,
    image_end_exclusive: int,
    input_sha256: str,
) -> dict[str, Any]:
    if (
        image_start % 4
        or image_end_exclusive % 4
        or not 0 <= image_start < image_end_exclusive <= ADDRESS_LIMIT
    ):
        raise HintValidationError("image range must be aligned, non-empty, and 32-bit")
    if len(input_sha256) != 64 or any(
        character not in "0123456789abcdef" for character in input_sha256
    ):
        raise HintValidationError(
            "input SHA-256 must be 64 lowercase hexadecimal digits"
        )
    root = _strict_dict(
        document,
        {
            "schema",
            "classification",
            "language",
            "function_count",
            "instruction_count",
            "functions",
        },
        "input",
    )
    if root["schema"] != INPUT_SCHEMA:
        raise HintValidationError(f"input.schema must equal {INPUT_SCHEMA}")
    if (
        not isinstance(root["classification"], str)
        or "untrusted" not in root["classification"]
    ):
        raise HintValidationError(
            "input classification must remain explicitly untrusted"
        )
    if not isinstance(root["language"], str) or not root["language"]:
        raise HintValidationError("input.language must be a non-empty string")
    functions = root["functions"]
    if not isinstance(functions, list):
        raise HintValidationError("input.functions must be an array")
    if _integer(root["function_count"], "input.function_count") != len(functions):
        raise HintValidationError("input.function_count does not match the array")

    entries: list[int] = []
    all_ranges: list[tuple[int, int]] = []
    instruction_total = 0
    previous_entry = -1
    for index, value in enumerate(functions):
        path = f"input.functions[{index}]"
        function = _strict_dict(
            value,
            {
                "entry",
                "name",
                "is_thunk",
                "body_bytes",
                "body_ranges",
                "instruction_count",
            },
            path,
        )
        entry = _aligned_address(function["entry"], f"{path}.entry")
        if not image_start <= entry < image_end_exclusive:
            raise HintValidationError(f"{path}.entry is outside the image")
        if previous_entry >= 0 and entry <= previous_entry:
            raise HintValidationError("function entries must be strictly increasing")
        previous_entry = entry
        if not isinstance(function["name"], str) or not function["name"]:
            raise HintValidationError(f"{path}.name must be a non-empty string")
        if type(function["is_thunk"]) is not bool:
            raise HintValidationError(f"{path}.is_thunk must be boolean")
        body_bytes = _integer(function["body_bytes"], f"{path}.body_bytes", 1)
        instruction_count = _integer(
            function["instruction_count"], f"{path}.instruction_count", 1
        )
        if instruction_count * 4 > body_bytes:
            raise HintValidationError(
                f"{path} has more instruction bytes than body bytes"
            )
        ranges = function["body_ranges"]
        if not isinstance(ranges, list) or not ranges:
            raise HintValidationError(f"{path}.body_ranges must be non-empty")
        parsed: list[tuple[int, int]] = []
        range_bytes = 0
        for range_index, range_value in enumerate(ranges):
            range_path = f"{path}.body_ranges[{range_index}]"
            fields = _strict_dict(range_value, {"start", "end_exclusive"}, range_path)
            start = _aligned_address(fields["start"], f"{range_path}.start")
            end = _aligned_address(
                fields["end_exclusive"], f"{range_path}.end_exclusive"
            )
            if not image_start <= start < end <= image_end_exclusive:
                raise HintValidationError(f"{range_path} is outside the image")
            if parsed and start <= parsed[-1][1]:
                raise HintValidationError(
                    f"{path}.body_ranges overlap or are not sorted"
                )
            parsed.append((start, end))
            range_bytes += end - start
        if range_bytes != body_bytes:
            raise HintValidationError(f"{path}.body_bytes does not match its ranges")
        if not any(start <= entry < end for start, end in parsed):
            raise HintValidationError(f"{path}.entry is outside its function body")
        entries.append(entry)
        all_ranges.extend(parsed)
        instruction_total += instruction_count

    if (
        _integer(root["instruction_count"], "input.instruction_count")
        != instruction_total
    ):
        raise HintValidationError(
            "input.instruction_count does not match its functions"
        )
    union = _coalesce(all_ranges)
    union_bytes = sum(end - start for start, end in union)
    image_bytes = image_end_exclusive - image_start
    return {
        "schema": OUTPUT_SCHEMA,
        "status": "valid-untrusted-hints",
        "classification": "discovery only; not executable coverage",
        "source": {
            "schema": INPUT_SCHEMA,
            "sha256": input_sha256,
            "language": root["language"],
        },
        "image_range": {"start": image_start, "end_exclusive": image_end_exclusive},
        "function_count": len(entries),
        "instruction_count": instruction_total,
        "candidate_roots": entries,
        "candidate_body_union": [
            {"start": start, "end_exclusive": end} for start, end in union
        ],
        "candidate_body_union_bytes": union_bytes,
        "image_bytes": image_bytes,
    }


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise HintValidationError(f"refusing to overwrite output: {path}") from error


def _preflight_outputs(paths: list[Path]) -> None:
    identities: set[str] = set()
    for path in paths:
        identity = os.path.normcase(str(path.resolve(strict=False)))
        if identity in identities:
            raise HintValidationError("output paths must be distinct")
        identities.add(identity)
        if path.exists():
            raise HintValidationError(f"refusing to overwrite output: {path}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument(
        "--image-start", type=lambda value: int(value, 0), required=True
    )
    parser.add_argument("--image-end", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--roots-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        _preflight_outputs(
            [path for path in (args.json_out, args.roots_out) if path is not None]
        )
        raw = args.input.read_bytes()
        document = json.loads(raw)
        report = normalize(
            document,
            image_start=args.image_start,
            image_end_exclusive=args.image_end,
            input_sha256=hashlib.sha256(raw).hexdigest(),
        )
        if args.roots_out:
            roots = "".join(f"0x{root:08X}\n" for root in report["candidate_roots"])
            _write_new(args.roots_out, roots)
            report["roots_output"] = {
                "name": args.roots_out.name,
                "sha256": hashlib.sha256(roots.encode()).hexdigest(),
            }
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, UnicodeError, json.JSONDecodeError, HintValidationError) as error:
        sys.stderr.write(f"function-hint normalization failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
