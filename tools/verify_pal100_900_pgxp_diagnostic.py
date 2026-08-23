"""Validate a focused PAL100/900%/PGXP diagnostic receipt.

This is intentionally an artifact gate, not a runtime probe.  The capture
harness may add extra fields, but each case must identify the requested
configuration, cover the required route phases, and explicitly report the
negative FMV/audio conditions.
"""
import argparse
import json
from pathlib import Path


PHASES = ("boot", "title_selector", "race", "save_load_state", "memory_card", "cd_load")


def validate(receipt):
    errors = []
    if receipt.get("overclock_percent") != 900:
        errors.append("overclock_percent must be 900")
    if receipt.get("pal_crtc_multiplier") != 2 or receipt.get("host_hz") != 100:
        errors.append("configuration must be PAL x2 at 100 Hz")
    if receipt.get("pgxp") is not True:
        errors.append("pgxp must be true")

    cases = receipt.get("cases")
    if not isinstance(cases, list) or not cases:
        return errors + ["cases must be a non-empty list"]
    seen = set()
    for index, case in enumerate(cases):
        name = case.get("name", f"case[{index}]")
        phase = case.get("phase")
        if phase not in PHASES:
            errors.append(f"{name}: phase must be one of {', '.join(PHASES)}")
        else:
            seen.add(phase)
        if case.get("pass") is not True:
            errors.append(f"{name}: pass must be true")
        if not case.get("evidence"):
            errors.append(f"{name}: evidence is required")
        if case.get("fmv_active") is not False:
            errors.append(f"{name}: fmv_active must be false for gameplay/load cases")
        if case.get("audio_underruns") != 0:
            errors.append(f"{name}: audio_underruns must be 0")

    missing = [phase for phase in PHASES if phase not in seen]
    if missing:
        errors.append("missing phases: " + ", ".join(missing))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    args = parser.parse_args()
    with args.receipt.open(encoding="utf-8") as stream:
        receipt = json.load(stream)
    errors = validate(receipt)
    if errors:
        print("FAIL PAL100/900/PGXP diagnostic")
        for error in errors:
            print(f"- {error}")
        return 1
    print("PASS PAL100/900/PGXP diagnostic: six required phases and exclusions verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
