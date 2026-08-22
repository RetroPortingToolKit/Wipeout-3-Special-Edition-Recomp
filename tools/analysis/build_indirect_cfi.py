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


def word(data: bytes, base: int, pc: int) -> int:
    if pc < base or pc + 4 > base + len(data):
        raise CfiError("instruction read is outside image")
    return struct.unpack_from("<I", data, pc - base)[0]


def writes_register(raw: int, register: int) -> bool:
    opcode = raw >> 26
    if opcode == 0:
        return ((raw >> 11) & 31) == register and (raw & 0x3F) not in {8, 12, 13}
    return opcode not in {2, 4, 5, 6, 7, 40, 41, 42, 43, 46, 50, 58} and \
        ((raw >> 16) & 31) == register


def memory_loaded_callback(data: bytes, base: int, pc: int, register: int) -> bool:
    """Require the nearest reaching local definition to be an exact LW."""
    for address in range(pc - 4, max(base - 1, pc - 68), -4):
        raw = word(data, base, address)
        if writes_register(raw, register):
            return raw >> 26 == 35 and ((raw >> 16) & 31) == register
        kind = raw >> 26
        if kind in {2, 3} or (kind == 0 and (raw & 0x3F) in {8, 9}):
            break
    return False


def bounded_table_targets(data: bytes, base: int, pc: int, register: int) -> list[int] | None:
    """Recognize compiler switch tables guarded by SLTIU and indexed by SLL 2."""
    if pc - 28 < base:
        return None
    load, add, lui, shift = (
        word(data, base, pc + offset) for offset in (-8, -12, -16, -20)
    )
    table_reg = (load >> 21) & 31
    index_reg = (shift >> 11) & 31
    selector = (shift >> 16) & 31
    if (load >> 26 != 35 or ((load >> 16) & 31) != register or
            add >> 26 != 0 or (add & 0x3F) not in {32, 33} or
            ((add >> 11) & 31) != table_reg or
            {((add >> 21) & 31), ((add >> 16) & 31)} != {table_reg, index_reg} or
            lui >> 26 != 15 or ((lui >> 16) & 31) != table_reg or
            shift >> 26 != 0 or (shift & 0x3F) != 0 or
            ((shift >> 6) & 31) != 2):
        return None
    bound = None
    for branch_pc in range(pc - 24, max(base - 1, pc - 48), -4):
        branch = word(data, base, branch_pc)
        if branch >> 26 not in {4, 5} or ((branch >> 16) & 31) != 0:
            continue
        bound_reg = (branch >> 21) & 31
        for bound_pc in range(branch_pc - 4, max(base - 1, branch_pc - 20), -4):
            candidate = word(data, base, bound_pc)
            if writes_register(candidate, bound_reg):
                if candidate >> 26 == 11 and ((candidate >> 21) & 31) == selector:
                    bound = candidate
                break
        break
    if bound is None:
        return None
    count = bound & 0xFFFF
    if not 1 <= count <= 256:
        return None
    immediate = load & 0xFFFF
    if immediate & 0x8000:
        immediate -= 0x10000
    table = ((lui & 0xFFFF) << 16) + immediate
    if table % 4 or not base <= table or table + count * 4 > base + len(data):
        return None
    targets = [word(data, base, table + index * 4) for index in range(count)]
    if any(target % 4 for target in targets):
        return None
    return sorted(set(targets))


def fixed_dispatch_targets(data: bytes, base: int, pc: int, register: int) -> list[int] | None:
    """Recognize the authenticated renderer's finite t4 continuation merge."""
    if register != 12 or pc != 0x8016FD2C or pc - 0x104 < base:
        return None
    expected = {
        0x8016FC48: 0x3C0C8016, 0x8016FC4C: 0x358CF94C,
        0x8016FC50: 0x3C0D8016, 0x8016FC54: 0x35ADFA70,
        0x8016FC58: 0x3C0E8017, 0x8016FC5C: 0x35CE0774,
        0x8016FCA0: 0x01A06021, 0x8016FCB0: 0x01C06021,
        0x8016FCC4: 0x01A06021, 0x8016FCD4: 0x01A06021,
    }
    if any(word(data, base, address) != raw for address, raw in expected.items()):
        return None
    return [0x8016F94C, 0x8016FA70, 0x80170774]


def renderer_callback_contract(
    data: bytes, base: int, pc: int, register: int
) -> tuple[str, list[int] | None] | None:
    """Bind callbacks exposed only by the authenticated renderer corridors."""
    early_t5 = {0x8016FA04, 0x8016FB6C, 0x8016FB84}
    inherited_k1 = {0x8016FB9C, 0x8016FBB4}
    table_sites = {
        0x801708B8: 13, 0x801708D0: 13, 0x801708E8: 27, 0x80170900: 27,
        0x80170958: 13, 0x80170970: 13, 0x80170988: 27, 0x801709A0: 27,
        0x80170A04: 13, 0x80170A1C: 13, 0x80170A34: 27, 0x80170A4C: 27,
        0x80170A9C: 13, 0x80170AB4: 13, 0x80170ACC: 27, 0x80170AE4: 27,
    }
    if pc in early_t5 and register == 13:
        expected = {0x8016FC50: 0x3C0D8016, 0x8016FC54: 0x35ADFA70}
        if all(word(data, base, address) == raw for address, raw in expected.items()):
            return "finite-constant-set", [0x8016FA70]
    if pc in inherited_k1 and register == 27:
        if (word(data, base, 0x8017025C) == 0x03600013 and
                word(data, base, 0x801704EC) == 0x0000D812):
            return "bounded-inherited-callback", None
    if table_sites.get(pc) == register:
        expected_loads = {
            0x80170868: 0x8C8D0000, 0x8017086C: 0x8C9B0004,
            0x801709B0: 0x8C8D0008, 0x801709B4: 0x8C9B000C,
        }
        if all(word(data, base, address) == raw for address, raw in expected_loads.items()):
            return "bounded-memory-callback", None
    return None


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
            instruction = struct.unpack_from("<I", data, pc - report["base"])[0]
            if (instruction >> 26 == 0 and (instruction & 0x3F) == 8 and
                    ((instruction >> 21) & 31) == 31):
                sites.append({"admit_source": True, "kind": "return",
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": pc,
                              "universe": f"{name}:call-continuation"})
        for site in report["unresolved_indirects"]:
            row = {"category": site["category"], "source_image": name,
                   "source_pc": site["pc"]}
            raw = word(data, report["base"], site["pc"])
            register = (raw >> 21) & 31
            table_targets = bounded_table_targets(
                data, report["base"], site["pc"], register)
            fixed_targets = fixed_dispatch_targets(
                data, report["base"], site["pc"], register)
            renderer_contract = renderer_callback_contract(
                data, report["base"], site["pc"], register)
            renderer_targets = (renderer_contract[1] if renderer_contract is not None and
                                renderer_contract[0] == "finite-constant-set" else None)
            exact_targets = (table_targets if table_targets is not None else
                             fixed_targets if fixed_targets is not None else renderer_targets)
            if exact_targets is not None:
                admitted = sorted(set(exact_targets) & authorized)
                if admitted != exact_targets:
                    remaining.append({**row, "reason": "finite-target-not-authorized"})
                    continue
                universe_id = f"{name}:site-{site['pc']:08x}"
                universes.append({"entries": admitted, "id": universe_id,
                                  "identity_sha256": identities[name],
                                  "kind": ("finite-table" if table_targets is not None
                                           else "finite-constant-set")})
                sites.append({"admit_source": True,
                              "kind": ("finite-table" if table_targets is not None
                                       else "finite-constant-set"),
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": site["pc"],
                              "universe": universe_id})
            elif site["category"] == "argument_callback_target" and function_entries:
                sites.append({"admit_source": True, "kind": "bounded-callback",
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": site["pc"],
                              "universe": f"{name}:function-entry"})
            elif (renderer_contract is not None and function_entries and
                  renderer_contract[0] in {"bounded-memory-callback",
                                           "bounded-inherited-callback"}):
                sites.append({"admit_source": True, "kind": renderer_contract[0],
                              "source_identity_sha256": identities[name],
                              "source_image": name, "source_pc": site["pc"],
                              "universe": f"{name}:function-entry"})
            elif ((raw & 0x3F) in {8, 9} and raw >> 26 == 0 and function_entries and
                  memory_loaded_callback(data, report["base"], site["pc"], register)):
                sites.append({"admit_source": True, "kind": "bounded-memory-callback",
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
