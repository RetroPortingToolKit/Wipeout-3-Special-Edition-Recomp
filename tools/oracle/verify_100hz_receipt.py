#!/usr/bin/env python3
"""Verify a clean-room 100 Hz simulation receipt.

This is an acceptance oracle, not an emulator or a renderer.  The input is a
JSON evidence document emitted by a future native clean-AOT race harness.  It
requires one distinct simulation-state identity per 100 Hz update and checks
the exact rational CPU-retirement schedule while keeping SPU and CDDA on the
unscaled native timeline.  Presentation/image hashes are deliberately not
used as the uniqueness proof.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
from typing import Any

FORMAT = "wipeout3se-100hz-evidence-v1"
RECEIPT_FORMAT = "wipeout3se-100hz-receipt-v1"
VBLANK_HZ = 100
TIMEBASE_TICKS_PER_FRAME = 1
CPU_CYCLES_PER_NATIVE_TICK = 338_688
SPU_SAMPLES_PER_NATIVE_TICK = 441
CDDA_SECTORS_PER_SECOND = 75
NATIVE_TIMEBASE_HZ = 100
MAX_FRAMES = 1_000_000


class EvidenceError(ValueError):
    """The evidence document is malformed or fails a deterministic gate."""


def _uint(value: Any, label: str, maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise EvidenceError(f"{label} must be a non-negative integer")
    if maximum is not None and value > maximum:
        raise EvidenceError(f"{label} exceeds {maximum}")
    return value


def _nonempty_text(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value or len(value) > 256:
        raise EvidenceError(f"{label} must be a non-empty string of at most 256 characters")
    return value


def _ratio(value: Any, label: str) -> tuple[int, int]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be an object with numerator and denominator")
    numerator = _uint(value.get("numerator"), f"{label}.numerator")
    denominator = _uint(value.get("denominator"), f"{label}.denominator")
    if numerator == 0 or denominator == 0:
        raise EvidenceError(f"{label} must be positive")
    if math.gcd(numerator, denominator) != 1:
        raise EvidenceError(f"{label} must be reduced")
    return numerator, denominator


def parse_multiplier(text: str) -> tuple[int, int]:
    parts = text.split("/", 1)
    if len(parts) != 2:
        raise EvidenceError("expected CPU multiplier as numerator/denominator")
    try:
        numerator = int(parts[0], 10)
        denominator = int(parts[1], 10)
    except ValueError as exc:
        raise EvidenceError("CPU multiplier must contain decimal integers") from exc
    if numerator <= 0 or denominator <= 0:
        raise EvidenceError("CPU multiplier must be positive")
    divisor = math.gcd(numerator, denominator)
    return numerator // divisor, denominator // divisor


def _expected_cpu_total(frame_number: int, numerator: int, denominator: int) -> int:
    return (frame_number * CPU_CYCLES_PER_NATIVE_TICK * numerator) // denominator


def verify_document(
    document: Any,
    expected_frames: int = 3_600,
    expected_multiplier: tuple[int, int] | None = None,
) -> dict[str, Any]:
    """Return a canonical summary or raise ``EvidenceError``."""

    if not isinstance(document, dict) or document.get("format") != FORMAT:
        raise EvidenceError(f"format must be {FORMAT}")
    if isinstance(expected_frames, bool) or not isinstance(expected_frames, int):
        raise EvidenceError("expected frame count must be an integer")
    if expected_frames <= 0 or expected_frames > MAX_FRAMES:
        raise EvidenceError(f"expected frame count must be in 1..{MAX_FRAMES}")

    multiplier = _ratio(document.get("cpu_multiplier"), "cpu_multiplier")
    if expected_multiplier is not None and multiplier != expected_multiplier:
        expected_text = f"{expected_multiplier[0]}/{expected_multiplier[1]}"
        actual_text = f"{multiplier[0]}/{multiplier[1]}"
        raise EvidenceError(
            f"CPU multiplier is {actual_text}; expected {expected_text}"
        )

    frames = document.get("frames")
    if not isinstance(frames, list) or len(frames) != expected_frames:
        actual = len(frames) if isinstance(frames, list) else "non-array"
        raise EvidenceError(
            f"evidence contains {actual} frames; expected {expected_frames}"
        )
    if len(frames) > MAX_FRAMES:
        raise EvidenceError(f"frame count exceeds {MAX_FRAMES}")

    simulation_hashes: set[str] = set()
    previous_cpu_total = 0
    previous_cdda_total = 0
    for index, frame in enumerate(frames):
        if not isinstance(frame, dict):
            raise EvidenceError(f"frames[{index}] must be an object")
        frame_index = _uint(frame.get("frame_index"), f"frames[{index}].frame_index")
        if frame_index != index:
            raise EvidenceError(
                f"frames[{index}].frame_index is {frame_index}; expected {index}"
            )
        if _uint(frame.get("vblank_hz"), f"frames[{index}].vblank_hz") != VBLANK_HZ:
            raise EvidenceError(f"frames[{index}] is not a 100 Hz VBlank")
        if _uint(frame.get("timebase_ticks"), f"frames[{index}].timebase_ticks") != TIMEBASE_TICKS_PER_FRAME:
            raise EvidenceError(f"frames[{index}] does not advance one native timebase tick")
        if _uint(frame.get("elapsed_timebase_ticks"), f"frames[{index}].elapsed_timebase_ticks") != index + 1:
            raise EvidenceError(f"frames[{index}] has a non-contiguous native timeline")

        simulation_hash = _nonempty_text(
            frame.get("simulation_hash"), f"frames[{index}].simulation_hash"
        )
        if simulation_hash in simulation_hashes:
            raise EvidenceError(
                f"frames[{index}] repeats a simulation state; presentation hashes are not accepted as proof"
            )
        simulation_hashes.add(simulation_hash)

        frame_number = index + 1
        expected_cpu_total = _expected_cpu_total(
            frame_number, multiplier[0], multiplier[1]
        )
        cpu_total = _uint(frame.get("total_cpu_cycles"), f"frames[{index}].total_cpu_cycles")
        cpu_retirement_total = _uint(
            frame.get("cpu_retirement_total"),
            f"frames[{index}].cpu_retirement_total",
        )
        cpu_amount = _uint(frame.get("cpu_cycles"), f"frames[{index}].cpu_cycles")
        if cpu_total != expected_cpu_total or cpu_retirement_total != expected_cpu_total:
            raise EvidenceError(f"frames[{index}] violates exact CPU-retirement carry")
        if cpu_amount != expected_cpu_total - previous_cpu_total:
            raise EvidenceError(f"frames[{index}] has an incorrect CPU delta")
        previous_cpu_total = cpu_total

        spu_amount = _uint(frame.get("spu_samples"), f"frames[{index}].spu_samples")
        spu_total = _uint(
            frame.get("total_spu_samples"), f"frames[{index}].total_spu_samples"
        )
        if spu_amount != SPU_SAMPLES_PER_NATIVE_TICK or spu_total != frame_number * SPU_SAMPLES_PER_NATIVE_TICK:
            raise EvidenceError(f"frames[{index}] accelerates or drops the native SPU clock")

        cdda_amount = _uint(frame.get("cdda_sectors"), f"frames[{index}].cdda_sectors")
        cdda_total = _uint(
            frame.get("total_cdda_sectors"), f"frames[{index}].total_cdda_sectors"
        )
        expected_cdda_total = (frame_number * CDDA_SECTORS_PER_SECOND) // NATIVE_TIMEBASE_HZ
        expected_cdda_phase = (frame_number * CDDA_SECTORS_PER_SECOND) % NATIVE_TIMEBASE_HZ
        if cdda_total != expected_cdda_total or cdda_amount != cdda_total - previous_cdda_total:
            raise EvidenceError(f"frames[{index}] accelerates or drops the native CDDA clock")
        if _uint(frame.get("cdda_phase"), f"frames[{index}].cdda_phase") != expected_cdda_phase:
            raise EvidenceError(f"frames[{index}] has an incorrect CDDA phase")
        previous_cdda_total = cdda_total

    return {
        "format": RECEIPT_FORMAT,
        "vblank_hz": VBLANK_HZ,
        "frames": expected_frames,
        "unique_simulation_states": len(simulation_hashes),
        "cpu_multiplier": {
            "numerator": multiplier[0],
            "denominator": multiplier[1],
        },
        "total_cpu_cycles": previous_cpu_total,
        "total_spu_samples": expected_frames * SPU_SAMPLES_PER_NATIVE_TICK,
        "total_cdda_sectors": previous_cdda_total,
        "native_timebase_ticks": expected_frames,
        "cdda_phase": (expected_frames * CDDA_SECTORS_PER_SECOND) % NATIVE_TIMEBASE_HZ,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="100 Hz evidence JSON")
    parser.add_argument("--receipt", type=Path, help="optional canonical receipt path")
    parser.add_argument(
        "--expected-frames",
        type=int,
        default=3_600,
        help="required contiguous simulation frames (default: 3600)",
    )
    parser.add_argument(
        "--expected-cpu-multiplier",
        help="optional exact multiplier constraint, for example 1/1 or 9/1",
    )
    args = parser.parse_args()
    try:
        source = args.input.read_bytes()
        document = json.loads(source.decode("utf-8"))
        expected_multiplier = (
            parse_multiplier(args.expected_cpu_multiplier)
            if args.expected_cpu_multiplier is not None
            else None
        )
        summary = verify_document(document, args.expected_frames, expected_multiplier)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, EvidenceError) as exc:
        parser.error(str(exc))

    summary = {
        **summary,
        "input_sha256": hashlib.sha256(source).hexdigest(),
    }
    encoded = json.dumps(summary, sort_keys=True, separators=(",", ":")) + "\n"
    if args.receipt is not None:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.receipt.with_name(args.receipt.name + ".tmp")
        temporary.write_text(encoded, encoding="utf-8")
        os.replace(temporary, args.receipt)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
