#!/usr/bin/env python3
"""Deterministic batching primitives for exact authenticated shell frontiers."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


class BatchError(RuntimeError):
    pass


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def is_control_transfer(word: int) -> bool:
    opcode, function = word >> 26, word & 0x3f
    return opcode in {1, 2, 3, 4, 5, 6, 7} or (opcode == 0 and function in {8, 9})


def authenticated_straight_line_span(image: bytes, offset: int,
                                     max_instructions: int) -> int:
    """Return a bounded span that cannot cross a later control transfer.

    A control transfer at the frontier owns its delay slot and is emitted as an
    atomic two-instruction shard.  A control transfer encountered after one or
    more straight-line instructions starts the next shard.  Thus batching adds
    no target authorization and changes neither delay-slot nor CFI semantics.
    """
    if max_instructions < 1:
        raise BatchError("max_instructions must be positive")
    if offset < 0 or offset + 4 > len(image):
        raise BatchError("frontier lies outside authenticated image")
    available = (len(image) - offset) // 4
    count = min(max_instructions, available)
    for index in range(count):
        word_offset = offset + index * 4
        word = int.from_bytes(image[word_offset:word_offset + 4], "little")
        if not is_control_transfer(word):
            continue
        if index:
            return index * 4
        if available < 2:
            raise BatchError("control-transfer delay slot lies outside authenticated image")
        return 8
    return count * 4


def authenticated_mandatory_ranges(image: bytes, image_base: int, frontier: int,
                                   max_instructions: int) -> list[tuple[int, int]]:
    """Close only mandatory straight-line and unconditional-J execution.

    Conditional branches, JAL/JALR, JR, exceptions, and malformed/out-of-image
    targets terminate the closure after their atomic delay slot.  Consequently
    no untaken branch, dormant callee, or guessed return becomes executable.
    """
    if max_instructions < 1:
        raise BatchError("max_instructions must be positive")
    pc = frontier
    words = 0
    selected: set[int] = set()
    while words < max_instructions:
        offset = pc - image_base
        if offset < 0 or offset + 4 > len(image) or pc & 3:
            if not selected:
                raise BatchError("frontier lies outside authenticated image")
            break
        if pc in selected:
            break
        word = int.from_bytes(image[offset:offset + 4], "little")
        opcode, function = word >> 26, word & 0x3f
        control = is_control_transfer(word)
        selected.add(pc)
        words += 1
        if not control:
            pc += 4
            continue
        if offset + 8 > len(image):
            raise BatchError("control-transfer delay slot lies outside authenticated image")
        selected.add(pc + 4)
        words += 1
        # Plain J is the only control transfer whose next PC is unconditional
        # and independent of live register/state values.
        if opcode == 2 and words < max_instructions:
            pc = ((pc + 4) & 0xf0000000) | ((word & 0x03ffffff) << 2)
            continue
        break
    ordered = sorted(selected)
    ranges: list[tuple[int, int]] = []
    for address in ordered:
        if ranges and address == ranges[-1][1]:
            ranges[-1] = (ranges[-1][0], address + 4)
        else:
            ranges.append((address, address + 4))
    return ranges


def receipt_relink_projection(path: Path) -> dict[str, int | float]:
    """Project historical one-binding relinks after exact contiguous bundling."""
    document = json.loads(path.read_text(encoding="utf-8"))
    bindings = document.get("bindings", [])
    bundles = 0
    previous = None
    for item in bindings:
        key = (item.get("source_sha256"), item.get("load_start"))
        if previous is None or key[0] != previous[0] or int(key[1], 16) != previous[1]:
            bundles += 1
        previous = (key[0], int(item["load_end_exclusive"], 16))
    count = len(bindings)
    return {"binding_count": count, "bundle_count": bundles,
            "relink_reduction": count - bundles,
            "speedup": 0.0 if bundles == 0 else count / bundles}


def audit_exact_receipt(path: Path) -> dict[str, int | str]:
    """Verify that a live-frontier receipt preserves exact identity binding."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != "clean-aot-live-shell-frontiers-v1":
        raise BatchError("unsupported live-frontier receipt schema")
    bindings = document.get("bindings")
    if not isinstance(bindings, list) or not bindings:
        raise BatchError("receipt has no authenticated bindings")
    seen_indices: set[int] = set()
    seen_variants: set[str] = set()
    total_bytes = 0
    for binding in bindings:
        try:
            index = binding["index"]
            start = int(binding["load_start"], 16)
            end = int(binding["load_end_exclusive"], 16)
            source = binding["source_sha256"]
            variant = binding["variant_sha256"]
        except (KeyError, TypeError, ValueError) as exc:
            raise BatchError("malformed authenticated binding") from exc
        if not isinstance(index, int) or index in seen_indices:
            raise BatchError("duplicate or malformed binding index")
        code_ranges = binding.get("code_ranges")
        if isinstance(code_ranges, list) and len(code_ranges) > 1:
            contract = ",".join(value.removeprefix("0x").replace("-0x", "-")
                                for value in code_ranges)
            expected = sha(f"{source}:ranges:{contract}".encode("ascii"))
        else:
            expected = sha(f"{source}:{start:08x}:{end:08x}".encode("ascii"))
        if variant != expected:
            raise BatchError(f"variant identity mismatch at binding {index}")
        if end <= start or start & 3 or end & 3:
            raise BatchError(f"invalid instruction range at binding {index}")
        if variant in seen_variants:
            raise BatchError("duplicate variant identity")
        for field in ("source_sha256", "variant_sha256", "image_sha256", "manifest_sha256"):
            value = binding.get(field)
            if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
                raise BatchError(f"malformed {field} at binding {index}")
        seen_indices.add(index)
        seen_variants.add(variant)
        total_bytes += end - start
    return {"schema": document["schema"], "binding_count": len(bindings),
            "instruction_count": total_bytes // 4,
            "receipt_sha256": sha(path.read_bytes())}
