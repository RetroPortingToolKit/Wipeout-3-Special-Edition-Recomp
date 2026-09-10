"""Validate a bounded PGXP save-state continuity receipt."""
import argparse
import json
from pathlib import Path

ASPECTS = ("4:3", "16:9", "21:9")
PGXP_FIELDS = ("geometry", "texture", "depth")
OBSERVATIONS = ("geometry", "depth", "texture", "culling")


def validate(receipt):
    errors = []
    configured = receipt.get("configured_pgxp")
    effective = receipt.get("effective_pgxp")
    if not isinstance(configured, dict): errors.append("configured_pgxp must be an object")
    if not isinstance(effective, dict): errors.append("effective_pgxp must be an object")
    if not isinstance(configured, dict) or not isinstance(effective, dict): return errors
    for field in PGXP_FIELDS:
        if configured.get(field) is not True: errors.append(f"configured_pgxp.{field} must be true")
        if effective.get(field) is not True: errors.append(f"effective_pgxp.{field} must be true (no silent fallback)")
    reset = receipt.get("reset")
    if not isinstance(reset, dict) or reset.get("pass") is not True: errors.append("reset.pass must be true")
    elif reset.get("cleared_previous_state") is not True: errors.append("reset.cleared_previous_state must be true")
    cases = receipt.get("cases")
    if not isinstance(cases, list): return errors + ["cases must be a list"]
    seen = set()
    for index, case in enumerate(cases):
        name = case.get("name", f"case[{index}]")
        aspect = case.get("aspect")
        if aspect not in ASPECTS: errors.append(f"{name}: aspect must be one of {', '.join(ASPECTS)}")
        elif aspect in seen: errors.append(f"{name}: duplicate aspect {aspect}")
        else: seen.add(aspect)
        if case.get("pass") is not True: errors.append(f"{name}: pass must be true")
        save, load = case.get("save"), case.get("load")
        if not isinstance(save, dict) or not isinstance(load, dict):
            errors.append(f"{name}: save and load receipts are required")
        else:
            if load.get("generation") != save.get("generation", -1) + 1: errors.append(f"{name}: load generation must advance exactly once")
            if load.get("last_op") != "load" or load.get("last_ok") is not True: errors.append(f"{name}: load must be committed and successful")
        continuation = case.get("continuation")
        if not isinstance(continuation, dict) or continuation.get("pass") is not True:
            errors.append(f"{name}: continuation.pass must be true"); continue
        if continuation.get("frames") != continuation.get("expected_frames"): errors.append(f"{name}: continuation frame count mismatch")
        for observation in OBSERVATIONS:
            value = continuation.get(observation)
            if not isinstance(value, dict) or value.get("before") is None or value.get("before") != value.get("after"):
                errors.append(f"{name}: {observation} before/after comparison failed")
    missing = [aspect for aspect in ASPECTS if aspect not in seen]
    if missing: errors.append("missing aspects: " + ", ".join(missing))
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("receipt", type=Path); args = parser.parse_args()
    with args.receipt.open(encoding="utf-8") as stream: receipt = json.load(stream)
    errors = validate(receipt)
    if errors:
        print("FAIL PGXP save-state continuity"); [print(f"- {error}") for error in errors]; return 1
    print("PASS PGXP save-state continuity: configured/effective PGXP, reset, restore, and 4:3/16:9/21:9 comparisons verified"); return 0


if __name__ == "__main__": raise SystemExit(main())
