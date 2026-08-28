import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_visible_race_timer import parse_aspect, parse_hud_time, verify


class VisibleRaceTimerTest(unittest.TestCase):
    def test_hud_time_formats(self):
        self.assertEqual(parse_hud_time("0:05.60"), 5.60)
        self.assertEqual(parse_hud_time("65.25"), 65.25)
        with self.assertRaises(ValueError):
            parse_hud_time("1:75.00")

    def fixture(self, wall_times):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name)
        frames = []
        for index, wall in enumerate(wall_times, 1):
            path = root / f"frame-{index:02d}.png"
            path.write_bytes(f"image-{index}".encode())
            frames.append(
                {
                    "index": index,
                    "path": str(path),
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "accepted": True,
                    "requested_elapsed_seconds": wall,
                    "staged_elapsed_seconds": wall + 0.002,
                    "client_size": "1920x1080",
                }
            )
        manifest = root / "visible-race-timer.json"
        manifest.write_text(
            json.dumps(
                {
                    "verdict": "REQUIRES_MANUAL_HUD_REVIEW",
                    "capture_mode": "PresentShot",
                    "accepted_frame_count": len(frames),
                    "minimum_accepted_frames": 3,
                    "frames": frames,
                }
            ),
            encoding="utf-8",
        )
        return temp, manifest

    def test_exact_visible_clock_passes(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            result = verify(
                manifest,
                [(1, 5.0, "0:05.00"), (2, 9.0, "0:09.00"),
                 (3, 13.0, "0:13.00")],
                0.95, 1.05, 0.99, 1.01,
            )
            self.assertEqual(result["verdict"], "PASS")
            self.assertEqual(result["geometry"]["aspect"], "16:9")
            self.assertEqual(len(result["capture_manifest_sha256"]), 64)

    def test_fast_visible_clock_fails_despite_frames(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            result = verify(
                manifest,
                [(1, 5.0, "0:05.00"), (2, 13.0, "0:13.00"),
                 (3, 21.0, "0:21.00")],
                0.95, 1.05, 0.99, 1.01,
            )
            self.assertEqual(result["verdict"], "FAIL")

    def test_tampered_frame_rejected(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            data = json.loads(manifest.read_text())
            Path(data["frames"][1]["path"]).write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                verify(
                    manifest,
                    [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                     (3, 13.0, "13.00")],
                    0.95, 1.05, 0.99, 1.01,
                )

    def test_window_capture_manifest_rejected(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            data = json.loads(manifest.read_text())
            data["capture_mode"] = "Window"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "PresentShot"):
                verify(
                    manifest,
                    [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                     (3, 13.0, "13.00")],
                    0.95, 1.05, 0.99, 1.01,
                )

    def test_ineligible_or_inconsistent_manifest_rejected(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            data = json.loads(manifest.read_text())
            data["verdict"] = "INVALID_CAPTURE_EVIDENCE"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not eligible"):
                verify(
                    manifest,
                    [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                     (3, 13.0, "13.00")],
                    0.95, 1.05, 0.99, 1.01,
                )

            data["verdict"] = "REQUIRES_MANUAL_HUD_REVIEW"
            data["accepted_frame_count"] = 2
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "inconsistent"):
                verify(
                    manifest,
                    [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                     (3, 13.0, "13.00")],
                    0.95, 1.05, 0.99, 1.01,
                )

    def test_expected_geometry_is_enforced(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            readings = [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                        (3, 13.0, "13.00")]
            result = verify(
                manifest, readings, 0.95, 1.05, 0.99, 1.01,
                expected_client_size="1920x1080", expected_aspect="16:9",
            )
            self.assertEqual(result["geometry"]["client_size"], "1920x1080")
            with self.assertRaisesRegex(ValueError, "does not match expected"):
                verify(
                    manifest, readings, 0.95, 1.05, 0.99, 1.01,
                    expected_aspect="21:9",
                )

    def test_mixed_geometry_is_rejected(self):
        temp, manifest = self.fixture([20.0, 24.0, 28.0])
        with temp:
            data = json.loads(manifest.read_text())
            data["frames"][2]["client_size"] = "2520x1080"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "mixed client sizes"):
                verify(
                    manifest,
                    [(1, 5.0, "5.00"), (2, 9.0, "9.00"),
                     (3, 13.0, "13.00")],
                    0.95, 1.05, 0.99, 1.01,
                )

    def test_aspect_parser_reduces_aliases(self):
        self.assertEqual(parse_aspect("21:9"), parse_aspect("7:3"))


if __name__ == "__main__":
    unittest.main()
