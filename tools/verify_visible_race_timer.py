#!/usr/bin/env python3
"""Bind manually read stock HUD race times to monotonic captured frames."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from fractions import Fraction


TIME_RE = re.compile(r"^(?:(\d+):)?(\d+)(?:[.:](\d{1,2}))?$")


def parse_hud_time(value: str) -> float:
    match = TIME_RE.fullmatch(value.strip())
    if not match:
        raise ValueError(f"invalid HUD time {value!r}; use M:SS.CC or seconds")
    minutes = int(match.group(1) or 0)
    seconds = int(match.group(2))
    centiseconds = int((match.group(3) or "0").ljust(2, "0"))
    if match.group(1) is not None and seconds >= 60:
        raise ValueError(f"invalid seconds field in HUD time {value!r}")
    return minutes * 60.0 + seconds + centiseconds / 100.0


def parse_reading(value: str) -> tuple[int, float, str]:
    if "=" not in value:
        raise ValueError(f"invalid reading {value!r}; use FRAME=M:SS.CC")
    index_text, hud_text = value.split("=", 1)
    index = int(index_text)
    if index < 1:
        raise ValueError("frame index must be positive")
    return index, parse_hud_time(hud_text), hud_text


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_client_size(value: str) -> tuple[int, int]:
    match = re.fullmatch(r"(\d+)x(\d+)", str(value).strip())
    if not match:
        raise ValueError(f"invalid or missing client size {value!r}")
    width, height = int(match.group(1)), int(match.group(2))
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid client size {value!r}")
    return width, height


def parse_aspect(value: str) -> Fraction:
    match = re.fullmatch(r"(\d+):(\d+)", value.strip())
    if not match or int(match.group(1)) <= 0 or int(match.group(2)) <= 0:
        raise ValueError(f"invalid aspect {value!r}; use W:H")
    return Fraction(int(match.group(1)), int(match.group(2)))


def verify(
    manifest_path: Path,
    readings: list[tuple[int, float, str]],
    min_window: float,
    max_window: float,
    min_aggregate: float,
    max_aggregate: float,
    expected_client_size: str | None = None,
    expected_aspect: str | None = None,
) -> dict:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if manifest.get("capture_mode") != "PresentShot":
        raise ValueError("visible race timer proof requires PresentShot capture mode")
    if manifest.get("verdict") != "REQUIRES_MANUAL_HUD_REVIEW":
        raise ValueError("capture manifest is not eligible for manual HUD review")
    frames = {int(frame["index"]): frame for frame in manifest.get("frames", [])}
    accepted_frame_count = sum(bool(frame.get("accepted")) for frame in frames.values())
    if accepted_frame_count < 3:
        raise ValueError("capture manifest has fewer than three accepted frames")
    reported_accepted_count = manifest.get("accepted_frame_count")
    if reported_accepted_count is not None and int(reported_accepted_count) != accepted_frame_count:
        raise ValueError("capture manifest accepted-frame count is inconsistent")
    if len(readings) < 3:
        raise ValueError("at least three visually read post-start HUD frames are required")
    if len({index for index, _, _ in readings}) != len(readings):
        raise ValueError("duplicate frame reading")

    required_size = parse_client_size(expected_client_size) if expected_client_size else None
    required_aspect = parse_aspect(expected_aspect) if expected_aspect else None

    bound = []
    for index, hud_seconds, hud_text in sorted(readings):
        if index not in frames:
            raise ValueError(f"frame {index} is absent from capture manifest")
        frame = frames[index]
        if not frame.get("accepted"):
            raise ValueError(f"frame {index} was rejected by client capture")
        image_path = Path(frame["path"])
        if not image_path.is_absolute():
            image_path = manifest_path.parent / image_path
        if not image_path.is_file():
            raise ValueError(f"frame {index} image is missing: {image_path}")
        actual_hash = sha256(image_path)
        expected_hash = str(frame.get("sha256", "")).lower()
        if actual_hash != expected_hash:
            raise ValueError(f"frame {index} SHA-256 differs from capture manifest")
        client_size = parse_client_size(frame.get("client_size"))
        if required_size is not None and client_size != required_size:
            raise ValueError(
                f"frame {index} client size {client_size[0]}x{client_size[1]} "
                f"does not match expected {required_size[0]}x{required_size[1]}"
            )
        actual_aspect = Fraction(*client_size)
        if required_aspect is not None and actual_aspect != required_aspect:
            raise ValueError(
                f"frame {index} aspect {actual_aspect.numerator}:{actual_aspect.denominator} "
                f"does not match expected {required_aspect.numerator}:{required_aspect.denominator}"
            )
        bound.append(
            {
                "index": index,
                "path": str(image_path.resolve()),
                "sha256": actual_hash,
                "wall_seconds": float(frame["requested_elapsed_seconds"]),
                "staged_wall_seconds": (
                    float(frame["staged_elapsed_seconds"])
                    if frame.get("staged_elapsed_seconds") is not None else None
                ),
                "client_size": f"{client_size[0]}x{client_size[1]}",
                "hud_text": hud_text,
                "hud_seconds": hud_seconds,
            }
        )

    geometries = {frame["client_size"] for frame in bound}
    if len(geometries) != 1:
        raise ValueError("reviewed frames use mixed client sizes; verify each aspect separately")
    geometry = parse_client_size(bound[0]["client_size"])
    aspect = Fraction(*geometry)

    windows = []
    for first, second in zip(bound, bound[1:]):
        wall_delta = second["wall_seconds"] - first["wall_seconds"]
        hud_delta = second["hud_seconds"] - first["hud_seconds"]
        if wall_delta <= 0 or hud_delta <= 0:
            raise ValueError("capture wall time and visible HUD time must strictly advance")
        ratio = hud_delta / wall_delta
        windows.append(
            {
                "first": first["index"],
                "second": second["index"],
                "wall_delta_seconds": wall_delta,
                "hud_delta_seconds": hud_delta,
                "ratio": ratio,
                "pass": min_window <= ratio <= max_window,
            }
        )

    total_wall = bound[-1]["wall_seconds"] - bound[0]["wall_seconds"]
    total_hud = bound[-1]["hud_seconds"] - bound[0]["hud_seconds"]
    aggregate = total_hud / total_wall
    verdict = (
        "PASS"
        if all(window["pass"] for window in windows)
        and min_aggregate <= aggregate <= max_aggregate
        else "FAIL"
    )
    return {
        "schema": 1,
        "verdict": verdict,
        "capture_manifest": str(manifest_path.resolve()),
        "capture_manifest_sha256": sha256(manifest_path),
        "evidence": "manually transcribed stock in-race HUD timer; FPS excluded",
        "visual_requirements": {
            "post_start_chase_visible": True,
            "stock_race_hud_legible": True,
            "review_required_before_invocation": True,
        },
        "limits": {
            "window": [min_window, max_window],
            "aggregate": [min_aggregate, max_aggregate],
        },
        "geometry": {
            "client_size": bound[0]["client_size"],
            "aspect": f"{aspect.numerator}:{aspect.denominator}",
            "expected_client_size": expected_client_size,
            "expected_aspect": expected_aspect,
        },
        "aggregate_ratio": aggregate,
        "frames": bound,
        "windows": windows,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--reading", action="append", required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--min-window", type=float, default=0.95)
    parser.add_argument("--max-window", type=float, default=1.05)
    parser.add_argument("--min-aggregate", type=float, default=0.99)
    parser.add_argument("--max-aggregate", type=float, default=1.01)
    parser.add_argument("--expected-client-size")
    parser.add_argument("--expected-aspect")
    parser.add_argument("--confirm-post-start-chase", action="store_true")
    parser.add_argument("--confirm-hud-legible", action="store_true")
    args = parser.parse_args(argv)
    try:
        if not args.confirm_post_start_chase or not args.confirm_hud_legible:
            raise ValueError(
                "visual review requires --confirm-post-start-chase and --confirm-hud-legible"
            )
        readings = [parse_reading(value) for value in args.reading]
        result = verify(
            args.manifest,
            readings,
            args.min_window,
            args.max_window,
            args.min_aggregate,
            args.max_aggregate,
            args.expected_client_size,
            args.expected_aspect,
        )
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
