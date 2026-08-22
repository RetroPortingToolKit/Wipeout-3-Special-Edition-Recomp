#!/usr/bin/env python3
"""Audit authenticated unknown ranges without promoting legal-looking data to code."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any

try:
    from tools.analysis.authorize_executable_regions import decode, legal_r3000a_instruction
except ModuleNotFoundError:  # Direct script execution places this directory on sys.path.
    from authorize_executable_regions import decode, legal_r3000a_instruction


SCHEMA = "wo3se.unknown-code-range-audit.v1"


class AuditError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def build(authority: dict[str, Any], images: dict[str, bytes]) -> dict[str, Any]:
    if authority.get("schema") != "wo3se.executable-authorization.v1":
        raise AuditError("unsupported authorization schema")
    reports = {row["name"]: row for row in authority["images"]
               if row["name"] in {"main-exe", "nfe-entry5-route-variant"}}
    if set(reports) != set(images):
        raise AuditError("image set does not exactly match game authorization")
    identities = {
        "main-exe": authority["evidence"]["exe_text_sha256"],
        "nfe-entry5-route-variant": authority["evidence"]["nfe_route_variant_sha256"],
    }
    rows = []
    incoming = []
    for name in sorted(reports):
        report = reports[name]
        data = images[name]
        base = report["base"]
        if (len(data) != report["end_exclusive"] - base or
                hashlib.sha256(data).hexdigest() != identities[name]):
            raise AuditError(f"{name} bytes do not match authenticated identity")
        unknown = {
            pc for item in report["unknown_ranges"]
            for pc in range(item["start"], item["end_exclusive"], 4)
        }
        for item in report["unknown_ranges"]:
            start, end = item["start"], item["end_exclusive"]
            payload = data[start - base:end - base]
            legal = sum(
                legal_r3000a_instruction(struct.unpack_from("<I", payload, offset)[0])
                for offset in range(0, len(payload), 4)
            )
            rows.append({"end_exclusive": end, "identity_sha256": identities[name],
                         "image": name, "legal_word_count": legal,
                         "sha256": hashlib.sha256(payload).hexdigest(), "start": start,
                         "word_count": len(payload) // 4})
        authorized = {
            pc for item in report["authorized_ranges"]
            for pc in range(item["start"], item["end_exclusive"], 4)
        }
        cross_selected = {
            (edge["source"], edge["target"])
            for edge in report.get("cross_image_direct_edges", [])
        }
        for pc in sorted(authorized):
            raw = struct.unpack_from("<I", data, pc - base)[0]
            kind, successors, _, _ = decode(raw, pc)
            if kind not in {"direct_call", "direct_jump", "conditional"}:
                continue
            for target in successors:
                if target in unknown and (pc, target) not in cross_selected:
                    incoming.append({"image": name, "source_pc": pc, "target_pc": target})
    return {
        "schema": SCHEMA,
        "status": ("authenticated-direct-roots-found" if incoming
                   else "no-additional-authenticated-roots"),
        "authority_sha256": hashlib.sha256(canonical(authority)).hexdigest(),
        "policy": {"legal_words_create_roots": False,
                   "recovery_requires_authenticated_direct_source": True,
                   "unknown_pc_action": "reject"},
        "ranges": rows,
        "authenticated_incoming_direct_edges": incoming,
        "recoverable_instruction_count": 0,
    }


def image_arg(value: str) -> tuple[str, Path]:
    name, separator, path = value.partition("=")
    if not separator:
        raise argparse.ArgumentTypeError("image must be NAME=PATH")
    return name, Path(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authorization", type=Path, required=True)
    parser.add_argument("--image", type=image_arg, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pairs = args.image
    if len({name for name, _ in pairs}) != len(pairs):
        raise SystemExit("duplicate image name")
    result = build(json.loads(args.authorization.read_text()),
                   {name: path.read_bytes() for name, path in pairs})
    args.output.write_bytes(canonical(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
