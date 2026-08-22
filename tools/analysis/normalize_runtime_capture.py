#!/usr/bin/env python3
"""Validate legacy runtime captures as bounded, untrusted discovery hints.

The legacy runner is quarantined.  This tool verifies the shape, bounds, bytes,
and exact observed-PC sets of its capture manifest, but deliberately makes no
claim that the observations are complete or semantically correct.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any, cast

INPUT_SCHEMA = "psxrecomp overlay capture v2"
OUTPUT_SCHEMA = "wo3se.normalized-runtime-discovery.v1"
ADDRESS_LIMIT = 1 << 32
DEFAULT_GUEST_START = 0x80000000
DEFAULT_GUEST_END = 0x80200000
MAX_INPUT_BYTES = 64 * 1024 * 1024
MAX_CAPTURE_REGIONS = 4096
MAX_TOTAL_REGION_BYTES = 16 * 1024 * 1024
_ADDRESS_RE = re.compile(r"0x[0-9A-F]{8}\Z")


class CaptureValidationError(RuntimeError):
    pass


def _strict_dict(value: object, keys: set[str], path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CaptureValidationError(f"{path} must be an object")
    if set(value) != keys:
        raise CaptureValidationError(f"{path} has unexpected or missing keys")
    return cast(dict[str, Any], value)


def _integer(value: object, path: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise CaptureValidationError(f"{path} must be an integer >= {minimum}")
    return value


def _address(value: object, path: str) -> int:
    if not isinstance(value, str) or _ADDRESS_RE.fullmatch(value) is None:
        raise CaptureValidationError(
            f"{path} must be canonical 0x-prefixed uppercase 32-bit hex"
        )
    result = int(value, 16)
    if result >= ADDRESS_LIMIT or result % 4:
        raise CaptureValidationError(f"{path} must be an aligned 32-bit address")
    return result


def _address_list(
    value: object,
    path: str,
    *,
    region_start: int,
    region_end: int,
) -> list[int]:
    if not isinstance(value, list):
        raise CaptureValidationError(f"{path} must be an array")
    result: list[int] = []
    previous = -1
    for index, raw in enumerate(value):
        address = _address(raw, f"{path}[{index}]")
        if not region_start <= address < region_end:
            raise CaptureValidationError(f"{path}[{index}] is outside its region")
        if address <= previous:
            raise CaptureValidationError(f"{path} must be strictly increasing")
        previous = address
        result.append(address)
    return result


def _validate_guest_range(guest_start: int, guest_end: int) -> None:
    if (
        guest_start % 4
        or guest_end % 4
        or not 0 <= guest_start < guest_end <= ADDRESS_LIMIT
    ):
        raise CaptureValidationError(
            "guest range must be aligned, non-empty, and 32-bit"
        )


def normalize(
    document: object,
    *,
    guest_start: int,
    guest_end: int,
    input_sha256: str,
) -> dict[str, Any]:
    _validate_guest_range(guest_start, guest_end)
    if len(input_sha256) != 64 or any(
        character not in "0123456789abcdef" for character in input_sha256
    ):
        raise CaptureValidationError(
            "input SHA-256 must be 64 lowercase hexadecimal digits"
        )
    if not isinstance(document, list) or not document:
        raise CaptureValidationError("input must be a non-empty capture array")
    if len(document) > MAX_CAPTURE_REGIONS:
        raise CaptureValidationError("input has too many capture regions")

    regions: list[dict[str, Any]] = []
    total_bytes = 0
    total_executed = 0
    all_roots: set[int] = set()
    previous_end = guest_start
    expected_keys = {
        "schema",
        "load_addr",
        "size",
        "bytes_b64",
        "executed_pcs",
        "dispatch_entry_pcs",
        "function_entry_pcs",
        "seeds",
    }

    for index, value in enumerate(document):
        path = f"input[{index}]"
        region = _strict_dict(value, expected_keys, path)
        if region["schema"] != INPUT_SCHEMA:
            raise CaptureValidationError(f"{path}.schema must equal {INPUT_SCHEMA}")
        start = _address(region["load_addr"], f"{path}.load_addr")
        size = _integer(region["size"], f"{path}.size", 4)
        if size % 4:
            raise CaptureValidationError(f"{path}.size must be word-aligned")
        end = start + size
        if end > ADDRESS_LIMIT or not guest_start <= start < end <= guest_end:
            raise CaptureValidationError(f"{path} is outside the authorized range")
        if start < previous_end:
            raise CaptureValidationError("capture regions overlap or are not sorted")
        previous_end = end
        total_bytes += size
        if total_bytes > MAX_TOTAL_REGION_BYTES:
            raise CaptureValidationError("capture regions exceed the byte limit")

        encoded = region["bytes_b64"]
        if not isinstance(encoded, str):
            raise CaptureValidationError(f"{path}.bytes_b64 must be a string")
        try:
            image = base64.b64decode(encoded, validate=True)
        except (binascii.Error, ValueError) as error:
            raise CaptureValidationError(
                f"{path}.bytes_b64 is not canonical base64"
            ) from error
        if len(image) != size:
            raise CaptureValidationError(f"{path}.size does not match decoded bytes")

        executed = _address_list(
            region["executed_pcs"],
            f"{path}.executed_pcs",
            region_start=start,
            region_end=end,
        )
        if not executed:
            raise CaptureValidationError(f"{path}.executed_pcs must be non-empty")
        executed_set = set(executed)
        categories: dict[str, list[int]] = {}
        for name in ("dispatch_entry_pcs", "function_entry_pcs", "seeds"):
            entries = _address_list(
                region[name],
                f"{path}.{name}",
                region_start=start,
                region_end=end,
            )
            if not set(entries).issubset(executed_set):
                raise CaptureValidationError(
                    f"{path}.{name} must be a subset of executed_pcs"
                )
            categories[name] = entries
        roots = sorted(
            set(categories["dispatch_entry_pcs"])
            | set(categories["function_entry_pcs"])
            | set(categories["seeds"])
        )
        all_roots.update(roots)
        total_executed += len(executed)
        regions.append(
            {
                "load_addr": start,
                "end_exclusive": end,
                "size": size,
                "bytes_sha256": hashlib.sha256(image).hexdigest(),
                "executed_pc_count": len(executed),
                "executed_pcs": executed,
                "dispatch_entry_pcs": categories["dispatch_entry_pcs"],
                "function_entry_pcs": categories["function_entry_pcs"],
                "seed_pcs": categories["seeds"],
                "candidate_roots": roots,
            }
        )

    return {
        "schema": OUTPUT_SCHEMA,
        "status": "valid-untrusted-discovery",
        "classification": (
            "quarantined legacy observation; discovery only; not semantic or "
            "executable-coverage proof"
        ),
        "source": {"schema": INPUT_SCHEMA, "sha256": input_sha256},
        "authorized_guest_range": {
            "start": guest_start,
            "end_exclusive": guest_end,
        },
        "region_count": len(regions),
        "total_region_bytes": total_bytes,
        "total_executed_pc_count": total_executed,
        "total_candidate_root_count": len(all_roots),
        "regions": regions,
    }


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise CaptureValidationError(f"refusing to overwrite output: {path}") from error


def _preflight_outputs(paths: list[Path]) -> None:
    identities: set[str] = set()
    for path in paths:
        identity = os.path.normcase(str(path.resolve(strict=False)))
        if identity in identities:
            raise CaptureValidationError("output paths must be distinct")
        identities.add(identity)
        if path.exists():
            raise CaptureValidationError(f"refusing to overwrite output: {path}")


def _flatten(report: dict[str, Any], field: str) -> list[int]:
    return sorted(
        {
            address
            for region in cast(list[dict[str, Any]], report["regions"])
            for address in cast(list[int], region[field])
        }
    )


def _address_payload(addresses: list[int]) -> str:
    return "".join(f"0x{address:08X}\n" for address in addresses)


def _extract_images(
    document: list[object], report: dict[str, Any], output: Path
) -> list[dict[str, Any]]:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f"{output.name}.tmp-", dir=output.parent))
    receipts: list[dict[str, Any]] = []
    try:
        report_regions = cast(list[dict[str, Any]], report["regions"])
        for index, (raw_region, region) in enumerate(
            zip(document, report_regions, strict=True)
        ):
            fields = cast(dict[str, Any], raw_region)
            image = base64.b64decode(cast(str, fields["bytes_b64"]), validate=True)
            start = cast(int, region["load_addr"])
            end = cast(int, region["end_exclusive"])
            sha256 = cast(str, region["bytes_sha256"])
            name = f"{index:04d}-{start:08X}-{end:08X}-{sha256}.bin"
            destination = temporary / name
            with destination.open("xb") as stream:
                stream.write(image)
            receipts.append({"name": name, "size": len(image), "sha256": sha256})
        temporary.replace(output)
        return receipts
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument(
        "--guest-start",
        type=lambda value: int(value, 0),
        default=DEFAULT_GUEST_START,
    )
    parser.add_argument(
        "--guest-end", type=lambda value: int(value, 0), default=DEFAULT_GUEST_END
    )
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--executed-pcs-out", type=Path)
    parser.add_argument("--roots-out", type=Path)
    parser.add_argument(
        "--images-dir",
        type=Path,
        help="optional new directory for hash-named captured region images",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        outputs = [
            path
            for path in (
                args.json_out,
                args.executed_pcs_out,
                args.roots_out,
                args.images_dir,
            )
            if path is not None
        ]
        _preflight_outputs(outputs)
        if args.input.stat().st_size > MAX_INPUT_BYTES:
            raise CaptureValidationError("input exceeds the file-size limit")
        raw = args.input.read_bytes()
        document = json.loads(raw)
        report = normalize(
            document,
            guest_start=args.guest_start,
            guest_end=args.guest_end,
            input_sha256=hashlib.sha256(raw).hexdigest(),
        )
        if args.images_dir:
            report["images_output"] = {
                "directory": args.images_dir.name,
                "files": _extract_images(document, report, args.images_dir),
            }
        if args.executed_pcs_out:
            executed_payload = _address_payload(_flatten(report, "executed_pcs"))
            _write_new(args.executed_pcs_out, executed_payload)
            report["executed_pcs_output"] = {
                "name": args.executed_pcs_out.name,
                "sha256": hashlib.sha256(executed_payload.encode()).hexdigest(),
            }
        if args.roots_out:
            roots_payload = _address_payload(_flatten(report, "candidate_roots"))
            _write_new(args.roots_out, roots_payload)
            report["roots_output"] = {
                "name": args.roots_out.name,
                "sha256": hashlib.sha256(roots_payload.encode()).hexdigest(),
            }
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (
        OSError,
        UnicodeError,
        json.JSONDecodeError,
        CaptureValidationError,
    ) as error:
        sys.stderr.write(f"runtime-capture normalization failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
