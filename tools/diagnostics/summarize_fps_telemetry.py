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
EXPLICIT_AUDIO_INTEGER_FIELDS = (
    "audio_output",
    "audio_host_frames",
    "audio_pumps",
    "audio_backpressure",
    "audio_underruns",
    "audio_overflows",
    "audio_callback_calls",
    "audio_callback_starved",
)
EXPLICIT_AUDIO_NONNEGATIVE_FLOAT_FIELDS = (
    "spu_wall_hz",
    "cdda_wall_sectors_s",
    "audio_host_wall_hz",
    "audio_fill_ms",
    "audio_target_ms",
    "audio_callback_uncovered_max_ms",
)
EXPLICIT_AUDIO_SIGNED_FLOAT_FIELDS = (
    "audio_correction",
    "av_sim_drift_ms",
    "audio_host_wall_drift_ms",
)
EXPLICIT_AUDIO_TOKEN_FIELDS = (
    "spu_effective_domain",
    "cdda_effective_domain",
    "audio_host_domain",
    "audio_host_content",
)
EXPLICIT_AUDIO_FIELDS = (
    EXPLICIT_AUDIO_INTEGER_FIELDS
    + EXPLICIT_AUDIO_NONNEGATIVE_FLOAT_FIELDS
    + EXPLICIT_AUDIO_SIGNED_FLOAT_FIELDS
    + EXPLICIT_AUDIO_TOKEN_FIELDS
)
DISPATCH_VALIDITY_FIELDS = (
    "text_watched_writes",
    "text_exact_revalidations",
)

Sample = dict[str, int | float | str]


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


def _parse_signed_float(value: str, field: str, location: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise TelemetryError(
            f"{location}: {field} must be numeric, got {value!r}"
        ) from exc
    if not math.isfinite(number):
        raise TelemetryError(f"{location}: {field} must be finite")
    return number


def parse_clock_line(line: str, location: str) -> Sample:
    """Parse one current runtime ``[CLOCK]`` record.

    Units printed after values (``fps``, ``x``, ``Hz``, ``%``) are deliberately
    ignored. Unknown fields are retained by the runtime but do not affect this
    versioned receipt.
    """

    marker = line.find(CLOCK_MARKER)
    if marker < 0:
        raise TelemetryError(f"{location}: missing {CLOCK_MARKER} marker")
    tokens: dict[str, str] = {}
    for match in TOKEN_RE.finditer(line[marker + len(CLOCK_MARKER):]):
        name, value = match.groups()
        if name in tokens:
            raise TelemetryError(f"{location}: duplicate field {name!r}")
        tokens[name] = value

    values: dict[str, str] = {}
    for match in FIELD_RE.finditer(line[marker + len(CLOCK_MARKER):]):
        name, value = match.groups()
        values[name] = value

    missing = [name for name in REQUIRED_FIELDS if name not in values]
    if missing:
        raise TelemetryError(
            f"{location}: malformed {CLOCK_MARKER} record; missing fields: "
            + ", ".join(missing)
        )

    sample: Sample = {}
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

    explicit_present = [name for name in EXPLICIT_AUDIO_FIELDS if name in tokens]
    if explicit_present:
        explicit_missing = [
            name for name in EXPLICIT_AUDIO_FIELDS if name not in tokens
        ]
        if explicit_missing:
            raise TelemetryError(
                f"{location}: incomplete explicit wall-audio record; missing fields: "
                + ", ".join(explicit_missing)
            )
        for name in EXPLICIT_AUDIO_INTEGER_FIELDS:
            sample[name] = _parse_int(tokens[name], name, location)
        for name in EXPLICIT_AUDIO_NONNEGATIVE_FLOAT_FIELDS:
            sample[name] = _parse_float(tokens[name], name, location)
        for name in EXPLICIT_AUDIO_SIGNED_FLOAT_FIELDS:
            sample[name] = _parse_signed_float(tokens[name], name, location)
        for name in EXPLICIT_AUDIO_TOKEN_FIELDS:
            sample[name] = tokens[name]

        if sample["audio_output"] not in (0, 1):
            raise TelemetryError(f"{location}: audio_output must be 0 or 1")
        if sample["spu_effective_domain"] != "sim_cycles":
            raise TelemetryError(
                f"{location}: unsupported spu_effective_domain="
                f"{sample['spu_effective_domain']!r}"
            )
        if sample["cdda_effective_domain"] != "sim_cycles":
            raise TelemetryError(
                f"{location}: unsupported cdda_effective_domain="
                f"{sample['cdda_effective_domain']!r}"
            )
        if sample["audio_host_content"] != "including_silence":
            raise TelemetryError(
                f"{location}: unsupported audio_host_content="
                f"{sample['audio_host_content']!r}"
            )
        host_domain = sample["audio_host_domain"]
        if host_domain not in ("none", "callback_consumed", "enqueue_produced"):
            raise TelemetryError(
                f"{location}: unsupported audio_host_domain={host_domain!r}"
            )
        if (sample["audio_output"] == 0) != (host_domain == "none"):
            raise TelemetryError(
                f"{location}: audio_output and audio_host_domain disagree"
            )

    dispatch_present = [
        name for name in DISPATCH_VALIDITY_FIELDS if name in tokens
    ]
    if dispatch_present:
        dispatch_missing = [
            name for name in DISPATCH_VALIDITY_FIELDS if name not in tokens
        ]
        if dispatch_missing:
            raise TelemetryError(
                f"{location}: incomplete dispatch-validity record; missing fields: "
                + ", ".join(dispatch_missing)
            )
        for name in DISPATCH_VALIDITY_FIELDS:
            sample[name] = _parse_int(tokens[name], name, location)

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


def read_source(path: Path) -> tuple[list[Sample], dict[str, object]]:
    try:
        data = path.read_bytes()
        text = data.decode("utf-8-sig")
    except (OSError, UnicodeError) as exc:
        raise TelemetryError(f"{_display_path(path)}: cannot read UTF-8 log: {exc}") from exc

    samples: list[Sample] = []
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
    sample: Sample, target_fps: float
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


def _wall_audio(
    sample: Sample, target_fps: float
) -> dict[str, float | str]:
    """Convert simulated-domain device rates to generated-per-wall-second.

    Runtime ``spu_effective`` and ``cdda_effective`` are normalized against
    simulated native device cycles. Multiplying them by measured VBlank speed
    (game FPS / target FPS) gives the amount generated per host wall second.
    This deliberately does not claim physical audio-device callback delivery.
    """

    wall_speed = float(sample["game"]) / target_fps
    explicit = "spu_wall_hz" in sample
    return {
        "cdda_pcm_frames": (
            float(sample["cdda_wall_sectors_s"]) * 588.0
            if explicit
            else float(sample["cdda_effective"]) * 588.0 * wall_speed
        ),
        "cdda_sectors": (
            float(sample["cdda_wall_sectors_s"])
            if explicit
            else float(sample["cdda_effective"]) * wall_speed
        ),
        "source": "runtime_explicit" if explicit else "legacy_derived",
        "spu_frames": (
            float(sample["spu_wall_hz"])
            if explicit
            else float(sample["spu_effective"]) * wall_speed
        ),
        "vblank_speed": wall_speed,
    }


def _stratum(
    samples: list[Sample],
    throughput: list[dict[str, float]],
) -> dict[str, object]:
    return {
        "cpu_native_hz": summarize(item["cpu_native"] for item in throughput),
        "fps": summarize(sample["game"] for sample in samples),
        "raw_retired_hz": summarize(item["raw_retired"] for item in throughput),
        "samples": len(samples),
        "speed": summarize(sample["speed"] for sample in samples),
    }


def _host_audio_domain(
    samples: list[Sample], domain: str, boundary: str
) -> dict[str, object]:
    selected = [sample for sample in samples if sample.get("audio_host_domain") == domain]
    return {
        "boundary": boundary,
        "callback": {
            "calls": sum(int(sample["audio_callback_calls"]) for sample in selected),
            "starvation_events": sum(
                int(sample["audio_callback_starved"]) for sample in selected
            ),
            "uncovered_max_ms": summarize(
                float(sample["audio_callback_uncovered_max_ms"])
                for sample in selected
            ),
        },
        "content": (
            sorted({str(sample["audio_host_content"]) for sample in selected})
        ),
        "drift_ms": summarize(
            float(sample["audio_host_wall_drift_ms"]) for sample in selected
        ),
        "events": {
            "backpressure": sum(
                int(sample["audio_backpressure"]) for sample in selected
            ),
            "overflow_drops": sum(
                int(sample["audio_overflows"]) for sample in selected
            ),
            "pump_calls": sum(int(sample["audio_pumps"]) for sample in selected),
            "underruns": sum(
                int(sample["audio_underruns"]) for sample in selected
            ),
        },
        "host_frame_hz": summarize(
            float(sample["audio_host_wall_hz"]) for sample in selected
        ),
        "host_frames": sum(int(sample["audio_host_frames"]) for sample in selected),
        "output_control": {
            "correction": summarize(
                float(sample["audio_correction"]) for sample in selected
            ),
            "fill_ms": summarize(
                float(sample["audio_fill_ms"]) for sample in selected
            ),
            "target_ms": summarize(
                float(sample["audio_target_ms"]) for sample in selected
            ),
        },
        "physical_device_delivery_proven": False,
        "samples": len(selected),
    }


def _dispatch_validity(samples: list[Sample]) -> dict[str, object]:
    selected = [
        sample for sample in samples if "text_watched_writes" in sample
    ]
    watched_total = sum(int(sample["text_watched_writes"]) for sample in selected)
    revalidation_total = sum(
        int(sample["text_exact_revalidations"]) for sample in selected
    )
    if not selected:
        ratio = None
        ratio_status = "not_reported"
    elif watched_total == 0:
        ratio = None
        ratio_status = "zero_watched_writes"
    else:
        ratio = _rounded(revalidation_total / watched_total)
        ratio_status = "computed"
    return {
        "exact_revalidations_per_watched_write": ratio,
        "interpretation": (
            "instrumentation activity counters only; they do not by themselves "
            "prove dispatch correctness or speed"
        ),
        "per_window": {
            "exact_revalidations": summarize(
                int(sample["text_exact_revalidations"]) for sample in selected
            ),
            "watched_writes": summarize(
                int(sample["text_watched_writes"]) for sample in selected
            ),
        },
        "ratio_status": ratio_status,
        "samples": len(selected),
        "totals": {
            "exact_revalidations": revalidation_total,
            "watched_writes": watched_total,
        },
    }


def build_receipt(
    samples: list[Sample],
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
    wall_audio = [_wall_audio(sample, target_fps) for sample in samples]
    active_indexes = [
        index for index, sample in enumerate(samples) if sample["idle_skips"] > 0
    ]
    inactive_indexes = [
        index for index, sample in enumerate(samples) if sample["idle_skips"] == 0
    ]

    def indexed(indexes: list[int]) -> tuple[list[Sample], list[dict[str, float]]]:
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
    cdda_wall = [
        wall_audio[index]
        for index, sample in enumerate(samples)
        if sample["cdda_playing"] != 0
    ]
    explicit_audio = [
        sample for sample in samples if "audio_host_domain" in sample
    ]
    wall_sources = {
        "legacy_derived": sum(
            item["source"] == "legacy_derived" for item in wall_audio
        ),
        "runtime_explicit": sum(
            item["source"] == "runtime_explicit" for item in wall_audio
        ),
    }

    return {
        "audio_domains": {
            "simulated_time": {
                "domain": "simulated PSX device cycles",
                "cdda_active_sector_hz": summarize(cdda_rates),
                "spu_frame_hz": summarize(
                    sample["spu_effective"] for sample in samples
                ),
            },
            "wall_generated": {
                "cdda_active_pcm_frame_hz": summarize(
                    item["cdda_pcm_frames"] for item in cdda_wall
                ),
                "cdda_active_sector_hz": summarize(
                    item["cdda_sectors"] for item in cdda_wall
                ),
                "derivation": (
                    "runtime wall-counter rates when present; older logs fall "
                    "back to simulated-domain rate * game_fps / target_fps"
                ),
                "measurement_sources": wall_sources,
                "physical_device_delivery_proven": False,
                "spu_frame_hz": summarize(
                    item["spu_frames"] for item in wall_audio
                ),
                "vblank_speed_ratio": summarize(
                    item["vblank_speed"] for item in wall_audio
                ),
            },
            "callback_consumed": _host_audio_domain(
                explicit_audio,
                "callback_consumed",
                "frames requested/consumed at the software host callback boundary; "
                "content includes silence; not proof of physical DAC delivery",
            ),
            "enqueue_produced": _host_audio_domain(
                explicit_audio,
                "enqueue_produced",
                "frames handed to the legacy software enqueue boundary; not callback "
                "consumption and not proof of physical DAC delivery",
            ),
            "inactive_output": _host_audio_domain(
                explicit_audio,
                "none",
                "runtime reports no active host output boundary; zero/nonzero "
                "health counters are retained but cannot prove delivery",
            ),
            "explicit_output_health": {
                "inactive_samples": sum(
                    sample["audio_host_domain"] == "none"
                    for sample in explicit_audio
                ),
                "samples": len(explicit_audio),
                "simulated_av_drift_ms": summarize(
                    float(sample["av_sim_drift_ms"])
                    for sample in explicit_audio
                ),
            },
        },
        "cdda_active": {
            "effective_hz": summarize(cdda_rates),
            "samples": len(cdda_rates),
        },
        "dispatch_validity": _dispatch_validity(samples),
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
        "schema": "psx-fps-telemetry-v2",
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
        samples: list[Sample] = []
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
