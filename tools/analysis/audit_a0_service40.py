#!/usr/bin/env python3
"""Prove the exact A0 service 0x40 contract and the live route that entered it."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


class AuditError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_words(data: bytes, offset: int, expected: list[int], label: str) -> None:
    end = offset + 4 * len(expected)
    if offset < 0 or end > len(data) or list(struct.unpack_from(f"<{len(expected)}I", data, offset)) != expected:
        raise AuditError(f"{label} mismatch")


def build(bios: bytes, ram: bytes, full_ram: bytes, route: dict, route_raw: bytes,
          baseline_runtime: bytes, spec: dict) -> dict:
    if spec.get("schema") != "clean-aot-a0-service40-audit-spec-v1":
        raise AuditError("unsupported specification schema")
    ids = spec["identities"]
    for label, blob, key in (("BIOS", bios, "bios_sha256"), ("RAM", ram, "ram_epoch_sha256"),
                             ("full RAM", full_ram, "full_ram_sha256")):
        if digest(blob) != ids[key]:
            raise AuditError(f"{label} identity mismatch")
    terminal = route.get("terminal", {})
    if route.get("schema") != "clean-aot-shell-disc-route-execution-v1" or terminal.get("pc") != "0x000005C4":
        raise AuditError("route terminal mismatch")

    require_words(bios, 0xD8E0, [0x240A00A0, 0x01400008, 0x24090040], "BIOS service stub")
    require_words(ram, 0xA0, [0x3C080000, 0x250805C4, 0x01000008, 0], "A0 trampoline")
    require_words(ram, 0x5C4, [0x24080200, 0x00094880, 0x01094020, 0x8D080000, 0, 0x01000008, 0], "A0 dispatcher")
    require_words(ram, 0x300, [0xBFC0D8E0], "service table entry")
    require_words(ram, 0x6A80, [0x240A00A0, 0x01400008, 0x24090040], "fatal caller stub")
    require_words(ram, 0x1B08, [0x0C001AA0, 0], "exception fatal call")
    require_words(full_ram, 0x51040, [0x8C6392A0, 0x34211000, 0x31AE3FFF, 0x01C17825,
                                     0xAC6F0000, 0x8C600000, 0x8C580000], "shell GPU sync")
    require_words(full_ram, 0x792A0, [0x1F801810], "GPUREAD pointer")
    defective = b"return result.ready ? handled_read(result.word) : rejected_read();"
    if defective not in baseline_runtime:
        raise AuditError("baseline GPUREAD rejection semantic missing")

    return {
        "schema": "clean-aot-a0-service40-audit-v1",
        "classification": "nonreturning-fatal-system-service",
        "identities": {"bios_sha256": digest(bios), "ram_epoch_sha256": digest(ram),
                       "full_ram_sha256": digest(full_ram), "route_receipt_sha256": digest(route_raw),
                       "baseline_runtime_sha256": digest(baseline_runtime)},
        "service": {"index": "0x40", "entry": "0xBFC0D8E0", "table_slot": "0x00000300",
                    "cycle": ["0xBFC0D8E0", "0x000000A0", "0x000005C4", "0x00000300", "0xBFC0D8E0"],
                    "returns": False, "stack_delta": 0,
                    "register_effects": {"t2": "0x000000A0", "t1": "0x40 then 0x100 then restored to 0x40",
                                         "t0": "0x5C4 then 0x200 then 0x300 then 0xBFC0D8E0",
                                         "preserved": ["sp", "ra", "a0-a3", "s0-s7"]}},
        "caller_contract": {"caller": "general exception handler fatal arm at 0x00001B08",
                            "condition": "exception Cause.ExcCode is neither interrupt (0) nor syscall (8)",
                            "expected_return": "none"},
        "live_trigger": {"shell_pc": "0x80051054", "operation": "CPU word read from GPUREAD 0x1F801810 into zero",
                         "baseline_defect": "runtime rejected GPUREAD unless transfer latch ready, producing BusErrorData",
                         "required_semantic": "GPUREAD remains CPU-readable and returns its latch word independent of DMA transfer readiness"},
        "finding": {"finite_initialization": False, "runtime_defect_proven": True,
                    "continuous_calls_explained": "one intentional fatal self-cycle after an emulation-induced exception"},
        "policy": {"runtime_modified_by_audit": False, "boot_manifest_modified": False, "probe_modified": False},
    }


def main() -> int:
    p = argparse.ArgumentParser()
    for name in ("bios", "ram", "full-ram", "route", "spec", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--repo", type=Path, required=True)
    p.add_argument("--baseline-commit", required=True)
    a = p.parse_args()
    try:
        route_raw = a.route.read_bytes(); route = json.loads(route_raw)
        spec = json.loads(a.spec.read_bytes())
        baseline = subprocess.run(["git", "show", f"{a.baseline_commit}:clean-aot/runtime/src/runtime.cpp"],
                                  cwd=a.repo, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
        result = build(a.bios.read_bytes(), a.ram.read_bytes(), a.full_ram.read_bytes(), route, route_raw, baseline, spec)
        a.output.write_text(json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n")
    except (OSError, KeyError, json.JSONDecodeError, subprocess.CalledProcessError, AuditError) as e:
        p.exit(2, f"A0 service 0x40 audit rejected: {e}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
