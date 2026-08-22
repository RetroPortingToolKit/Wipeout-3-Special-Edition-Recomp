#!/usr/bin/env python3
"""Build finite identity-bound CFI universes for clean static-AOT dispatch."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any


SCHEMA = "wo3se.indirect-cfi.v1"


class CfiError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def expand(items: list[dict[str, int]]) -> set[int]:
    return {pc for item in items for pc in range(item["start"], item["end_exclusive"], 4)}


def build(authority: dict[str, Any], shards: dict[str, Any], images: dict[str, bytes]) -> dict[str, Any]:
    if authority.get("schema") != "wo3se.executable-authorization.v1":
        raise CfiError("unsupported authorization schema")
    if shards.get("schema") != "wo3se.generated-game-shards.v1":
        raise CfiError("unsupported shard schema")
    reports = {row["name"]: row for row in authority["images"]
               if row["name"] in {"main-exe", "nfe-entry5-route-variant"}}
    compiled = {row["name"]: expand(row["ranges"]) for row in shards["images"]}
    if set(reports) != set(compiled) or set(images) != set(reports):
        raise CfiError("image sets do not exactly match")
    evidence = authority["evidence"]
    identities = {"main-exe": evidence["exe_text_sha256"],
                  "nfe-entry5-route-variant": evidence["nfe_route_variant_sha256"]}

    universes = []
    sites = []
    remaining = []
    for name in sorted(reports):
        report = reports[name]
        data = images[name]
        if len(data) != report["end_exclusive"] - report["base"] or \
                hashlib.sha256(data).hexdigest() != identities[name]:
            raise CfiError(f"{name} bytes do not match authenticated identity")
        function_entries = set(report["roots"])
        function_entries.update(edge["target"] for edge in report["direct_edges"]
                                if edge["kind"] in {"direct_call", "conditional_call"})
        function_entries &= compiled[name]
        continuations = {edge["target"] for edge in report["direct_edges"]
                         if edge["kind"] == "fallthrough"} & compiled[name]
        universes.extend((
            {"entries": sorted(function_entries), "id": f"{name}:function-entry",
             "identity_sha256": identities[name], "kind": "function-entry"},
            {"entries": sorted(continuations), "id": f"{name}:call-continuation",
             "identity_sha256": identities[name], "kind": "call-continuation"},
        ))
        authorized = expand(report["authorized_ranges"])
        for pc in sorted(authorized):
            word = struct.unpack_from("<I", data, pc - report["base"])[0]
            if word >> 26 == 0 and (word & 0x3F) == 8 and ((word >> 21) & 31) == 31:
                sites.append({"admit_source": True, "kind": "return",
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": pc,
                              "universe": f"{name}:call-continuation"})
        for site in report["unresolved_indirects"]:
            row = {"category": site["category"], "source_image": name,
                   "source_pc": site["pc"]}
            if site["category"] == "argument_callback_target" and function_entries:
                sites.append({"admit_source": True, "kind": "bounded-callback",
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": site["pc"],
                              "universe": f"{name}:function-entry"})
            else:
                remaining.append(row)
    sites.sort(key=lambda row: (row["source_image"], row["source_pc"], row["kind"]))
    result = {
        "schema": SCHEMA,
        "status": "partial-fail-closed",
        "authority_sha256": hashlib.sha256(canonical(authority)).hexdigest(),
        "shards_sha256": hashlib.sha256(canonical(shards)).hexdigest(),
        "policy": {"runtime_decode": False, "unknown_target_action": "reject",
                   "cross_image_callback_action": "require-trace-evidence",
                   "computed_target_action": "unresolved"},
        "universes": universes,
        "sites": sites,
        "remaining_unresolved": remaining,
    }
    return result


def image_arg(value: str) -> tuple[str, Path]:
    name, separator, path = value.partition("=")
    if not separator:
        raise argparse.ArgumentTypeError("image must be NAME=PATH")
    return name, Path(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authorization", type=Path, required=True)
    parser.add_argument("--shards", type=Path, required=True)
    parser.add_argument("--image", type=image_arg, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    pairs = args.image
    if len({name for name, _ in pairs}) != len(pairs):
        raise SystemExit("duplicate image name")
    result = build(json.loads(args.authorization.read_text()),
                   json.loads(args.shards.read_text()),
                   {name: path.read_bytes() for name, path in pairs})
    args.output.write_bytes(canonical(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
