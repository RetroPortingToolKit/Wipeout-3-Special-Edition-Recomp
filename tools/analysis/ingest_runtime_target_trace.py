#!/usr/bin/env python3
"""Turn a finalized runtime target trace into identity-bound AOT manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any

TRACE_SCHEMA = "clean-aot-runtime-target-trace-v1"
CLOSURE_SCHEMA = "clean-aot-runtime-target-closure-v1"
ID = re.compile(r"[a-z0-9][a-z0-9._-]*\Z")
HEX64 = re.compile(r"[0-9a-f]{64}\Z")


class TraceError(RuntimeError):
    pass


def _object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise TraceError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _keys(value: dict[str, Any], required: set[str], context: str) -> None:
    if set(value) != required:
        missing = sorted(required - set(value))
        extra = sorted(set(value) - required)
        raise TraceError(f"{context} fields differ: missing={missing}, extra={extra}")


def _u32(value: Any, context: str) -> int:
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise TraceError(f"{context} must be a uint32")
    return value


def _positive(value: Any, context: str) -> int:
    if type(value) is not int or value <= 0:
        raise TraceError(f"{context} must be positive")
    return value


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def parse_image_argument(value: str) -> tuple[str, Path]:
    name, separator, path = value.partition("=")
    if not separator or not ID.fullmatch(name) or not path:
        raise argparse.ArgumentTypeError("image must be ID=PATH")
    return name, Path(path)


def build_closure(receipt_bytes: bytes,
                  supplied_images: dict[str, bytes]) -> tuple[dict[str, Any], dict[str, str]]:
    try:
        receipt = json.loads(receipt_bytes, object_pairs_hook=_object)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise TraceError(f"invalid trace JSON: {error}") from error
    if not isinstance(receipt, dict):
        raise TraceError("trace must be a JSON object")
    if set(receipt) not in ({"capture", "images", "schema", "sites"},
                            {"capture", "images", "schema", "sites", "telemetry"}):
        raise TraceError("trace fields differ")
    if receipt["schema"] != TRACE_SCHEMA:
        raise TraceError("unsupported trace schema")

    capture = receipt["capture"]
    if not isinstance(capture, dict):
        raise TraceError("capture must be an object")
    _keys(capture, {"complete", "dropped_events", "event_count", "first_sequence",
                    "last_sequence"}, "capture")
    if capture["complete"] is not True or capture["dropped_events"] != 0:
        raise TraceError("trace is not complete or reports dropped events")
    event_count = _positive(capture["event_count"], "capture.event_count")
    first = _u32(capture["first_sequence"], "capture.first_sequence")
    last = _u32(capture["last_sequence"], "capture.last_sequence")
    if last < first or last - first + 1 != event_count:
        raise TraceError("capture sequence interval is not gap-free")

    image_rows = receipt["images"]
    if not isinstance(image_rows, list) or not image_rows:
        raise TraceError("images must be a non-empty array")
    if supplied_images.keys() != {row.get("id") for row in image_rows
                                  if isinstance(row, dict)}:
        raise TraceError("supplied image IDs do not exactly match the receipt")
    images: dict[str, dict[str, Any]] = {}
    for row in image_rows:
        if not isinstance(row, dict):
            raise TraceError("image must be an object")
        _keys(row, {"id", "image_kind", "load_end_exclusive", "load_start",
                    "sha256", "variant_sha256"}, "image")
        name = row["id"]
        if not isinstance(name, str) or not ID.fullmatch(name) or name in images:
            raise TraceError("image IDs must be safe and unique")
        if row["image_kind"] not in {"raw-payload", "ps-x-exe"}:
            raise TraceError("unsupported image kind")
        start = _u32(row["load_start"], f"{name}.load_start")
        end = row["load_end_exclusive"]
        if type(end) is not int or not start < end <= 1 << 32 or (start | end) & 3:
            raise TraceError(f"{name} has an invalid aligned load range")
        for field in ("sha256", "variant_sha256"):
            if not isinstance(row[field], str) or not HEX64.fullmatch(row[field]):
                raise TraceError(f"{name}.{field} must be lowercase SHA-256")
        data = supplied_images[name]
        if len(data) != end - start or sha256(data) != row["sha256"]:
            raise TraceError(f"{name} bytes do not match the receipt identity")
        images[name] = row
    if [row["id"] for row in image_rows] != sorted(images):
        raise TraceError("images must be sorted by ID")

    telemetry = receipt.get("telemetry")
    if telemetry is not None:
        if not isinstance(telemetry, dict):
            raise TraceError("telemetry must be an object")
        _keys(telemetry, {"complete", "dropped_events", "event_count", "events",
                          "first_sequence", "last_sequence"}, "telemetry")
        if (telemetry["complete"] != capture["complete"] or
                telemetry["dropped_events"] != capture["dropped_events"]):
            raise TraceError("telemetry completeness disagrees with capture")
        telemetry_count = _positive(telemetry["event_count"], "telemetry.event_count")
        telemetry_first = _u32(telemetry["first_sequence"], "telemetry.first_sequence")
        telemetry_last = _u32(telemetry["last_sequence"], "telemetry.last_sequence")
        events = telemetry["events"]
        if (not isinstance(events, list) or len(events) != telemetry_count or
                telemetry_last < telemetry_first or
                telemetry_last - telemetry_first + 1 != telemetry_count):
            raise TraceError("telemetry sequence interval is not gap-free")
        accepted_targets = 0
        for index, event in enumerate(events):
            if not isinstance(event, dict):
                raise TraceError("telemetry event must be an object")
            _keys(event, {"accepted", "active_epoch_identity_sha256",
                          "destination_image", "destination_pc", "kind", "sequence",
                          "source_image", "source_pc", "status", "transfer"},
                  "telemetry event")
            if event["sequence"] != telemetry_first + index:
                raise TraceError("telemetry event sequence is not contiguous")
            if (not isinstance(event["active_epoch_identity_sha256"], str) or
                    not HEX64.fullmatch(event["active_epoch_identity_sha256"])):
                raise TraceError("telemetry active epoch identity is invalid")
            if event["kind"] not in {"epoch", "dispatch", "target-attempt"}:
                raise TraceError("telemetry event kind is invalid")
            if event["transfer"] not in {"none", "indirect-call", "indirect-jump"}:
                raise TraceError("telemetry transfer is invalid")
            if type(event["accepted"]) is not bool:
                raise TraceError("telemetry accepted must be boolean")
            _u32(event["source_pc"], "telemetry source PC")
            _u32(event["destination_pc"], "telemetry destination PC")
            for field in ("source_image", "destination_image"):
                if event[field] != "unknown" and event[field] not in images:
                    raise TraceError("telemetry event names an unknown image")
            if event["accepted"] != (event["status"] == "executed"):
                raise TraceError("telemetry accepted/status mismatch")
            if event["kind"] == "target-attempt" and event["accepted"]:
                accepted_targets += 1
        if accepted_targets != event_count:
            raise TraceError("telemetry accepted targets disagree with capture")

    sites = receipt["sites"]
    if not isinstance(sites, list) or not sites:
        raise TraceError("sites must be a non-empty array")
    closed_sites: list[dict[str, Any]] = []
    total = 0
    previous_site: tuple[str, int] | None = None
    roots: dict[str, set[int]] = {name: set() for name in images}
    for site in sites:
        if not isinstance(site, dict):
            raise TraceError("site must be an object")
        _keys(site, {"complete", "observations", "source_image", "source_pc",
                     "transfer"}, "site")
        if site["complete"] is not True:
            raise TraceError("every site must carry an explicit complete claim")
        source_image = site["source_image"]
        if source_image not in images:
            raise TraceError("site names an unknown source image")
        source_pc = _u32(site["source_pc"], "site.source_pc")
        source = images[source_image]
        if source_pc & 3 or not source["load_start"] <= source_pc < source["load_end_exclusive"]:
            raise TraceError("source PC is outside its image")
        site_key = (source_image, source_pc)
        if previous_site is not None and site_key <= previous_site:
            raise TraceError("sites must be sorted and unique")
        previous_site = site_key
        offset = source_pc - source["load_start"]
        word = struct.unpack_from("<I", supplied_images[source_image], offset)[0]
        function = word & 0x3F
        if word >> 26 != 0 or function not in {8, 9}:
            raise TraceError("source PC is not an authenticated JR/JALR")
        rs = (word >> 21) & 31
        rd = (word >> 11) & 31
        if function == 8 and rs == 31:
            raise TraceError("return sites are not finite-target closure evidence")
        actual_transfer = (
            "indirect-call" if function == 9 and rd != 0 else "indirect-jump"
        )
        if site["transfer"] != actual_transfer:
            raise TraceError("declared transfer disagrees with source opcode")

        observations = site["observations"]
        if not isinstance(observations, list) or not observations:
            raise TraceError("closed sites require at least one observed target")
        targets: list[dict[str, Any]] = []
        previous_target: tuple[str, int] | None = None
        for observation in observations:
            if not isinstance(observation, dict):
                raise TraceError("observation must be an object")
            _keys(observation, {"count", "destination_image", "destination_pc"},
                  "observation")
            destination_image = observation["destination_image"]
            if destination_image not in images:
                raise TraceError("observation names an unknown destination image")
            destination_pc = _u32(observation["destination_pc"],
                                  "observation.destination_pc")
            destination = images[destination_image]
            if destination_pc & 3 or not destination["load_start"] <= destination_pc < destination["load_end_exclusive"]:
                raise TraceError("destination PC is outside its image")
            target_key = (destination_image, destination_pc)
            if previous_target is not None and target_key <= previous_target:
                raise TraceError("observations must be sorted and unique")
            previous_target = target_key
            count = _positive(observation["count"], "observation.count")
            total += count
            roots[destination_image].add(destination_pc)
            targets.append({"count": count, "image": destination_image,
                            "pc": destination_pc})
        closed_sites.append({"source_image": source_image, "source_pc": source_pc,
                             "targets": targets, "transfer": actual_transfer})
    if total != event_count:
        raise TraceError("observation counts do not equal capture.event_count")

    receipt_digest = sha256(canonical(receipt))
    regions = []
    manifests: dict[str, str] = {}
    for name in sorted(images):
        entries = sorted(roots[name])
        if not entries:
            continue
        row = images[name]
        regions.append({"entries": entries, "image": name,
                        "start": row["load_start"],
                        "end_exclusive": row["load_end_exclusive"]})
        lines = ["schema=clean-aot-compiler-manifest-v1",
                 f"image_kind={row['image_kind']}",
                 f"variant_sha256={row['variant_sha256']}",
                 f"image_sha256={row['sha256']}",
                 f"load_start=0x{row['load_start']:08x}",
                 f"load_end_exclusive=0x{row['load_end_exclusive']:08x}"]
        lines.extend(f"entry=0x{entry:08x}" for entry in entries)
        lines.append(f"code=0x{row['load_start']:08x}-0x{row['load_end_exclusive']:08x}")
        manifests[name] = "\n".join(lines) + "\n"
    closure = {"images": image_rows, "regions": regions,
               "schema": CLOSURE_SCHEMA, "sites": closed_sites,
               "status": "authenticated-finite",
               "trace_receipt_sha256": receipt_digest}
    return closure, manifests


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--image", type=parse_image_argument, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        pairs = args.image
        if len({name for name, _ in pairs}) != len(pairs):
            raise TraceError("supplied image IDs must be unique")
        supplied = {name: path.read_bytes() for name, path in pairs}
        closure, manifests = build_closure(args.trace.read_bytes(), supplied)
        if args.output.exists():
            raise TraceError("output already exists")
        args.output.mkdir(parents=True)
        (args.output / "closure.json").write_bytes(
            json.dumps(closure, indent=2, sort_keys=True).encode() + b"\n")
        for name, manifest in manifests.items():
            (args.output / f"{name}.manifest").write_text(
                manifest, encoding="utf-8", newline="\n")
        return 0
    except (OSError, TraceError) as error:
        parser.exit(2, f"runtime target trace rejected: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
