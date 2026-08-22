#!/usr/bin/env python3
"""Compare one observed RAM region with an exact authenticated source slice."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any

NORMALIZED_CAPTURE_SCHEMA = "wo3se.normalized-runtime-discovery.v1"
OUTPUT_SCHEMA = "wo3se.runtime-variant-inventory.v1"
MAX_EXECUTED_PCS = 2_000_000


class RuntimeVariantError(RuntimeError):
    pass


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _load_delta_module() -> Any:
    path = Path(__file__).with_name("inventory_binary_delta.py")
    spec = importlib.util.spec_from_file_location("runtime_variant_binary_delta", path)
    if spec is None or spec.loader is None:
        raise RuntimeVariantError(f"cannot load binary-delta inventory: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _capture_region(
    normalized: dict[str, Any], runtime_sha256: str, guest_base: int, size: int
) -> dict[str, Any]:
    if normalized.get("schema") != NORMALIZED_CAPTURE_SCHEMA:
        raise RuntimeVariantError("unsupported normalized-capture schema")
    if normalized.get("status") != "valid-untrusted-discovery":
        raise RuntimeVariantError("normalized capture is not valid discovery evidence")
    regions = normalized.get("regions")
    if not isinstance(regions, list):
        raise RuntimeVariantError("normalized capture has no region array")
    matches = [
        region
        for region in regions
        if isinstance(region, dict)
        and region.get("bytes_sha256") == runtime_sha256
        and region.get("load_addr") == guest_base
        and region.get("size") == size
    ]
    if len(matches) != 1:
        raise RuntimeVariantError(
            "runtime image must match exactly one normalized capture region"
        )
    return matches[0]


def inventory(
    source: bytes,
    runtime: bytes,
    *,
    source_file_sha256: str,
    source_offset: int,
    guest_base: int,
    capture_region: dict[str, Any],
    runtime_offset: int = 0,
    comparison_bytes: int | None = None,
) -> dict[str, Any]:
    if source_file_sha256 != _sha256(source):
        raise RuntimeVariantError("source-file digest does not match supplied bytes")
    if runtime_offset < 0 or runtime_offset > len(runtime) or runtime_offset % 4:
        raise RuntimeVariantError("runtime offset must be word-aligned within the region")
    available_size = len(runtime) - runtime_offset
    compared_size = available_size if comparison_bytes is None else comparison_bytes
    if compared_size <= 0 or compared_size > available_size or compared_size % 4:
        raise RuntimeVariantError("comparison size must be word-aligned within the region")
    if source_offset < 0 or source_offset + compared_size > len(source):
        raise RuntimeVariantError("source slice lies outside the authenticated source")
    compared_guest_base = guest_base + runtime_offset
    if (
        guest_base < 0
        or guest_base % 4
        or guest_base + len(runtime) > 1 << 32
        or compared_guest_base + compared_size > 1 << 32
    ):
        raise RuntimeVariantError("runtime guest range must be aligned and 32-bit")
    if not runtime or len(runtime) % 4:
        raise RuntimeVariantError("runtime region must be non-empty and word-aligned")
    runtime_sha256 = _sha256(runtime)
    if (
        capture_region.get("bytes_sha256") != runtime_sha256
        or capture_region.get("load_addr") != guest_base
        or capture_region.get("size") != len(runtime)
    ):
        raise RuntimeVariantError("capture-region identity does not match runtime bytes")

    raw_pcs = capture_region.get("executed_pcs")
    if not isinstance(raw_pcs, list) or len(raw_pcs) > MAX_EXECUTED_PCS:
        raise RuntimeVariantError("capture-region executed-PC list is invalid")
    if not all(isinstance(pc, int) for pc in raw_pcs):
        raise RuntimeVariantError("capture-region executed PCs must be integers")
    executed_pcs = list(raw_pcs)
    if executed_pcs != sorted(set(executed_pcs)):
        raise RuntimeVariantError("capture-region executed PCs must be sorted and unique")
    capture_end_exclusive = guest_base + len(runtime)
    if any(
        pc % 4 or pc < guest_base or pc + 4 > capture_end_exclusive
        for pc in executed_pcs
    ):
        raise RuntimeVariantError("capture-region executed PC lies outside the region")
    end_exclusive = compared_guest_base + compared_size
    compared_pcs = [
        pc
        for pc in executed_pcs
        if compared_guest_base <= pc and pc + 4 <= end_exclusive
    ]

    source_slice = source[source_offset : source_offset + compared_size]
    runtime_slice = runtime[runtime_offset : runtime_offset + compared_size]
    delta_module = _load_delta_module()
    try:
        delta = delta_module.inventory(
            source_slice, runtime_slice, guest_base=compared_guest_base
        )
    except delta_module.DeltaInventoryError as error:
        raise RuntimeVariantError(str(error)) from error

    changed_observed: list[dict[str, Any]] = []
    changed_bytes_in_observed_words = 0
    for pc in compared_pcs:
        offset = pc - compared_guest_base
        source_word = source_slice[offset : offset + 4]
        runtime_word = runtime_slice[offset : offset + 4]
        if source_word == runtime_word:
            continue
        changed_bytes_in_observed_words += sum(
            left != right for left, right in zip(source_word, runtime_word)
        )
        changed_observed.append(
            {
                "pc": pc,
                "source_word_le": source_word.hex(),
                "runtime_word_le": runtime_word.hex(),
            }
        )

    return {
        "schema": OUTPUT_SCHEMA,
        "status": "exact-source" if delta["status"] == "identical" else "runtime-variant",
        "classification": (
            "exact source-slice delta against quarantined observed RAM; proves byte "
            "identity/difference only, not semantic or executable coverage"
        ),
        "source": {
            "file_sha256": source_file_sha256,
            "file_bytes": len(source),
            "slice_offset": source_offset,
            "slice_bytes": len(source_slice),
            "slice_sha256": _sha256(source_slice),
        },
        "runtime": {
            "capture_guest_start": guest_base,
            "capture_guest_end_exclusive": capture_end_exclusive,
            "comparison_offset": runtime_offset,
            "guest_start": compared_guest_base,
            "guest_end_exclusive": end_exclusive,
            "capture_bytes": len(runtime),
            "capture_sha256": runtime_sha256,
            "compared_bytes": len(runtime_slice),
            "compared_sha256": _sha256(runtime_slice),
        },
        "observed_execution": {
            "pc_count": len(compared_pcs),
            "changed_pc_count": len(changed_observed),
            "changed_bytes_in_observed_words": changed_bytes_in_observed_words,
            "changed_pcs": changed_observed,
        },
        "delta": delta,
    }


def _write_new(path: Path, payload: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
    except FileExistsError as error:
        raise RuntimeVariantError(f"refusing to overwrite output: {path}") from error


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--source-offset", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--runtime-region", type=Path, required=True)
    parser.add_argument("--guest-base", type=lambda value: int(value, 0), required=True)
    parser.add_argument(
        "--runtime-offset",
        type=lambda value: int(value, 0),
        default=0,
        help="optional word-aligned offset of the compared slice within the capture",
    )
    parser.add_argument(
        "--comparison-bytes",
        type=lambda value: int(value, 0),
        help="optional word-aligned prefix length when the capture extends past the source",
    )
    parser.add_argument("--normalized-capture", type=Path, required=True)
    parser.add_argument("--json-out", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        for path in (args.source, args.runtime_region, args.normalized_capture):
            if not path.is_file():
                raise RuntimeVariantError(f"missing input: {path}")
        source = args.source.read_bytes()
        runtime = args.runtime_region.read_bytes()
        normalized = json.loads(args.normalized_capture.read_text(encoding="utf-8"))
        if not isinstance(normalized, dict):
            raise RuntimeVariantError("normalized capture must be a JSON object")
        runtime_sha256 = _sha256(runtime)
        region = _capture_region(
            normalized, runtime_sha256, args.guest_base, len(runtime)
        )
        report = inventory(
            source,
            runtime,
            source_file_sha256=_sha256(source),
            source_offset=args.source_offset,
            guest_base=args.guest_base,
            capture_region=region,
            runtime_offset=args.runtime_offset,
            comparison_bytes=args.comparison_bytes,
        )
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            _write_new(args.json_out, payload)
        sys.stdout.write(payload)
        return 0
    except (OSError, ValueError, json.JSONDecodeError, RuntimeVariantError) as error:
        sys.stderr.write(f"runtime-variant inventory failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
