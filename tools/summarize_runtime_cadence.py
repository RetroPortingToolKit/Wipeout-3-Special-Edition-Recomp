#!/usr/bin/env python3
"""Extract a compact, machine-readable verdict from runtime cadence logs."""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path


CADENCE_RE = re.compile(
    r"runtime cadence: guest=(?P<guest>[0-9.]+) Hz, "
    r"spu=(?P<spu>[0-9.]+) Hz, cdda=(?P<cdda>[0-9.]+) Hz "
    r"nonzero=\+(?P<nonzero>\d+).*?"
    r"underruns=\+(?P<underruns>\d+), overflows=\+(?P<overflows>\d+)"
)


def median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def rounded(value: float | None) -> float | None:
    return round(value, 3) if value is not None else None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="runtime stdout log")
    parser.add_argument("--json", dest="json_path", type=Path)
    parser.add_argument("--warmup-samples", type=int, default=2)
    parser.add_argument("--expected-guest-hz", type=float, default=100.0)
    parser.add_argument("--expected-audio-hz", type=float, default=44100.0)
    parser.add_argument("--guest-tolerance-hz", type=float, default=1.5)
    parser.add_argument("--spu-tolerance-hz", type=float, default=441.0)
    parser.add_argument("--cdda-tolerance-hz", type=float, default=882.0)
    parser.add_argument("--allow-no-cdda", action="store_true")
    args = parser.parse_args()

    if args.warmup_samples < 0:
        parser.error("--warmup-samples must be non-negative")
    try:
        text = args.log.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"cadence: ERROR: {exc}", file=sys.stderr)
        return 2

    samples = [
        {
            "guest_hz": float(match["guest"]),
            "spu_hz": float(match["spu"]),
            "cdda_hz": float(match["cdda"]),
            "cdda_nonzero_frames": int(match["nonzero"]),
            "underruns": int(match["underruns"]),
            "overflows": int(match["overflows"]),
        }
        for match in CADENCE_RE.finditer(text)
    ]
    used = samples[args.warmup_samples :]
    active_cdda = [s for s in used if s["cdda_nonzero_frames"] > 0]

    guest = median([s["guest_hz"] for s in used])
    spu = median([s["spu_hz"] for s in used])
    cdda = median([s["cdda_hz"] for s in active_cdda])
    underruns = sum(s["underruns"] for s in used)
    overflows = sum(s["overflows"] for s in used)

    checks = {
        "enough_samples": len(used) >= 2,
        "guest_rate": guest is not None
        and abs(guest - args.expected_guest_hz) <= args.guest_tolerance_hz,
        "spu_rate": spu is not None
        and abs(spu - args.expected_audio_hz) <= args.spu_tolerance_hz,
        "cdda_present": bool(active_cdda) or args.allow_no_cdda,
        "cdda_rate": (
            bool(active_cdda)
            and cdda is not None
            and abs(cdda - args.expected_audio_hz) <= args.cdda_tolerance_hz
        )
        or (not active_cdda and args.allow_no_cdda),
        "no_audio_faults": underruns == 0 and overflows == 0,
    }
    passed = all(checks.values())
    report = {
        "verdict": "PASS" if passed else "FAIL",
        "source_log": str(args.log.resolve()),
        "samples_total": len(samples),
        "samples_skipped_as_warmup": min(args.warmup_samples, len(samples)),
        "samples_used": len(used),
        "active_cdda_samples": len(active_cdda),
        "median": {
            "guest_hz": rounded(guest),
            "spu_hz": rounded(spu),
            "cdda_hz": rounded(cdda),
        },
        "audio_fault_deltas": {"underruns": underruns, "overflows": overflows},
        "expected": {
            "guest_hz": args.expected_guest_hz,
            "audio_hz": args.expected_audio_hz,
            "guest_tolerance_hz": args.guest_tolerance_hz,
            "spu_tolerance_hz": args.spu_tolerance_hz,
            "cdda_tolerance_hz": args.cdda_tolerance_hz,
        },
        "checks": checks,
    }

    if args.json_path:
        args.json_path.parent.mkdir(parents=True, exist_ok=True)
        args.json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    med = report["median"]
    print(
        f"cadence {report['verdict']}: samples={len(used)}, "
        f"guest={med['guest_hz']} Hz, spu={med['spu_hz']} Hz, "
        f"cdda={med['cdda_hz']} Hz ({len(active_cdda)} active), "
        f"faults={underruns} underrun/{overflows} overflow"
    )
    failed = [name for name, ok in checks.items() if not ok]
    if failed:
        print("failed checks: " + ", ".join(failed))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
