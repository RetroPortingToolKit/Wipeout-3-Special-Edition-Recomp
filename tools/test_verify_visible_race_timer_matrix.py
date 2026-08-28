import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.verify_visible_race_timer_matrix import verify_matrix


class VisibleRaceTimerMatrixTest(unittest.TestCase):
    def fixture(self):
        temp = tempfile.TemporaryDirectory()
        root = Path(temp.name)
        reviews = []
        for label, size in (("4:3", "1280x960"), ("16:9", "1920x1080"),
                            ("21:9", "2520x1080"), ("32:9", "3840x1080")):
            tag = label.replace(":", "x")
            manifest = root / f"manifest-{tag}.json"
            manifest.write_text('{"capture_mode":"PresentShot"}', encoding="utf-8")
            frames = []
            for index in range(3):
                image = root / f"{tag}-{index}.png"
                image.write_bytes(f"{tag}-{index}".encode())
                frames.append({
                    "path": str(image),
                    "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                    "client_size": size,
                })
            review = root / f"review-{tag}.json"
            review.write_text(json.dumps({
                "verdict": "PASS",
                "visual_requirements": {
                    "post_start_chase_visible": True,
                    "stock_race_hud_legible": True,
                },
                "geometry": {
                    "aspect": label,
                    "expected_aspect": label,
                    "client_size": size,
                },
                "capture_manifest": str(manifest),
                "capture_manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest(),
                "aggregate_ratio": 1.0,
                "limits": {"aggregate": [0.99, 1.01]},
                "frames": frames,
                "windows": [{"pass": True}, {"pass": True}],
            }), encoding="utf-8")
            reviews.append(review)
        return temp, reviews

    def test_complete_matrix_passes_and_is_hash_bound(self):
        temp, reviews = self.fixture()
        with temp:
            result = verify_matrix(reviews, ["4:3", "16:9", "21:9", "32:9"])
            self.assertEqual(result["verdict"], "PASS")
            self.assertEqual([item["aspect"] for item in result["aspects"]],
                             ["4:3", "16:9", "21:9", "32:9"])
            self.assertTrue(all(len(item["review_sha256"]) == 64
                                for item in result["aspects"]))

    def test_missing_aspect_fails(self):
        temp, reviews = self.fixture()
        with temp, self.assertRaisesRegex(ValueError, "missing timer review for: 32:9"):
            verify_matrix(reviews[:3], ["4:3", "16:9", "21:9", "32:9"])

    def test_changed_frame_fails(self):
        temp, reviews = self.fixture()
        with temp:
            data = json.loads(reviews[2].read_text())
            Path(data["frames"][1]["path"]).write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "missing or changed"):
                verify_matrix(reviews, ["4:3", "16:9", "21:9", "32:9"])


if __name__ == "__main__":
    unittest.main()
