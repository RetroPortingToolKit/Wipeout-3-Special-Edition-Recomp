#!/usr/bin/env python3
"""Require independently reviewed, hash-bound HUD timer proof at every aspect."""

from __future__ import annotations

import argparse
from fractions import Fraction
import json
from pathlib import Path
import sys

try:
    from tools.verify_visible_race_timer import parse_aspect, sha256
except ModuleNotFoundError:  # Direct `py tools/...py` invocation.
    from verify_visible_race_timer import parse_aspect, sha256


DEFAULT_ASPECTS = ("4:3", "16:9", "21:9", "32:9")


def verify_matrix(review_paths: list[Path], required_aspects: list[str]) -> dict:
    required = {parse_aspect(value): value for value in required_aspects}
    if len(required) != len(required_aspects):
        raise ValueError("required aspect list contains equivalent duplicates")
    found: dict[Fraction, dict] = {}

    for review_path in review_paths:
        review = json.loads(review_path.read_text(encoding="utf-8-sig"))
        if review.get("verdict") != "PASS":
            raise ValueError(f"timer review is not PASS: {review_path}")
        visual = review.get("visual_requirements", {})
        if not visual.get("post_start_chase_visible") or not visual.get("stock_race_hud_legible"):
            raise ValueError(f"timer review lacks explicit visual confirmation: {review_path}")
        geometry = review.get("geometry", {})
        aspect = parse_aspect(str(geometry.get("aspect", "")))
        expected = parse_aspect(str(geometry.get("expected_aspect", "")))
        if aspect != expected:
            raise ValueError(f"reviewed and expected aspect differ: {review_path}")
        if aspect not in required:
            raise ValueError(f"unexpected aspect in timer review: {review_path}")
        if aspect in found:
            raise ValueError(f"duplicate timer review for aspect {required[aspect]}")

        manifest_path = Path(review["capture_manifest"])
        if not manifest_path.is_file():
            raise ValueError(f"capture manifest is missing: {manifest_path}")
        if sha256(manifest_path) != str(review.get("capture_manifest_sha256", "")).lower():
            raise ValueError(f"capture manifest hash differs: {manifest_path}")
        frames = review.get("frames", [])
        if len(frames) < 3:
            raise ValueError(f"timer review has fewer than three frames: {review_path}")
        for frame in frames:
            image_path = Path(frame["path"])
            if not image_path.is_file() or sha256(image_path) != str(frame.get("sha256", "")).lower():
                raise ValueError(f"reviewed frame is missing or changed: {image_path}")
            if frame.get("client_size") != geometry.get("client_size"):
                raise ValueError(f"reviewed frame geometry differs: {image_path}")
        if not review.get("windows") or not all(window.get("pass") for window in review["windows"]):
            raise ValueError(f"timer review contains a failing interval: {review_path}")
        aggregate = float(review["aggregate_ratio"])
        aggregate_limits = review["limits"]["aggregate"]
        if not float(aggregate_limits[0]) <= aggregate <= float(aggregate_limits[1]):
            raise ValueError(f"timer review aggregate is outside its limits: {review_path}")

        found[aspect] = {
            "aspect": required[aspect],
            "client_size": geometry["client_size"],
            "review": str(review_path.resolve()),
            "review_sha256": sha256(review_path),
            "capture_manifest": str(manifest_path.resolve()),
            "capture_manifest_sha256": sha256(manifest_path),
            "aggregate_ratio": aggregate,
            "frame_count": len(frames),
            "frame_sha256": [frame["sha256"] for frame in frames],
        }

    missing = [label for aspect, label in required.items() if aspect not in found]
    if missing:
        raise ValueError("missing timer review for: " + ", ".join(missing))
    return {
        "schema": 1,
        "verdict": "PASS",
        "evidence": "four independent PresentShot HUD timer reviews; FPS excluded",
        "required_aspects": required_aspects,
        "aspects": [found[parse_aspect(value)] for value in required_aspects],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("review", type=Path, nargs="+")
    parser.add_argument("--required-aspect", action="append", dest="required_aspects")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = verify_matrix(args.review, args.required_aspects or list(DEFAULT_ASPECTS))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
