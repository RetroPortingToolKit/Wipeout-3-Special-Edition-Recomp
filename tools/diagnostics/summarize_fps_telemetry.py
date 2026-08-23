#!/usr/bin/env python3
"""Summarize PSX_FPS_TELEMETRY stderr records as a stable JSON receipt."""

from __future__ import annotations

import argparse
import glob
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Iterable


CLOCK_MARKER = "[CLOCK]"
DEFAULT_TARGET_FPS = 99.493634313370
NATIVE_CLOCK_HZ = 33_868_800.0
FIELD_RE = re.compile(
    r"(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)="
    r"([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)"
)
TOKEN_RE = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
REQUIRED_FIELDS = (
    "game",
    "speed",
    "frames",
    "cpu_effective",
    "retired",
    "cpu_native",
    "device_clock",
    "spu_effective",
    "cdda_playing",
    "cdda_effective",
    "idle_skips",
    "idle_native",
)
CONFIG_FIELDS = (
    "title_crtc",
    "base",
    "multiplier",
    "effective",
    "nominal",
    "cpu",
    "device_clock",
    "spu",
)
SAMPLE_HINT_FIELDS = (
    "game",
    "speed",
    "frames",
    "cpu_effective",
    "retired",
    "cpu_native",
    "spu_effective",
    "cdda_playing",
    "idle_skips",
    "idle_native",
)
INTEGER_FIELDS = (
    "frames",
    "retired",
    "cpu_native",
    "device_clock",
    "cdda_playing",
    "idle_skips",
    "idle_native",
)
RENDER_FIELDS = ("render_fps", "render", "display_fps", "display")


class TelemetryError(ValueError):
    """Input cannot produce a trustworthy telemetry receipt."""


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _display_path(path: Path) -> str:
    return path.resolve().as_posix()


def _parse_int(value: str, field: str, location: str) -> int:
    try:
        number = int(value, 10)
    except ValueError as exc:
        raise TelemetryError(
            f"{location}: {field} must be a base-10 integer, got {value!r}"
        ) from exc
    if number < 0:
        raise TelemetryError(f"{location}: {field} must be non-negative")
    return number


def _parse_float(value: str, field: str, location: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise TelemetryError(
            f"{location}: {field} must be numeric, got {value!r}"
        ) from exc
    if not math.isfinite(number) or number < 0.0:
        raise TelemetryError(
            f"{location}: {field} must be finite and non-negative"
        )
    return number


def parse_clock_line(line: str, location: str) -> dict[str, int | float]:
    """Parse one current runtime ``[CLOCK]`` record.

    Units printed after values (``fps``, ``x``, ``Hz``, ``%``) are deliberately
    ignored. Unknown fields are retained by the runtime but do not affect this
    versioned receipt.
    """

    marker = line.find(CLOCK_MARKER)
    if marker < 0:
        raise TelemetryError(f"{location}: missing {CLOCK_MARKER} marker")
    values: dict[str, str] = {}
    for match in FIELD_RE.finditer(line[marker + len(CLOCK_MARKER):]):
        name, value = match.groups()
        if name in values:
            raise TelemetryError(f"{location}: duplicate field {name!r}")
        values[name] = value

    missing = [name for name in REQUIRED_FIELDS if name not in values]
    if missing:
        raise TelemetryError(
            f"{location}: malformed {CLOCK_MARKER} record; missing fields: "
            + ", ".join(missing)
        )

    sample: dict[str, int | float] = {}
    for name in REQUIRED_FIELDS:
        if name in INTEGER_FIELDS:
            sample[name] = _parse_int(values[name], name, location)
        else:
            sample[name] = _parse_float(values[name], name, location)

    render_names = [name for name in RENDER_FIELDS if name in values]
    if len(render_names) > 1:
        raise TelemetryError(
            f"{location}: multiple render FPS fields: {', '.join(render_names)}"
        )
    if render_names:
        sample["render_fps"] = _parse_float(
            values[render_names[0]], render_names[0], location
        )

    if sample["device_clock"] == 0:
        raise TelemetryError(f"{location}: device_clock must be greater than zero")
    return sample


def parse_clock_config(line: str, location: str) -> None:
    """Validate the non-sample clock-policy header emitted at startup."""

    marker = line.find(CLOCK_MARKER)
    if marker < 0:
        raise TelemetryError(f"{location}: missing {CLOCK_MARKER} marker")
    values: dict[str, str] = {}
    for match in TOKEN_RE.finditer(line[marker + len(CLOCK_MARKER):]):
        name, value = match.groups()
        if name in values:
            raise TelemetryError(f"{location}: duplicate field {name!r}")
        values[name] = value
    missing = [name for name in CONFIG_FIELDS if name not in values]
    if missing:
        raise TelemetryError(
            f"{location}: malformed {CLOCK_MARKER} configuration record; "
            "missing fields: " + ", ".join(missing)
        )

    patterns = {
        "title_crtc": r"[A-Za-z0-9_.+-]+",
        "base": r"\d+Hz",
        "multiplier": r"\d+x",
        "effective": r"\d+(?:\.\d+)?Hz",
        "nominal": r"\d+Hz",
        "cpu": r"\d+%",
        "device_clock": r"\d+(?:\.\d+)?MHz",
        "spu": r"\d+(?:\.\d+)?Hz",
    }
    for name, pattern in patterns.items():
        if re.fullmatch(pattern, values[name]) is None:
            raise TelemetryError(
                f"{location}: malformed {CLOCK_MARKER} configuration field "
                f"{name}={values[name]!r}"
            )


def read_source(path: Path) -> tuple[list[dict[str, int | float]], dict[str, object]]:
    try:
        data = path.read_bytes()
        text = data.decode("utf-8-sig")
    except (OSError, UnicodeError) as exc:
        raise TelemetryError(f"{_display_path(path)}: cannot read UTF-8 log: {exc}") from exc

    samples: list[dict[str, int | float]] = []
    configuration_records = 0
    for line_number, line in enumerate(text.splitlines(), 1):
        if CLOCK_MARKER not in line:
            continue
        location = f"{_display_path(path)}:{line_number}"
        if "title_crtc=" in line:
            parse_clock_config(line, location)
            configuration_records += 1
            continue
        if any(f"{name}=" in line for name in SAMPLE_HINT_FIELDS):
            samples.append(parse_clock_line(line, location))
            continue
        raise TelemetryError(
            f"{location}: unsupported {CLOCK_MARKER} record; expected a "
            "title_crtc configuration or game telemetry sample"
        )
    source = {
        "bytes": len(data),
        "configuration_records": configuration_records,
        "path": _display_path(path),
        "samples": len(samples),
        "sha256": _sha256(data),
    }
    return samples, source


def resolve_inputs(values: Iterable[str]) -> list[Path]:
    paths: dict[str, Path] = {}
    for value in values:
        literal = Path(value)
        if literal.is_file():
            matches = [literal]
        else:
            matches = [Path(item) for item in glob.glob(value)]
        if not matches:
            raise TelemetryError(f"input log not found: {value}")
        for path in matches:
            if not path.is_file():
                raise TelemetryError(f"input is not a file: {_display_path(path)}")
            resolved = path.resolve()
            paths[resolved.as_posix().casefold()] = resolved
    return [paths[key] for key in sorted(paths)]


def _rounded(value: float) -> float:
    return round(float(value), 6)


def _percentile(sorted_values: list[float], percentile: float) -> float:
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (len(sorted_values) - 1) * percentile
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    fraction = position - lower
    return sorted_values[lower] + (
        sorted_values[upper] - sorted_values[lower]
    ) * fraction


def summarize(values: Iterable[float | int]) -> dict[str, int | float]:
    ordered = sorted(float(value) for value in values)
    if not ordered:
        return {"count": 0}
    return {
        "count": len(ordered),
        "max": _rounded(ordered[-1]),
        "mean": _rounded(math.fsum(ordered) / len(ordered)),
        "min": _rounded(ordered[0]),
        "p01": _rounded(_percentile(ordered, 0.01)),
        "p05": _rounded(_percentile(ordered, 0.05)),
        "p50": _rounded(_percentile(ordered, 0.50)),
        "p95": _rounded(_percentile(ordered, 0.95)),
        "p99": _rounded(_percentile(ordered, 0.99)),
    }


def _throughput(
    sample: dict[str, int | float], target_fps: float
) -> dict[str, float]:
    # device_clock is native simulated time for this reporting window.  The
    # measured VBlank rate converts that interval to wall time without assuming
    # the window is exactly one second long.
    wall_scale = (float(sample["game"]) / target_fps) * NATIVE_CLOCK_HZ
    device_clock = float(sample["device_clock"])
    return {
        "raw_retired": float(sample["retired"]) / device_clock * wall_scale,
        "cpu_native": float(sample["cpu_native"]) / device_clock * wall_scale,
        "device_native": wall_scale,
    }


def _stratum(
    samples: list[dict[str, int | float]],
    throughput: list[dict[str, float]],
) -> dict[str, object]:
    return {
        "cpu_native_hz": summarize(item["cpu_native"] for item in throughput),
        "fps": summarize(sample["game"] for sample in samples),
        "raw_retired_hz": summarize(item["raw_retired"] for item in throughput),
        "samples": len(samples),
        "speed": summarize(sample["speed"] for sample in samples),
    }


def build_receipt(
    samples: list[dict[str, int | float]],
    sources: list[dict[str, object]],
    target_fps: float = DEFAULT_TARGET_FPS,
) -> dict[str, object]:
    if not math.isfinite(target_fps) or target_fps <= 0.0:
        raise TelemetryError("target FPS must be finite and greater than zero")
    if not samples:
        raise TelemetryError(
            f"no {CLOCK_MARKER} telemetry samples found in {len(sources)} input file(s)"
        )

    throughputs = [_throughput(sample, target_fps) for sample in samples]
    active_indexes = [
        index for index, sample in enumerate(samples) if sample["idle_skips"] > 0
    ]
    inactive_indexes = [
        index for index, sample in enumerate(samples) if sample["idle_skips"] == 0
    ]

    def indexed(indexes: list[int]) -> tuple[list[dict[str, int | float]], list[dict[str, float]]]:
        return (
            [samples[index] for index in indexes],
            [throughputs[index] for index in indexes],
        )

    active_samples, active_throughputs = indexed(active_indexes)
    inactive_samples, inactive_throughputs = indexed(inactive_indexes)
    normalized = [float(sample["game"]) / target_fps for sample in samples]
    cdda_rates = [
        sample["cdda_effective"]
        for sample in samples
        if sample["cdda_playing"] != 0
    ]

    return {
        "cdda_active": {
            "effective_hz": summarize(cdda_rates),
            "samples": len(cdda_rates),
        },
        "cpu_effective_ratio": summarize(
            sample["cpu_effective"] for sample in samples
        ),
        "idle_skip": {
            "active": _stratum(active_samples, active_throughputs),
            "inactive": _stratum(inactive_samples, inactive_throughputs),
            "native_cycles": sum(int(sample["idle_native"]) for sample in samples),
            "skip_events": sum(int(sample["idle_skips"]) for sample in samples),
        },
        "native_clock_hz": int(NATIVE_CLOCK_HZ),
        "render_fps": summarize(
            sample["render_fps"] for sample in samples if "render_fps" in sample
        ),
        "samples": len(samples),
        "schema": "psx-fps-telemetry-v1",
        "sources": sources,
        "spu_effective_hz": summarize(
            sample["spu_effective"] for sample in samples
        ),
        "target_fps": _rounded(target_fps),
        "throughput_hz": {
            "cpu_native": summarize(item["cpu_native"] for item in throughputs),
            "device_native": summarize(item["device_native"] for item in throughputs),
            "raw_retired": summarize(item["raw_retired"] for item in throughputs),
        },
        "vblank": {
            "below": {
                "60_fps": sum(float(sample["game"]) < 60.0 for sample in samples),
                "95_fps": sum(float(sample["game"]) < 95.0 for sample in samples),
                "reported_1_00x": sum(
                    float(sample["speed"]) < 1.0 for sample in samples
                ),
                "target_fps": sum(
                    float(sample["game"]) < target_fps for sample in samples
                ),
            },
            "fps": summarize(sample["game"] for sample in samples),
            "normalized_bands": {
                "0_60_to_lt_0_95x": sum(0.60 <= value < 0.95 for value in normalized),
                "0_95_to_lt_1_00x": sum(0.95 <= value < 1.00 for value in normalized),
                "1_00x_or_more": sum(value >= 1.00 for value in normalized),
                "lt_0_60x": sum(value < 0.60 for value in normalized),
            },
            "speed": summarize(sample["speed"] for sample in samples),
        },
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", help="stderr log paths or glob patterns")
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument(
        "--target-fps",
        type=float,
        default=DEFAULT_TARGET_FPS,
        help=f"VBlank target used for below-target and throughput analysis (default: {DEFAULT_TARGET_FPS:.12f})",
    )
    return parser.parse_args(argv)


def run(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        paths = resolve_inputs(args.logs)
        if args.output and args.output.resolve() in paths:
            raise TelemetryError("output path must not overwrite an input log")
        samples: list[dict[str, int | float]] = []
        sources: list[dict[str, object]] = []
        for path in paths:
            source_samples, source = read_source(path)
            samples.extend(source_samples)
            sources.append(source)
        receipt = build_receipt(samples, sources, args.target_fps)
    except TelemetryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    rendered = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.output:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8", newline="\n")
        except OSError as exc:
            print(f"error: cannot write {_display_path(args.output)}: {exc}", file=sys.stderr)
            return 2
    else:
        print(rendered, end="")
    return 0


def main() -> int:
    return run(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
