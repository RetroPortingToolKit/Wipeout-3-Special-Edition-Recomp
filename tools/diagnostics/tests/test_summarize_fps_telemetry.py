from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "summarize_fps_telemetry.py"
SPEC = importlib.util.spec_from_file_location("summarize_fps_telemetry", TOOL)
assert SPEC is not None and SPEC.loader is not None
TELEMETRY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TELEMETRY)

CONFIG_LINE = (
    "[CLOCK] title_crtc=PALx2 base=50Hz multiplier=2x "
    "effective=99.493634Hz nominal=100Hz cpu=900% "
    "device_clock=33.8688MHz spu=44100Hz\n"
)


def clock_line(
    fps: float,
    speed: float,
    frames: int,
    *,
    idle_skips: int = 0,
    idle_native: int = 0,
    cdda_playing: int = 0,
    cdda_effective: float = 0.0,
    dispatch_validity: tuple[int, int] | None = None,
    explicit_audio: bool = False,
    render: float | None = None,
) -> str:
    suffix = "" if render is None else f" render={render:.1f}fps"
    if explicit_audio:
        suffix += (
            " spu_effective_domain=sim_cycles spu_wall_hz=43000.0"
            " cdda_effective_domain=sim_cycles cdda_wall_sectors_s=70.00"
            " audio_output=1 audio_host_domain=callback_consumed"
            " audio_host_content=including_silence"
            " audio_host_frames=43900 audio_host_wall_hz=43890.0"
            " audio_fill_ms=32.5 audio_target_ms=40.0"
            " audio_correction=-0.00125 audio_pumps=100"
            " audio_backpressure=4 audio_underruns=2 audio_overflows=1"
            " audio_callback_calls=87 audio_callback_starved=3"
            " audio_callback_uncovered_max_ms=55.5"
            " av_sim_drift_ms=-2.50 audio_host_wall_drift_ms=+7.25"
        )
    if dispatch_validity is not None:
        suffix += (
            f" text_watched_writes={dispatch_validity[0]}"
            f" text_exact_revalidations={dispatch_validity[1]}"
        )
    return (
        f"[CLOCK] game={fps:.9f} fps speed={speed:.2f}x frames={frames} "
        "cpu_requested=900% cpu_effective=9.000x "
        "retired=304819200 cpu_native=33868800 device_clock=33868800 "
        "spu_frames=44100 spu_effective=44100.0Hz "
        f"cdda_playing={cdda_playing} cdda_track=3 cdda_sectors=75 "
        f"cdda_effective={cdda_effective:.2f}Hz mdec_decodes=12 "
        f"idle_skips={idle_skips} idle_native={idle_native} carry=0{suffix}\n"
    )


class SummarizeFpsTelemetryTests(unittest.TestCase):
    def test_multi_source_receipt_covers_rates_throughput_and_idle_strata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = TELEMETRY.DEFAULT_TARGET_FPS
            a_data = (
                CONFIG_LINE
                + clock_line(target * 0.5, 0.50, 50)
                + clock_line(
                    target + 0.000001,
                    1.00,
                    150,
                    idle_skips=4,
                    idle_native=12345,
                    render=120.0,
                )
            ).encode("utf-8")
            b_data = clock_line(
                target * 0.7,
                0.70,
                60,
                cdda_playing=1,
                cdda_effective=75.0,
            ).encode("utf-8")
            a_path = root / "a.stderr.log"
            b_path = root / "b.stderr.log"
            a_path.write_bytes(a_data)
            b_path.write_bytes(b_data)

            samples = []
            sources = []
            for path in (a_path, b_path):
                parsed, source = TELEMETRY.read_source(path)
                samples.extend(parsed)
                sources.append(source)
            receipt = TELEMETRY.build_receipt(samples, sources, target)

            self.assertEqual(receipt["schema"], "psx-fps-telemetry-v2")
            self.assertEqual(receipt["samples"], 3)
            self.assertEqual(receipt["vblank"]["below"]["60_fps"], 1)
            self.assertEqual(receipt["vblank"]["below"]["target_fps"], 2)
            self.assertEqual(
                receipt["vblank"]["normalized_bands"],
                {
                    "0_60_to_lt_0_95x": 1,
                    "0_95_to_lt_1_00x": 0,
                    "1_00x_or_more": 1,
                    "lt_0_60x": 1,
                },
            )
            self.assertEqual(receipt["render_fps"]["count"], 1)
            self.assertEqual(receipt["render_fps"]["p50"], 120.0)
            self.assertEqual(receipt["cpu_effective_ratio"]["mean"], 9.0)
            self.assertEqual(receipt["spu_effective_hz"]["mean"], 44100.0)
            self.assertEqual(receipt["cdda_active"]["samples"], 1)
            self.assertEqual(receipt["cdda_active"]["effective_hz"]["mean"], 75.0)
            self.assertEqual(
                receipt["audio_domains"]["simulated_time"]["spu_frame_hz"]["mean"],
                44100.0,
            )
            self.assertAlmostEqual(
                receipt["audio_domains"]["wall_generated"]
                ["cdda_active_sector_hz"]["mean"],
                52.5,
                places=5,
            )
            self.assertAlmostEqual(
                receipt["audio_domains"]["wall_generated"]
                ["cdda_active_pcm_frame_hz"]["mean"],
                30870.0,
                places=2,
            )
            self.assertEqual(
                receipt["audio_domains"]["wall_generated"]
                ["measurement_sources"],
                {"legacy_derived": 3, "runtime_explicit": 0},
            )
            self.assertEqual(
                receipt["audio_domains"]["callback_consumed"]["samples"], 0
            )
            self.assertEqual(receipt["dispatch_validity"]["samples"], 0)
            self.assertEqual(
                receipt["dispatch_validity"]["ratio_status"], "not_reported"
            )
            self.assertIsNone(
                receipt["dispatch_validity"]
                ["exact_revalidations_per_watched_write"]
            )
            self.assertAlmostEqual(
                receipt["audio_domains"]["wall_generated"]
                ["spu_frame_hz"]["min"],
                22050.0,
                places=2,
            )
            self.assertAlmostEqual(
                receipt["throughput_hz"]["raw_retired"]["min"],
                152409600.0,
                places=2,
            )
            self.assertAlmostEqual(
                receipt["throughput_hz"]["cpu_native"]["min"],
                16934400.0,
                places=2,
            )
            self.assertEqual(receipt["idle_skip"]["active"]["samples"], 1)
            self.assertEqual(receipt["idle_skip"]["inactive"]["samples"], 2)
            self.assertEqual(receipt["idle_skip"]["skip_events"], 4)
            self.assertEqual(receipt["idle_skip"]["native_cycles"], 12345)
            self.assertEqual(
                receipt["sources"][0]["sha256"], hashlib.sha256(a_data).hexdigest()
            )
            self.assertEqual(receipt["sources"][0]["configuration_records"], 1)
            self.assertEqual(
                receipt["sources"][1]["sha256"], hashlib.sha256(b_data).hexdigest()
            )

    def test_explicit_wall_and_callback_audio_fields_are_preserved_by_domain(self) -> None:
        target = TELEMETRY.DEFAULT_TARGET_FPS
        line = clock_line(
            target * 0.5,
            0.5,
            50,
            cdda_playing=1,
            cdda_effective=75.0,
            explicit_audio=True,
        )
        sample = TELEMETRY.parse_clock_line(line, "explicit.log:1")
        self.assertEqual(sample["audio_host_domain"], "callback_consumed")
        self.assertEqual(sample["audio_correction"], -0.00125)
        self.assertEqual(sample["av_sim_drift_ms"], -2.5)

        receipt = TELEMETRY.build_receipt(
            [sample], [{"path": "explicit.log", "samples": 1}], target
        )
        domains = receipt["audio_domains"]
        self.assertEqual(
            domains["wall_generated"]["measurement_sources"],
            {"legacy_derived": 0, "runtime_explicit": 1},
        )
        # Explicit runtime wall counters win over the legacy 0.5x derivation.
        self.assertEqual(domains["wall_generated"]["spu_frame_hz"]["mean"], 43000.0)
        self.assertEqual(
            domains["wall_generated"]["cdda_active_sector_hz"]["mean"], 70.0
        )
        callback = domains["callback_consumed"]
        self.assertEqual(callback["samples"], 1)
        self.assertEqual(callback["host_frame_hz"]["mean"], 43890.0)
        self.assertEqual(callback["host_frames"], 43900)
        self.assertEqual(callback["events"]["underruns"], 2)
        self.assertEqual(callback["events"]["overflow_drops"], 1)
        self.assertEqual(callback["events"]["backpressure"], 4)
        self.assertEqual(callback["callback"]["calls"], 87)
        self.assertEqual(callback["callback"]["starvation_events"], 3)
        self.assertEqual(callback["callback"]["uncovered_max_ms"]["max"], 55.5)
        self.assertEqual(callback["output_control"]["fill_ms"]["mean"], 32.5)
        self.assertEqual(callback["output_control"]["target_ms"]["mean"], 40.0)
        self.assertEqual(callback["output_control"]["correction"]["mean"], -0.00125)
        self.assertEqual(callback["drift_ms"]["mean"], 7.25)
        self.assertFalse(callback["physical_device_delivery_proven"])
        self.assertEqual(
            domains["explicit_output_health"]["simulated_av_drift_ms"]["mean"],
            -2.5,
        )

    def test_partial_explicit_audio_record_is_rejected(self) -> None:
        line = clock_line(99.493634, 1.0, 100).rstrip() + " spu_wall_hz=44100.0\n"
        with self.assertRaisesRegex(
            TELEMETRY.TelemetryError, "incomplete explicit wall-audio record"
        ):
            TELEMETRY.parse_clock_line(line, "partial.log:1")

    def test_dispatch_validity_pair_is_summarized_without_overclaim(self) -> None:
        samples = [
            TELEMETRY.parse_clock_line(
                clock_line(
                    99.493634,
                    1.0,
                    100,
                    dispatch_validity=(4, 1),
                ),
                "dispatch.log:1",
            ),
            TELEMETRY.parse_clock_line(
                clock_line(
                    99.493634,
                    1.0,
                    200,
                    dispatch_validity=(6, 2),
                ),
                "dispatch.log:2",
            ),
        ]
        receipt = TELEMETRY.build_receipt(
            samples, [{"path": "dispatch.log", "samples": 2}]
        )
        validity = receipt["dispatch_validity"]
        self.assertEqual(validity["samples"], 2)
        self.assertEqual(
            validity["totals"],
            {"exact_revalidations": 3, "watched_writes": 10},
        )
        self.assertEqual(validity["per_window"]["watched_writes"]["p50"], 5.0)
        self.assertEqual(
            validity["per_window"]["exact_revalidations"]["p50"], 1.5
        )
        self.assertEqual(validity["exact_revalidations_per_watched_write"], 0.3)
        self.assertEqual(validity["ratio_status"], "computed")
        self.assertIn("do not by themselves prove", validity["interpretation"])

    def test_dispatch_validity_zero_denominator_is_explicit_null(self) -> None:
        sample = TELEMETRY.parse_clock_line(
            clock_line(
                99.493634,
                1.0,
                100,
                dispatch_validity=(0, 7),
            ),
            "zero.log:1",
        )
        validity = TELEMETRY.build_receipt(
            [sample], [{"path": "zero.log", "samples": 1}]
        )["dispatch_validity"]
        self.assertIsNone(validity["exact_revalidations_per_watched_write"])
        self.assertEqual(validity["ratio_status"], "zero_watched_writes")

    def test_partial_dispatch_validity_pair_is_rejected(self) -> None:
        line = (
            clock_line(99.493634, 1.0, 100).rstrip()
            + " text_watched_writes=1\n"
        )
        with self.assertRaisesRegex(
            TELEMETRY.TelemetryError, "incomplete dispatch-validity record"
        ):
            TELEMETRY.parse_clock_line(line, "partial-dispatch.log:1")

    def test_cli_output_is_stable_across_input_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            a_path = root / "a.log"
            z_path = root / "z.log"
            a_path.write_text(clock_line(99.493634, 1.0, 100), encoding="utf-8")
            z_path.write_text(clock_line(49.746817, 0.5, 50), encoding="utf-8")

            first = subprocess.run(
                [sys.executable, str(TOOL), str(z_path), str(a_path)],
                check=False,
                capture_output=True,
                text=True,
            )
            second = subprocess.run(
                [sys.executable, str(TOOL), str(a_path), str(z_path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(first.stdout, second.stdout)
            decoded = json.loads(first.stdout)
            self.assertEqual(decoded["samples"], 2)
            self.assertEqual(
                [Path(source["path"]).name for source in decoded["sources"]],
                ["a.log", "z.log"],
            )

    def test_malformed_clock_record_fails_clearly(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.log"
            path.write_text("[CLOCK] game=50.0 fps speed=0.50x\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TOOL), str(path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("malformed [CLOCK] record", result.stderr)
            self.assertIn("missing fields", result.stderr)
            self.assertIn(":1:", result.stderr)

    def test_malformed_clock_configuration_fails_clearly(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad-config.log"
            path.write_text(
                "[CLOCK] title_crtc=PALx2 base=50Hz multiplier=2x\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(TOOL), str(path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("malformed [CLOCK] configuration record", result.stderr)
            self.assertIn("missing fields", result.stderr)

    def test_valid_configuration_without_samples_reaches_no_sample_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config-only.log"
            path.write_text(CONFIG_LINE, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TOOL), str(path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("no [CLOCK] telemetry samples found", result.stderr)
            self.assertNotIn("malformed", result.stderr)

    def test_log_without_clock_samples_fails_clearly(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.log"
            path.write_text("ordinary stderr only\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TOOL), str(path)],
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, "")
            self.assertIn("no [CLOCK] telemetry samples found", result.stderr)


if __name__ == "__main__":
    unittest.main()
