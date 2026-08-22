#!/usr/bin/env python3
"""Verify an identity-bound MIPS route slice without executing guest code."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


class RouteError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def number(value: int | str) -> int:
    return value if isinstance(value, int) else int(value, 0)


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def load_json(path: Path) -> tuple[dict, bytes]:
    raw = path.read_bytes()
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RouteError(f"invalid JSON: {path}") from error
    if not isinstance(value, dict):
        raise RouteError(f"JSON root is not an object: {path}")
    return value, raw


def word_at(image: bytes, base: int, pc: int) -> int:
    offset = pc - base
    if pc & 3 or offset < 0 or offset + 4 > len(image):
        raise RouteError(f"PC outside aligned image: 0x{pc:08X}")
    return struct.unpack_from("<I", image, offset)[0]


def direct_target(pc: int, word: int) -> int:
    return ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)


def branch_target(pc: int, word: int) -> int:
    displacement = word & 0xFFFF
    if displacement & 0x8000:
        displacement -= 0x10000
    return (pc + 4 + displacement * 4) & 0xFFFFFFFF


def build(image: bytes, spec: dict, frontier: dict, frontier_raw: bytes) -> dict:
    if spec.get("schema") != "clean-aot-shell-disc-route-spec-v1":
        raise RouteError("unsupported route specification schema")
    expected_image = spec.get("resident_image_sha256")
    actual_image = sha256(image)
    if actual_image != expected_image:
        raise RouteError("resident image identity mismatch")
    if frontier.get("schema") != "clean-aot-bios-shell-exact-frontiers-v1":
        raise RouteError("unsupported frontier receipt schema")
    expected_frontier = spec["frontier"]
    if frontier.get("last_frontier") != expected_frontier["number"]:
        raise RouteError("frontier number mismatch")
    if number(frontier.get("terminal_pc")) != number(expected_frontier["terminal_pc"]):
        raise RouteError("frontier terminal PC mismatch")
    if frontier.get("terminal_ram_epoch_sha256") != expected_frontier["ram_epoch_sha256"]:
        raise RouteError("frontier RAM epoch mismatch")

    base = number(spec["load_start"])
    calls = []
    for item in spec["direct_calls"]:
        pc, target = number(item["pc"]), number(item["target"])
        word = word_at(image, base, pc)
        if word >> 26 != 3 or direct_target(pc, word) != target:
            raise RouteError(f"direct-call assertion failed at 0x{pc:08X}")
        delay = word_at(image, base, pc + 4)
        calls.append({"pc": f"0x{pc:08X}", "target": f"0x{target:08X}",
                      "return_pc": f"0x{pc + 8:08X}",
                      "word_le": f"{word:08x}", "delay_word_le": f"{delay:08x}",
                      "role": item["role"]})

    branches = []
    for item in spec["branches"]:
        pc, target = number(item["pc"]), number(item["target"])
        word = word_at(image, base, pc)
        opcode = word >> 26
        if opcode not in {1, 4, 5, 6, 7} or branch_target(pc, word) != target:
            raise RouteError(f"branch assertion failed at 0x{pc:08X}")
        branches.append({"pc": f"0x{pc:08X}", "target": f"0x{target:08X}",
                         "word_le": f"{word:08x}", "prerequisite": item["prerequisite"]})

    returns = []
    for item in spec["finite_returns"]:
        pc, callee = number(item["pc"]), number(item["callee"])
        if word_at(image, base, pc) != 0x03E00008:
            raise RouteError(f"JR RA assertion failed at 0x{pc:08X}")
        targets = []
        for call_pc_value in item["call_sites"]:
            call_pc = number(call_pc_value)
            word = word_at(image, base, call_pc)
            if word >> 26 != 3 or direct_target(call_pc, word) != callee:
                raise RouteError(f"finite-return callsite failed at 0x{call_pc:08X}")
            targets.append(f"0x{call_pc + 8:08X}")
        returns.append({"pc": f"0x{pc:08X}", "callee": f"0x{callee:08X}",
                        "finite_targets": targets, "proof": "direct JAL return address set"})

    loops = []
    for item in spec["loops"]:
        start, end = number(item["start"]), number(item["end_exclusive"])
        if start >= end:
            raise RouteError("empty loop range")
        payload = image[start - base:end - base]
        if len(payload) != end - start:
            raise RouteError("loop range outside resident image")
        loops.append({"name": item["name"], "start": f"0x{start:08X}",
                      "end_exclusive": f"0x{end:08X}", "sha256": sha256(payload),
                      "bound": item["bound"], "exit_prerequisite": item["exit_prerequisite"]})

    return {
        "schema": "clean-aot-shell-disc-route-receipt-v1",
        "status": "bounded-prerequisites",
        "resident_image": {"load_start": f"0x{base:08X}",
                           "length": len(image), "sha256": actual_image},
        "frontier": {"number": frontier["last_frontier"],
                     "terminal_pc": frontier["terminal_pc"],
                     "ram_epoch_sha256": frontier["terminal_ram_epoch_sha256"],
                     "receipt_sha256": sha256(frontier_raw)},
        "shell_init_entry": spec["shell_init_entry"],
        "goal_call_site": spec["goal_call_site"],
        "direct_calls": calls,
        "branches": branches,
        "finite_returns": returns,
        "loops": loops,
        "ordered_top_level_calls": spec["ordered_top_level_calls"],
        "unresolved_prerequisites": spec["unresolved_prerequisites"],
        "policy": {"guest_execution": False, "runtime_modified": False,
                   "guessed_edges": False, "authorization_broadened": False},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--frontier", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        spec, _ = load_json(args.spec)
        frontier, frontier_raw = load_json(args.frontier)
        receipt = build(args.image.read_bytes(), spec, frontier, frontier_raw)
        args.output.write_bytes(canonical(receipt))
    except (OSError, KeyError, RouteError) as error:
        parser.exit(2, f"shell disc route rejected: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
