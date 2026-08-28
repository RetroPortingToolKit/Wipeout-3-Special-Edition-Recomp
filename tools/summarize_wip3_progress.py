#!/usr/bin/env python3
"""Fail-closed parser for the Release WipEout title-progression diagnostic."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


LINE_RE = re.compile(
    r"wip3 progression: vblanks=(?P<vblanks>\d+) "
    r"max_object_updates=(?P<max_updates>\d+) ratio=(?P<ratio>[0-9.]+) "
    r"unique_objects=(?P<unique>\d+) total_updates=(?P<total>\d+) "
    r"first_update_vblank=(?P<first>\d+) last_update_vblank=(?P<last>\d+) "
    r"eligible=(?P<eligible>[01])"
)

TIMING_RE = re.compile(
    r"wip3 timing: vblanks=(?P<vblanks>\d+) "
    r"scheduler_calls=(?P<calls>\d+) logical_ticks=(?P<ticks>\d+) "
    r"race_centiseconds=(?P<race_cs>\d+) "
    r"calls_per_vblank=(?P<calls_ratio>[0-9.]+) "
    r"ticks_per_vblank=(?P<ticks_ratio>[0-9.]+) "
    r"wall_seconds=(?P<wall>[0-9.]+) "
    r"vblanks_per_wall_second=(?P<vblank_hz>[0-9.]+) "
    r"ticks_per_wall_second=(?P<tick_hz>[0-9.]+) "
    r"race_cs_per_wall_second=(?P<race_cs_hz>[0-9.]+) "
    r"rate=(?P<rate>\d+) coefficient=0x(?P<coefficient>[0-9A-Fa-f]+) "
    r"exact_pal100=(?P<exact>[01])"
)


def parse_windows(text: str) -> list[dict[str, int | float | bool]]:
    windows: list[dict[str, int | float | bool]] = []
    for match in LINE_RE.finditer(text):
        windows.append(
            {
                "vblanks": int(match["vblanks"]),
                "max_object_updates": int(match["max_updates"]),
                "ratio": float(match["ratio"]),
                "unique_objects": int(match["unique"]),
                "total_updates": int(match["total"]),
                "first_update_vblank": int(match["first"]),
                "last_update_vblank": int(match["last"]),
                "eligible": match["eligible"] == "1",
            }
        )
    return windows


def parse_timing_windows(text: str) -> list[dict[str, int | float | bool]]:
    windows: list[dict[str, int | float | bool]] = []
    for match in TIMING_RE.finditer(text):
        windows.append(
            {
                "vblanks": int(match["vblanks"]),
                "scheduler_calls": int(match["calls"]),
                "logical_ticks": int(match["ticks"]),
                "race_centiseconds": int(match["race_cs"]),
                "calls_per_vblank": float(match["calls_ratio"]),
                "ticks_per_vblank": float(match["ticks_ratio"]),
                "wall_seconds": float(match["wall"]),
                "vblanks_per_wall_second": float(match["vblank_hz"]),
                "ticks_per_wall_second": float(match["tick_hz"]),
                "race_cs_per_wall_second": float(match["race_cs_hz"]),
                "rate": int(match["rate"]),
                "coefficient": int(match["coefficient"], 16),
                "exact_pal100": match["exact"] == "1",
            }
        )
    return windows


def summarize(
    text: str, expected: float, tolerance: float, minimum_windows: int
) -> dict[str, object]:
    windows = parse_windows(text)
    timing_windows = parse_timing_windows(text)
    eligible = [window for window in windows if window["eligible"]]
    lower = expected - tolerance
    upper = expected + tolerance
    passing = [
        window for window in eligible if lower <= float(window["ratio"]) <= upper
    ]
    checks = {
        "timing_window_count": len(timing_windows) >= minimum_windows,
        "all_timing_windows_exact_pal100": len(timing_windows) >= minimum_windows
        and all(window["exact_pal100"] for window in timing_windows),
        "eligible_window_count": len(eligible) >= minimum_windows,
        "all_eligible_windows_at_expected_speed": bool(eligible)
        and len(passing) == len(eligible),
    }
    return {
        "verdict": "PASS" if all(checks.values()) else "FAIL",
        "expected_ratio": expected,
        "tolerance": tolerance,
        "accepted_range": [lower, upper],
        "minimum_eligible_windows": minimum_windows,
        "checks": checks,
        "window_count": len(windows),
        "eligible_window_count": len(eligible),
        "passing_window_count": len(passing),
        "windows": windows,
        "timing_window_count": len(timing_windows),
        "timing_windows": timing_windows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--expected-ratio", type=float, default=1.0)
    parser.add_argument("--tolerance", type=float, default=0.1)
    parser.add_argument("--minimum-windows", type=int, default=2)
    args = parser.parse_args()
    if args.expected_ratio <= 0 or not 0 <= args.tolerance < args.expected_ratio:
        parser.error("expected ratio/tolerance do not form a positive range")
    if args.minimum_windows < 1:
        parser.error("minimum windows must be positive")

    report = summarize(
        args.log.read_text(encoding="utf-8", errors="replace"),
        args.expected_ratio,
        args.tolerance,
        args.minimum_windows,
    )
    rendered = json.dumps(report, indent=2) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8")
    print(
        "progression "
        f"{report['verdict']}: {report['passing_window_count']}/"
        f"{report['eligible_window_count']} eligible windows at expected speed"
    )
    return 0 if report["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
