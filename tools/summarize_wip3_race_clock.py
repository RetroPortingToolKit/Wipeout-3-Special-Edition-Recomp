#!/usr/bin/env python3
"""Fail-closed acceptance check for WipEout 3 race-clock diagnostics."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


RACE_CLOCK_RE = re.compile(
    r"psxrecomp: wip3 race clock: "
    r"object=(?P<object>0x[0-9A-Fa-f]+) "
    r"start_cs=(?P<start_cs>\d+) end_cs=(?P<end_cs>\d+) "
    r"game_seconds=(?P<game_seconds>\d+(?:\.\d+)?) "
    r"wall_seconds=(?P<wall_seconds>\d+(?:\.\d+)?) "
    r"ratio=(?P<ratio>\d+(?:\.\d+)?) "
    r"samples=(?P<samples>\d+) zero_steps=(?P<zero_steps>\d+) "
    r"two_steps=(?P<two_steps>\d+) "
    r"average_step_cs=(?P<average_step_cs>\d+(?:\.\d+)?) "
    r"eligible=(?P<eligible>[01])"
)


def summarize(
    text: str,
    *,
    min_ratio: float = 0.95,
    max_ratio: float = 1.05,
    min_aggregate_ratio: float = 0.99,
    max_aggregate_ratio: float = 1.01,
    minimum_windows: int = 2,
    min_average_step_cs: float = 0.99,
    max_average_step_cs: float = 1.01,
) -> dict[str, Any]:
    windows: list[dict[str, Any]] = []
    errors: list[str] = []

    for match in RACE_CLOCK_RE.finditer(text):
        values = match.groupdict()
        start_cs = int(values["start_cs"])
        end_cs = int(values["end_cs"])
        game_seconds = float(values["game_seconds"])
        wall_seconds = float(values["wall_seconds"])
        reported_ratio = float(values["ratio"])
        samples = int(values["samples"])
        zero_steps = int(values["zero_steps"])
        two_steps = int(values["two_steps"])
        reported_average_step_cs = float(values["average_step_cs"])
        eligible = values["eligible"] == "1"
        calculated_game_seconds = (end_cs - start_cs) / 100.0
        calculated_ratio = (
            game_seconds / wall_seconds if wall_seconds > 0.0 else 0.0
        )
        calculated_average_step_cs = (
            (2.0 * two_steps) / samples if samples > 0 else 0.0
        )
        internally_consistent = (
            end_cs > start_cs
            and game_seconds > 0.0
            and wall_seconds > 0.0
            and abs(game_seconds - calculated_game_seconds) <= 0.000001
            and abs(reported_ratio - calculated_ratio) <= 0.00001
            and samples == zero_steps + two_steps
            and abs(
                reported_average_step_cs - calculated_average_step_cs
            )
            <= 0.00001
        )
        in_range = min_ratio <= calculated_ratio <= max_ratio
        step_semantics_valid = (
            zero_steps > 0
            and two_steps > 0
            and min_average_step_cs
            <= calculated_average_step_cs
            <= max_average_step_cs
        )
        accepted = (
            eligible
            and internally_consistent
            and in_range
            and step_semantics_valid
        )
        window = {
            "object": values["object"].lower(),
            "start_cs": start_cs,
            "end_cs": end_cs,
            "game_seconds": game_seconds,
            "wall_seconds": wall_seconds,
            "ratio": calculated_ratio,
            "samples": samples,
            "zero_steps": zero_steps,
            "two_steps": two_steps,
            "average_step_cs": calculated_average_step_cs,
            "eligible": eligible,
            "internally_consistent": internally_consistent,
            "in_range": in_range,
            "step_semantics_valid": step_semantics_valid,
            "accepted": accepted,
        }
        windows.append(window)
        if eligible and not internally_consistent:
            errors.append(f"window {len(windows)} is internally inconsistent")
        elif eligible and not in_range:
            errors.append(
                f"window {len(windows)} ratio {calculated_ratio:.6f} is outside "
                f"[{min_ratio:.6f}, {max_ratio:.6f}]"
            )
        elif eligible and not step_semantics_valid:
            errors.append(
                f"window {len(windows)} does not prove 0/2cs PAL100 "
                f"semantics (average {calculated_average_step_cs:.6f})"
            )

    eligible_windows = [window for window in windows if window["eligible"]]
    accepted_windows = [window for window in windows if window["accepted"]]
    if len(eligible_windows) < minimum_windows:
        errors.append(
            f"only {len(eligible_windows)} eligible race window(s); "
            f"need {minimum_windows}"
        )

    total_game_seconds = sum(w["game_seconds"] for w in eligible_windows)
    total_wall_seconds = sum(w["wall_seconds"] for w in eligible_windows)
    aggregate_ratio = (
        total_game_seconds / total_wall_seconds
        if total_wall_seconds > 0.0
        else None
    )
    aggregate_in_range = (
        aggregate_ratio is not None
        and min_aggregate_ratio <= aggregate_ratio <= max_aggregate_ratio
    )
    if len(eligible_windows) >= minimum_windows and not aggregate_in_range:
        aggregate_text = "n/a" if aggregate_ratio is None else f"{aggregate_ratio:.6f}"
        errors.append(
            f"aggregate race-clock ratio {aggregate_text} is outside "
            f"[{min_aggregate_ratio:.6f}, {max_aggregate_ratio:.6f}]"
        )
    all_eligible_accepted = len(accepted_windows) == len(eligible_windows)
    passed = (
        len(eligible_windows) >= minimum_windows
        and all_eligible_accepted
        and aggregate_in_range
        and not errors
    )

    return {
        "schema": 2,
        "verdict": "PASS" if passed else "FAIL",
        "limits": {
            "min_ratio": min_ratio,
            "max_ratio": max_ratio,
            "min_aggregate_ratio": min_aggregate_ratio,
            "max_aggregate_ratio": max_aggregate_ratio,
            "minimum_windows": minimum_windows,
            "min_average_step_cs": min_average_step_cs,
            "max_average_step_cs": max_average_step_cs,
        },
        "window_count": len(windows),
        "eligible_window_count": len(eligible_windows),
        "accepted_window_count": len(accepted_windows),
        "total_game_seconds": total_game_seconds,
        "total_wall_seconds": total_wall_seconds,
        "aggregate_ratio": aggregate_ratio,
        "aggregate_in_range": aggregate_in_range,
        "windows": windows,
        "errors": errors,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="runtime stdout/stderr log")
    parser.add_argument("--min-ratio", type=float, default=0.95)
    parser.add_argument("--max-ratio", type=float, default=1.05)
    parser.add_argument("--min-aggregate-ratio", type=float, default=0.99)
    parser.add_argument("--max-aggregate-ratio", type=float, default=1.01)
    parser.add_argument("--minimum-windows", type=int, default=2)
    parser.add_argument("--min-average-step-cs", type=float, default=0.99)
    parser.add_argument("--max-average-step-cs", type=float, default=1.01)
    parser.add_argument("--json", action="store_true", help="print full JSON")
    args = parser.parse_args(argv)

    if (
        args.minimum_windows < 1
        or args.min_ratio > args.max_ratio
        or args.min_aggregate_ratio > args.max_aggregate_ratio
        or args.min_average_step_cs > args.max_average_step_cs
    ):
        parser.error("invalid acceptance limits")
    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"FAIL: cannot read {args.log}: {exc}", file=sys.stderr)
        return 2

    result = summarize(
        text,
        min_ratio=args.min_ratio,
        max_ratio=args.max_ratio,
        min_aggregate_ratio=args.min_aggregate_ratio,
        max_aggregate_ratio=args.max_aggregate_ratio,
        minimum_windows=args.minimum_windows,
        min_average_step_cs=args.min_average_step_cs,
        max_average_step_cs=args.max_average_step_cs,
    )
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        aggregate = result["aggregate_ratio"]
        aggregate_text = "n/a" if aggregate is None else f"{aggregate:.6f}"
        print(
            f"{result['verdict']}: race-clock "
            f"eligible={result['eligible_window_count']} "
            f"accepted={result['accepted_window_count']} "
            f"game_seconds={result['total_game_seconds']:.6f} "
            f"wall_seconds={result['total_wall_seconds']:.6f} "
            f"ratio={aggregate_text}"
        )
        for error in result["errors"]:
            print(f"  - {error}")
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
