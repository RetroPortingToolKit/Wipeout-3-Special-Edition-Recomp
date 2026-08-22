#!/usr/bin/env python3
"""Bind the continuous 0x5C4 path to exact BIOS/RAM and IRQ semantics."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


class AuditError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def words(data: bytes, offset: int, count: int) -> list[int]:
    if offset < 0 or offset + count * 4 > len(data):
        raise AuditError("word range outside image")
    return list(struct.unpack_from(f"<{count}I", data, offset))


def require_words(data: bytes, offset: int, expected: list[int], label: str) -> None:
    if words(data, offset, len(expected)) != expected:
        raise AuditError(f"{label} instruction mismatch")


def build(bios: bytes, ram: bytes, route: dict, route_raw: bytes,
          runtime_source: bytes, soc_source: bytes, generated_source: bytes,
          spec: dict) -> dict:
    if spec.get("schema") != "clean-aot-shell-interrupt-audit-spec-v1":
        raise AuditError("unsupported specification schema")
    identities = spec["identities"]
    if digest(bios) != identities["bios_sha256"]:
        raise AuditError("BIOS identity mismatch")
    if digest(ram) != identities["ram_epoch_sha256"]:
        raise AuditError("RAM epoch mismatch")
    if route.get("schema") != "clean-aot-shell-disc-route-execution-v1" or \
       route.get("terminal", {}).get("pc") != "0x000005C4":
        raise AuditError("route terminal mismatch")

    # Exact retail BIOS A0/40 stub: t2=A0, JR t2, delay t1=0x40.
    require_words(bios, 0xD8E0, [0x240A00A0, 0x01400008, 0x24090040],
                  "BIOS A0/40 stub")
    # Installed low-RAM exception vector and A0 trampoline/dispatcher.
    require_words(ram, 0x80, [0x3C1A0000, 0x275A0C80, 0x03400008, 0],
                  "general exception vector")
    require_words(ram, 0xA0, [0x3C080000, 0x250805C4, 0x01000008, 0],
                  "A0 trampoline")
    require_words(ram, 0x5C4,
                  [0x24080200, 0x00094880, 0x01094020, 0x8D080000,
                   0, 0x01000008, 0], "A0 dispatcher")
    if words(ram, 0x200 + 0x40 * 4, 1)[0] != 0xBFC0D8E0:
        raise AuditError("A0 service table entry 0x40 mismatch")

    runtime_text = runtime_source.decode("utf-8")
    soc_text = soc_source.decode("utf-8")
    generated_text = generated_source.decode("utf-8")
    runtime_markers = [
        "soc.interrupts().raise(soc::InterruptSource::vblank)",
        "soc.interrupts().write_status(written)",
        "sample_device_irqs();",
        "interrupt_lines = soc.interrupts().cpu_line() ? 1U : 0U",
        "cop0_cause=(impl_->bios_cpu.cop0_cause&~0x0000FC00U)",
    ]
    soc_markers = [
        "status_ &= value & kInterruptAllMask",
        "return (status_ & mask_) != 0U",
    ]
    if any(marker not in runtime_text for marker in runtime_markers):
        raise AuditError("runtime interrupt semantic marker missing")
    if any(marker not in soc_text for marker in soc_markers):
        raise AuditError("SoC interrupt semantic marker missing")
    generated_markers = [
        "sample_interrupt(GeneratedCpuState&c)",
        "const auto mode=effective_status(c)&0x3FU",
        "((mode<<2U)&0x3CU)",
        "rfe(GeneratedCpuState&c)",
        "((mode>>2U)&0xFU)",
    ]
    if any(marker not in generated_text for marker in generated_markers):
        raise AuditError("generated COP0 semantic marker missing")

    terminal = route["terminal"]
    return {
        "schema": "clean-aot-shell-interrupt-audit-v1",
        "classification": "a0-service-dispatch-not-interrupt-handler",
        "identities": {
            "bios_sha256": digest(bios), "ram_epoch_sha256": digest(ram),
            "route_receipt_sha256": digest(route_raw),
            "runtime_interrupt_semantics_sha256":
                digest("\n".join(runtime_markers).encode()),
            "soc_interrupt_semantics_sha256":
                digest("\n".join(soc_markers).encode()),
            "generated_cop0_semantics_sha256":
                digest("\n".join(generated_markers).encode()),
        },
        "route_terminal": {
            "pc": terminal["pc"], "dispatch_status": terminal["dispatch_status"],
            "instructions": terminal["instructions"], "dispatches": terminal["dispatches"],
        },
        "exact_path": [
            "0xBFC0D8E0: t2=0xA0",
            "0xBFC0D8E4: JR t2; delay t1=0x40",
            "0x000000A0: trampoline to 0x000005C4",
            "0x000005C4: table base 0x200 plus t1*4",
            "0x00000300: service[0x40]=0xBFC0D8E0",
        ],
        "exception_path": ["0x00000080", "0x00000C80"],
        "interrupt_semantics": {
            "source": "CRTC VBlank raises I_STAT bit 0 once at the exact vertical-end edge",
            "acknowledge": "I_STAT is zero-to-clear; the write is followed by device-level resampling",
            "reassertion": "DMA/CD/GPU/SIO/SPU reassert only while their underlying level remains active; VBlank is not resampled",
            "delivery": "I_STAT & I_MASK drives COP0 hardware IP2; generated instructions sample Status.IE and Status.IM",
            "nesting": "exception entry pushes the six low Status mode bits, disabling current IE; RFE restores them",
        },
        "finding": {
            "interrupt_storm_proven": False,
            "missing_interrupt_return_proven": False,
            "runtime_or_soc_defect_proven": False,
            "reason": "the terminal transition is an A0/40 service dispatch and never traverses the exception vector",
        },
        "limits": [
            "the committed route receipt does not include I_STAT, I_MASK, COP0 Status/Cause/EPC, or stack samples",
            "therefore it cannot attribute any pending interrupt source or claim handler nesting",
        ],
        "policy": {"runtime_modified": False, "boot_control_modified": False,
                   "guest_execution_inferred": False},
    }


def load(path: Path) -> tuple[dict, bytes]:
    raw = path.read_bytes()
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise AuditError("JSON root is not an object")
    return value, raw


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bios", type=Path, required=True)
    parser.add_argument("--ram", type=Path, required=True)
    parser.add_argument("--route", type=Path, required=True)
    parser.add_argument("--runtime-source", type=Path, required=True)
    parser.add_argument("--soc-source", type=Path, required=True)
    parser.add_argument("--generated-source", type=Path, required=True)
    parser.add_argument("--spec", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        route, route_raw = load(args.route)
        spec, _ = load(args.spec)
        receipt = build(args.bios.read_bytes(), args.ram.read_bytes(), route,
                        route_raw, args.runtime_source.read_bytes(),
                        args.soc_source.read_bytes(),
                        args.generated_source.read_bytes(), spec)
        args.output.write_text(json.dumps(receipt, sort_keys=True,
                                          separators=(",", ":")) + "\n")
    except (OSError, KeyError, UnicodeError, json.JSONDecodeError, AuditError) as error:
        parser.exit(2, f"shell interrupt audit rejected: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
