#!/usr/bin/env python3
"""Build deterministic, fail-closed compiler shards from executable authority."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


SCHEMA = "wo3se.generated-game-shards.v1"


class ShardError(ValueError):
    pass


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def ranges(pcs: set[int]) -> list[dict[str, int]]:
    result: list[dict[str, int]] = []
    for pc in sorted(pcs):
        if result and result[-1]["end_exclusive"] == pc:
            result[-1]["end_exclusive"] += 4
        else:
            result.append({"start": pc, "end_exclusive": pc + 4})
    return result


def expand(items: list[dict[str, int]]) -> set[int]:
    result: set[int] = set()
    for item in items:
        start, end = item["start"], item["end_exclusive"]
        if start % 4 or end <= start or end % 4:
            raise ShardError("authorized range is not aligned and non-empty")
        result.update(range(start, end, 4))
    return result


def build(authority: dict[str, Any], finite: dict[str, Any]) -> dict[str, Any]:
    if authority.get("schema") != "wo3se.executable-authorization.v1":
        raise ShardError("unsupported executable authorization schema")
    if finite.get("schema") != "wo3se.finite-indirect-targets.v1":
        raise ShardError("unsupported finite-target schema")

    selected = {item["name"]: item for item in authority["images"]
                if item["name"] in {"main-exe", "nfe-entry5-route-variant"}}
    if set(selected) != {"main-exe", "nfe-entry5-route-variant"}:
        raise ShardError("authorization lacks the two game images")
    evidence = authority["evidence"]
    identities = {
        "main-exe": evidence["exe_text_sha256"],
        "nfe-entry5-route-variant": evidence["nfe_route_variant_sha256"],
    }
    if (finite.get("image", {}).get("name") != "main-exe" or
            finite["image"].get("sha256") != identities["main-exe"]):
        raise ShardError("finite-target evidence is not bound to the main image")

    nodes = {name: expand(image["authorized_ranges"]) for name, image in selected.items()}
    safe = {name: set(values) for name, values in nodes.items()}
    finite_sites = {item["source"]: set(item["targets"]) for item in finite["sites"]}
    dependencies: dict[tuple[str, int], set[tuple[str, int]]] = {}
    rejected: dict[str, set[int]] = {}

    for name, image in selected.items():
        rejected[name] = {edge["source"] for edge in image["rejected_direct_edges"]}
        unsafe_indirect = {item["pc"] for item in image["unresolved_indirects"]}
        if name == "main-exe":
            unsafe_indirect -= finite_sites.keys()
        safe[name] -= rejected[name] | unsafe_indirect
        for edge in image["direct_edges"]:
            dependencies.setdefault((name, edge["source"]), set()).add((name, edge["target"]))
        for edge in image.get("cross_image_direct_edges", []):
            dependencies.setdefault((name, edge["source"]), set()).add(
                (edge["target_image"], edge["target"]))
        if name == "main-exe":
            for source, targets in finite_sites.items():
                dependencies.setdefault((name, source), set()).update(
                    (name, target) for target in targets)

    # A control transfer and its architectural delay slot are indivisible.
    delay_pairs: list[tuple[tuple[str, int], tuple[str, int]]] = []
    for name, image in selected.items():
        for pair in image["delay_slot_owners"]:
            delay_pairs.append(((name, pair["owner"]), (name, pair["delay_slot"])))

    removed_for_closure = {name: set(nodes[name] - safe[name]) for name in selected}
    changed = True
    while changed:
        changed = False
        for (source_image, source), targets in dependencies.items():
            if source in safe[source_image] and any(
                    target not in safe[target_image] for target_image, target in targets):
                safe[source_image].remove(source)
                removed_for_closure[source_image].add(source)
                changed = True
        for owner, slot in delay_pairs:
            owner_safe = owner[1] in safe[owner[0]]
            slot_safe = slot[1] in safe[slot[0]]
            if owner_safe != slot_safe:
                victim = owner if owner_safe else slot
                safe[victim[0]].remove(victim[1])
                removed_for_closure[victim[0]].add(victim[1])
                changed = True

    images: list[dict[str, Any]] = []
    for name in sorted(selected):
        image = selected[name]
        closed_ranges = ranges(safe[name])
        # Every instruction is an explicit generated entry. This prevents the
        # compiler from discovering authority beyond the exact closed ranges.
        images.append({
            "name": name,
            "identity_sha256": identities[name],
            "load_start": image["base"],
            "load_end_exclusive": image["end_exclusive"],
            "authorized_instruction_count": len(nodes[name]),
            "compiled_instruction_count": len(safe[name]),
            "compiled_region_count": len(closed_ranges),
            "excluded_instruction_count": len(nodes[name] - safe[name]),
            "ranges": closed_ranges,
            "entries": sorted(safe[name]),
            "unknown_pc_action": "reject",
        })
    result = {
        "schema": SCHEMA,
        "status": "partial-fail-closed",
        "authority_sha256": hashlib.sha256(canonical(authority)).hexdigest(),
        "finite_targets_sha256": hashlib.sha256(canonical(finite)).hexdigest(),
        "policy": {
            "unknown_pc_action": "reject",
            "unknown_identity_action": "reject",
            "unresolved_edge_action": "exclude-source-and-reverse-closure",
            "delay_slots_are_atomic": True,
            "full_game_closure_claimed": False,
        },
        "images": images,
    }
    result["compiled_instruction_count"] = sum(x["compiled_instruction_count"] for x in images)
    result["compiled_region_count"] = sum(x["compiled_region_count"] for x in images)
    if result["compiled_instruction_count"] == 0:
        raise ShardError("closed-set construction produced no compiled game code")
    return result


def manifest(image: dict[str, Any]) -> str:
    lines = [
        "schema=clean-aot-compiler-manifest-v1",
        "image_kind=raw-payload",
        f"variant_sha256={image['identity_sha256']}",
        f"image_sha256={image['identity_sha256']}",
        f"load_start=0x{image['load_start']:08X}",
        f"load_end_exclusive=0x{image['load_end_exclusive']:08X}",
    ]
    lines.extend(f"entry=0x{pc:08X}" for pc in image["entries"])
    lines.extend(f"code=0x{item['start']:08X}-0x{item['end_exclusive']:08X}"
                 for item in image["ranges"])
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--authorization", type=Path, required=True)
    parser.add_argument("--finite-targets", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument("--manifest-dir", type=Path, required=True)
    args = parser.parse_args()
    authority = json.loads(args.authorization.read_text())
    finite = json.loads(args.finite_targets.read_text())
    result = build(authority, finite)
    args.manifest_dir.mkdir(parents=True, exist_ok=True)
    for image in result["images"]:
        path = args.manifest_dir / f"{image['name']}-closed-shards.manifest"
        path.write_text(manifest(image), newline="\n")
        image["manifest"] = path.name
        image["manifest_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        del image["entries"]
    args.receipt.write_bytes(canonical(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
