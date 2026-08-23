"""Validate a bounded PAL-x2 pacing receipt and classify common failures.

The receipt is produced by an external/runtime probe; this tool deliberately
does not run the game.  It keeps guest VBlank, title updates, backend swaps,
and audio production separate so a single misleading FPS number cannot pass.
"""
import argparse
import json
from pathlib import Path


def _rate(count, elapsed):
    return count / elapsed if elapsed else 0.0


def validate(receipt):
    errors = []
    diagnosis = []
    elapsed = receipt.get("elapsed_s")
    if not isinstance(elapsed, (int, float)) or elapsed <= 0:
        return ["elapsed_s must be positive"], []
    if receipt.get("target_hz") != 100:
        errors.append("target_hz must be 100")
    if receipt.get("pal_crtc_multiplier") != 2:
        errors.append("pal_crtc_multiplier must be 2")
    for field in ("vblank", "gameplay_updates", "backend_swaps", "audio_frames"):
        if not isinstance(receipt.get(field), (int, float)):
            errors.append(f"{field} is required")

    wall_pacer = receipt.get("wall_pacer") is True
    swap_interval = receipt.get("swap_interval")
    if wall_pacer and swap_interval not in (0, None):
        errors.append("double-blocking: wall pacer and blocking swap interval are both active")
        diagnosis.append("double-blocking: use exactly one cadence owner")
    if receipt.get("native_pal_x2") is True and swap_interval not in (0, None):
        errors.append("native PAL x2 requires effective swap_interval 0")

    debt = receipt.get("max_debt_periods")
    if isinstance(debt, (int, float)) and debt > 12:
        errors.append("max_debt_periods exceeds the bounded 12-period recovery window")

    if errors:
        return errors, diagnosis

    vblank_rate = _rate(receipt["vblank"], elapsed)
    gameplay_rate = _rate(receipt["gameplay_updates"], elapsed)
    swap_rate = _rate(receipt["backend_swaps"], elapsed)
    audio_rate = _rate(receipt["audio_frames"], elapsed)
    if not 99.0 <= vblank_rate <= 101.0:
        diagnosis.append(f"guest under-speed: VBlank {vblank_rate:.1f} Hz")
    if not 99.0 <= gameplay_rate <= 101.0:
        diagnosis.append(f"title gate/update rate is not 100 Hz: {gameplay_rate:.1f} Hz")
    if receipt.get("fmv") is True:
        if not 49.0 <= swap_rate <= 51.0:
            errors.append(f"PAL FMV backend swaps must be ~50 Hz, got {swap_rate:.1f}")
    elif not 99.0 <= swap_rate <= 101.0:
        diagnosis.append(f"gameplay presentation is not 100 Hz: {swap_rate:.1f} Hz")
    if not 43_700 <= audio_rate <= 44_500:
        diagnosis.append(f"audio production is not ~44.1 kHz: {audio_rate:.1f} Hz")
    if receipt.get("audio_underruns", 0) != 0:
        diagnosis.append("audio underruns accompany the pacing deficit")

    budget_ms = 1000.0 / receipt["target_hz"]
    work_ms = receipt.get("max_work_ms")
    if isinstance(work_ms, (int, float)) and work_ms > budget_ms:
        diagnosis.append(
            f"timer debt: max work {work_ms:.2f} ms exceeds {budget_ms:.2f} ms budget"
        )
        if work_ms > 1000.0 / 79.0:
            diagnosis.append("sustained over-budget work explains the observed 52-79 Hz")
    return errors, diagnosis


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    args = parser.parse_args()
    with args.receipt.open(encoding="utf-8") as stream:
        receipt = json.load(stream)
    errors, diagnosis = validate(receipt)
    if errors:
        print("FAIL PAL pacing diagnostic")
        for item in errors:
            print(f"- {item}")
        for item in diagnosis:
            print(f"! {item}")
        return 1
    print("PASS PAL pacing diagnostic")
    for item in diagnosis:
        print(f"! {item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
