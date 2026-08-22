#!/usr/bin/env python3
"""Cluster unknown authenticated payload identities without assigning roles."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from collections.abc import Callable
from pathlib import Path, PurePosixPath
from typing import Any

SCHEMA = "wo3se.content-payload-clusters.v1"
UNKNOWN = {"authenticated-structured-envelope", "unclassified-payload"}


class ClusterError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def magic_key(prefix_hex: str) -> str:
    try:
        prefix = bytes.fromhex(prefix_hex)
    except ValueError as error:
        raise ClusterError("payload prefix is not hexadecimal") from error
    if len(prefix) < 4:
        return f"short:{prefix.hex()}"
    head = prefix[:4]
    if all(0x20 <= byte <= 0x7E for byte in head):
        return f"ascii:{head.decode('ascii')}"
    return f"hex:{head.hex()}"


def grouped(identities: list[dict[str, Any]], key: Callable[[dict[str, Any]], Any]
            ) -> list[dict[str, Any]]:
    groups: dict[Any, list[dict[str, Any]]] = {}
    for row in identities:
        groups.setdefault(key(row), []).append(row)
    return [
        {"identity_count": len(rows), "instance_count": sum(row["occurrences"] for row in rows),
         "key": group_key, "unique_bytes": sum(row["bytes"] for row in rows)}
        for group_key, rows in sorted(groups.items(), key=lambda item: str(item[0]))
    ]


def build_clusters(audit: dict[str, Any], audit_digest: str) -> dict[str, Any]:
    if audit.get("schema") != "wo3se.content-static-coverage.v1":
        raise ClusterError("unsupported content audit schema")
    payloads = audit.get("media_payloads")
    if not isinstance(payloads, list):
        raise ClusterError("content audit lacks media payloads")
    selected = [row for row in payloads if row.get("classification") in UNKNOWN]
    counts = Counter(row.get("expanded_sha256") for row in selected)
    if None in counts or len(selected) != audit.get("summary", {}).get("unclassified_payloads"):
        raise ClusterError("unknown payload accounting disagrees with the content audit")
    representatives: dict[str, dict[str, Any]] = {}
    for row in selected:
        digest = row["expanded_sha256"]
        required = {"classification", "container", "expanded_bytes", "prefix_hex"}
        if not required.issubset(row) or not isinstance(row["prefix_hex"], str):
            raise ClusterError("unknown payload lacks clustering metadata")
        signature = (row["classification"], row["expanded_bytes"], row["prefix_hex"])
        if digest in representatives:
            previous = representatives[digest]
            previous_signature = (previous["classification"], previous["bytes"],
                                  previous["prefix_hex"])
            if signature != previous_signature:
                raise ClusterError("one identity has ambiguous clustering metadata")
            continue
        extension = PurePosixPath(row["container"]).suffix.upper() or "<none>"
        representatives[digest] = {
            "bytes": row["expanded_bytes"], "classification": row["classification"],
            "extension": extension, "magic": magic_key(row["prefix_hex"]),
            "prefix_hex": row["prefix_hex"],
        }
    identities = []
    for digest in sorted(representatives):
        row = representatives[digest]
        identities.append({"bytes": row["bytes"], "classification": row["classification"],
                           "expanded_sha256": digest, "extension": row["extension"],
                           "magic": row["magic"], "occurrences": counts[digest]})
    if len(identities) != audit["summary"]["unique_unknown_payload_identities"]:
        raise ClusterError("unique identity count disagrees with content audit")

    clusters = {
        "extension": grouped(identities, lambda row: row["extension"]),
        "frequency": grouped(identities, lambda row: row["occurrences"]),
        "magic": grouped(identities, lambda row: row["magic"]),
        "size": grouped(identities, lambda row: row["bytes"]),
        "structure": grouped(identities, lambda row: row["classification"]),
    }
    dominant = sorted(
        ({"family": "magic", **row} for row in clusters["magic"]),
        key=lambda row: (-row["instance_count"], -row["identity_count"], str(row["key"])),
    )[:16]
    return {
        "clusters": clusters,
        "content_audit_sha256": audit_digest,
        "coverage_gates": {
            "all_unknown_identities_clustered": True,
            "all_unknown_roles_resolved": False,
            "cluster_labels_are_role_authority": False,
            "unknown_executable_risk_eliminated": False,
        },
        "dominant_families": dominant,
        "identities": identities,
        "schema": SCHEMA,
        "status": "clustered-role-unknown",
        "summary": {"instance_count": len(selected), "unique_identity_count": len(identities)},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        raw = args.audit.read_bytes()
        audit = json.loads(raw)
        report = build_clusters(audit, sha256(raw))
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8", newline="\n")
        print(payload, end="")
        return 0
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ClusterError) as error:
        parser.exit(2, f"payload clustering rejected: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
