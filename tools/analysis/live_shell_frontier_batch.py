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
