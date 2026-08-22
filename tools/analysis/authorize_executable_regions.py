#!/usr/bin/env python3
"""Build a fail-closed executable authorization manifest from cross-evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from collections import deque
from pathlib import Path
from typing import Any

SCHEMA = "wo3se.executable-authorization.v1"


class AuthorizationError(RuntimeError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ranges(values: set[int], stride: int = 4) -> list[dict[str, int]]:
    if not values:
        return []
    ordered = sorted(values)
    result: list[dict[str, int]] = []
    start = previous = ordered[0]
    for value in ordered[1:]:
        if value != previous + stride:
            result.append({"start": start, "end_exclusive": previous + stride})
            start = value
        previous = value
    result.append({"start": start, "end_exclusive": previous + stride})
    return result


def complement(start: int, end: int, covered: set[int]) -> list[dict[str, int]]:
    return ranges(set(range(start, end, 4)) - covered)


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_bytes())
    if not isinstance(value, dict):
        raise AuthorizationError(f"{path} must contain an object")
    return value


def decode(word: int, pc: int) -> tuple[str, list[int], bool, int | None]:
    opcode = word >> 26
    if opcode == 0:
        function = word & 0x3F
        rs = (word >> 21) & 31
        if function == 8:
            return ("return" if rs == 31 else "indirect_jump", [], True, rs)
        if function == 9:
            return ("indirect_call", [pc + 8], True, rs)
        return ("linear", [pc + 4], False, None)
    if opcode == 2 or opcode == 3:
        target = ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
        successors = [target]
        if opcode == 3:
            successors.append(pc + 8)
        return ("direct_call" if opcode == 3 else "direct_jump", successors, True, None)
    if opcode in {1, 4, 5, 6, 7, 20, 21, 22, 23}:
        displacement = word & 0xFFFF
        if displacement & 0x8000:
            displacement -= 0x10000
        return ("conditional", [pc + 8, pc + 4 + displacement * 4], True, None)
    return ("linear", [pc + 4], False, None)


def resolve_indirect(image: bytes, base: int, pc: int, register: int) -> int | None:
    """Resolve only local, literal register constructions; reject all inference."""
    if register <= 0 or pc - 4 < base:
        return None

    def read(address: int) -> int:
        return struct.unpack_from("<I", image, address - base)[0]

    previous = read(pc - 4)
    opcode = previous >> 26
    rs = (previous >> 21) & 31
    rt = (previous >> 16) & 31
    immediate = previous & 0xFFFF
    if opcode == 9 and rt == register and rs == 0:
        return immediate | (0xFFFF0000 if immediate & 0x8000 else 0)
    if opcode == 15 and rt == register:
        return immediate << 16
    if opcode == 13 and rt == register and rs == register and pc - 8 >= base:
        earlier = read(pc - 8)
        if earlier >> 26 == 15 and ((earlier >> 16) & 31) == register:
            return ((earlier & 0xFFFF) << 16) | immediate
    return None


def analyze(
    image: bytes,
    base: int,
    roots: set[int],
    allowed: set[int],
    root_evidence: str,
) -> dict[str, Any]:
    end = base + len(image)
    authorized: set[int] = set()
    owners: dict[int, int] = {}
    edges: set[tuple[int, int, str]] = set()
    unresolved_indirect: set[tuple[int, int]] = set()
    indirect_targets: set[tuple[int, int]] = set()
    rejected_edges: set[tuple[int, int, str]] = set()
    rejected_roots: set[int] = set()
    queue = deque(sorted(roots))

    def word_at(pc: int) -> int:
        return struct.unpack_from("<I", image, pc - base)[0]

    while queue:
        pc = queue.popleft()
        if pc in authorized:
            continue
        if pc % 4 or not base <= pc < end or pc not in allowed:
            rejected_roots.add(pc)
            continue
        word = word_at(pc)
        kind, successors, delay, register = decode(word, pc)
        if pc in owners:
            rejected_roots.add(pc)
            continue
        if delay:
            slot = pc + 4
            if slot not in allowed or not base <= slot < end:
                rejected_edges.add((pc, slot, "missing_delay_slot_evidence"))
                continue
            slot_kind, _, _, _ = decode(word_at(slot), slot)
            if slot_kind != "linear":
                rejected_edges.add((pc, slot, "control_in_delay_slot"))
                continue
            prior = owners.get(slot)
            if prior is not None and prior != pc:
                rejected_edges.add((pc, slot, "ambiguous_delay_slot_owner"))
                continue
            owners[slot] = pc
            authorized.add(slot)
        authorized.add(pc)
        if kind.startswith("indirect"):
            source_register = register if register is not None else -1
            target = resolve_indirect(image, base, pc, source_register)
            if target is not None and target in allowed and base <= target < end:
                indirect_targets.add((pc, target))
                edges.add((pc, target, kind))
                queue.append(target)
            else:
                unresolved_indirect.add((pc, source_register))
        if kind == "return":
            continue
        for target in successors:
            edge_kind = kind if target != pc + 8 else "fallthrough"
            if target in allowed and base <= target < end and target not in owners:
                edges.add((pc, target, edge_kind))
                queue.append(target)
            else:
                rejected_edges.add((pc, target, "outside_cross_evidence"))

    return {
        "root_evidence": root_evidence,
        "roots": sorted(roots),
        "rejected_roots": sorted(rejected_roots),
        "authorized_instruction_count": len(authorized),
        "authorized_ranges": ranges(authorized),
        "direct_edges": [
            {"source": source, "target": target, "kind": kind}
            for source, target, kind in sorted(edges)
        ],
        "direct_closure_complete": not rejected_edges,
        "rejected_direct_edges": [
            {"source": source, "target": target, "reason": reason}
            for source, target, reason in sorted(rejected_edges)
        ],
        "indirect_targets": [
            {"source": source, "target": target, "proof": "local_literal_register_construction"}
            for source, target in sorted(indirect_targets)
        ],
        "unresolved_indirects": [
            {"pc": pc, "source_register": register}
            for pc, register in sorted(unresolved_indirect)
        ],
        "finite_indirect_closure": not unresolved_indirect,
        "delay_slot_owners": [
            {"delay_slot": slot, "owner": owner}
            for slot, owner in sorted(owners.items())
        ],
        "unknown_ranges": complement(base, end, authorized),
    }


def build(args: argparse.Namespace) -> dict[str, Any]:
    container = args.exe_container.read_bytes()
    exe = args.exe_text.read_bytes()
    nfe = bytearray(args.nfe_entry5.read_bytes())
    expansion = load_json(args.nfe_expansion_receipt)
    route = load_json(args.route_receipt)
    capture = load_json(args.capture)
    exe_hints = load_json(args.exe_hints)
    hints = load_json(args.nfe_hints)
    if sha256(container) != args.exe_container_sha256 or \
       sha256(exe) != args.exe_sha256 or sha256(nfe) != args.nfe_sha256:
        raise AuthorizationError("authenticated input SHA-256 mismatch")
    if len(container) < 2048 + len(exe) or container[2048 : 2048 + len(exe)] != exe:
        raise AuthorizationError("EXE text is not the authenticated container payload")
    if len(exe) % 4 or len(nfe) % 4:
        raise AuthorizationError("images must be word aligned")
    expansion_entries = expansion.get("entries", [])
    if len(expansion_entries) <= 5 or expansion_entries[5].get("expanded_sha256") != args.nfe_sha256:
        raise AuthorizationError("NFE expansion receipt does not authenticate entry 5")
    for hint, expected_base, expected_bytes in (
        (exe_hints, args.exe_base, len(exe)),
        (hints, args.nfe_base, len(nfe)),
    ):
        if hint.get("status") != "valid-untrusted-hints" or \
           hint.get("image_bytes") != expected_bytes or \
           hint.get("image_range") != {"start": expected_base, "end_exclusive": expected_base + expected_bytes}:
            raise AuthorizationError("hint receipt image identity/range mismatch")
    if capture.get("status") != "valid-untrusted-discovery":
        raise AuthorizationError("capture receipt is not normalized discovery evidence")
    if route.get("ok") is not True or route.get("completion") != "exact":
        raise AuthorizationError("deterministic route receipt is not exact and complete")

    nfe_end = args.nfe_base + len(nfe)
    observed: set[int] = set()
    captured_bytes = 0
    patched_words: set[int] = set()
    for region in capture.get("regions", []):
        start = region.get("load_addr")
        end = region.get("end_exclusive")
        pcs = region.get("executed_pcs")
        digest = region.get("bytes_sha256")
        if not isinstance(start, int) or not isinstance(end, int) or not isinstance(pcs, list):
            raise AuthorizationError("malformed normalized capture region")
        image_path = next(args.capture_images.glob(f"*-{start:08X}-{end:08X}-{digest}.bin"), None)
        if image_path is None:
            raise AuthorizationError(f"missing hash-named capture image for 0x{start:08X}")
        runtime = image_path.read_bytes()
        if sha256(runtime) != digest or len(runtime) != end - start:
            raise AuthorizationError("capture image identity mismatch")
        overlap_start = max(start, args.nfe_base)
        overlap_end = min(end, nfe_end)
        if overlap_start < overlap_end:
            source_offset = overlap_start - args.nfe_base
            runtime_offset = overlap_start - start
            for offset in range(0, overlap_end - overlap_start, 4):
                source_word = nfe[source_offset + offset : source_offset + offset + 4]
                runtime_word = runtime[runtime_offset + offset : runtime_offset + offset + 4]
                if source_word != runtime_word:
                    patched_words.add(overlap_start + offset)
                    nfe[source_offset + offset : source_offset + offset + 4] = runtime_word
            captured_bytes += overlap_end - overlap_start
            observed.update(pc for pc in pcs if overlap_start <= pc < overlap_end)

    hint_allowed: set[int] = set()
    for body in hints.get("candidate_body_union", []):
        start = max(args.nfe_base, body["start"])
        end = min(nfe_end, body["end_exclusive"])
        hint_allowed.update(range(start, end, 4))
    nfe_allowed = observed | hint_allowed

    exe_end = args.exe_base + len(exe)
    exe_allowed = {args.exe_entry}
    for body in exe_hints.get("candidate_body_union", []):
        start = max(args.exe_base, body["start"])
        end = min(exe_end, body["end_exclusive"])
        exe_allowed.update(range(start, end, 4))
    exe_report = analyze(
        exe, args.exe_base, {args.exe_entry}, exe_allowed,
        "authenticated PS-X EXE entry; closure constrained by untrusted Ghidra body bounds, which never create roots",
    )
    nfe_report = analyze(
        bytes(nfe), args.nfe_base, observed, nfe_allowed,
        "route-observed PCs whose runtime bytes were matched to authenticated NFE entry 5; Ghidra bodies constrain extension but never create roots",
    )
    return {
        "schema": SCHEMA,
        "status": "partial-fail-closed",
        "policy": {
            "bytes_are_authoritative": True,
            "ghidra_is_discovery_only": True,
            "capture_is_discovery_only": True,
            "unknown_pc_action": "reject",
            "unknown_executable_write_action": "reject",
        },
        "evidence": {
            "exe_text_sha256": sha256(exe),
            "exe_container_sha256": sha256(container),
            "exe_hint_receipt_sha256": sha256(args.exe_hints.read_bytes()),
            "nfe_entry5_source_sha256": args.nfe_sha256,
            "nfe_expansion_receipt_sha256": sha256(args.nfe_expansion_receipt.read_bytes()),
            "nfe_route_variant_sha256": sha256(nfe),
            "normalized_capture_sha256": sha256(args.capture.read_bytes()),
            "deterministic_route_receipt_sha256": sha256(args.route_receipt.read_bytes()),
            "nfe_hint_receipt_sha256": sha256(args.nfe_hints.read_bytes()),
            "nfe_captured_overlap_bytes": captured_bytes,
            "nfe_runtime_variant_word_addresses": sorted(patched_words),
        },
        "images": [
            {"name": "main-exe", "base": args.exe_base,
             "end_exclusive": args.exe_base + len(exe), **exe_report},
            {"name": "nfe-entry5-route-variant", "base": args.nfe_base,
             "end_exclusive": nfe_end, **nfe_report},
        ],
    }


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--exe-text", type=Path, required=True)
    result.add_argument("--exe-container", type=Path, required=True)
    result.add_argument("--exe-container-sha256", required=True)
    result.add_argument("--exe-sha256", required=True)
    result.add_argument("--exe-base", type=lambda value: int(value, 0), default=0x80010000)
    result.add_argument("--exe-entry", type=lambda value: int(value, 0), default=0x80162474)
    result.add_argument("--exe-hints", type=Path, required=True)
    result.add_argument("--nfe-entry5", type=Path, required=True)
    result.add_argument("--nfe-expansion-receipt", type=Path, required=True)
    result.add_argument("--nfe-sha256", required=True)
    result.add_argument("--nfe-base", type=lambda value: int(value, 0), default=0x800BF800)
    result.add_argument("--capture", type=Path, required=True)
    result.add_argument("--route-receipt", type=Path, required=True)
    result.add_argument("--capture-images", type=Path, required=True)
    result.add_argument("--nfe-hints", type=Path, required=True)
    result.add_argument("--output", type=Path, required=True)
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        report = build(args)
        payload = json.dumps(report, indent=2, sort_keys=True) + "\n"
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload, encoding="utf-8", newline="\n")
        print(payload, end="")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError, AuthorizationError) as error:
        print(f"authorization failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
