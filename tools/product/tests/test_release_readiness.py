from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from tools.product.release_readiness import audit_release_readiness


class ReleaseReadinessTests(unittest.TestCase):
    def test_missing_product_proofs_fail_closed_deterministically(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package = root / "package"
            package.mkdir(parents=True)
            (package / "policy").mkdir()
            (package / "Wipeout3SE.exe").write_bytes(b"MZ")
            (package / "policy" / "default-config.json").write_text(json.dumps({
                "schema": "wipeout3se.runtime-settings.v2",
                "saves": {"atomic": True}, "input": {"profile": "modern-gamepad"},
                "audio": {"sample_rate_hz": 44100}}), encoding="utf-8")
            evidence = root / "evidence"
            (evidence / "product" / "evidence").mkdir(parents=True)
            (evidence / "catalog" / "evidence").mkdir(parents=True)
            structural = {"status": "validated", "manifest_sha256": "a" * 64}
            first = audit_release_readiness(package, evidence, structural)
            second = audit_release_readiness(package, evidence, structural)
            self.assertEqual(first, second)
            self.assertFalse(first["game_ready"])
            failed = {item["name"] for item in first["gates"] if item["status"] == "fail"}
            self.assertIn("direct-bios-to-game-execution", failed)
            self.assertIn("external-media-payload-availability", failed)
            self.assertIn("static-executable-overlay-coverage", failed)
            self.assertIn("complete-behavioral-route-coverage", failed)
            self.assertIn("authenticated-media-execution", failed)
            self.assertIn("graphics-feature-matrix", failed)
            self.assertIn("current-product-performance-soak", failed)
            self.assertIn("memory-card-persistence-recovery", failed)
            passed = {item["name"] for item in first["gates"] if item["status"] == "pass"}
            self.assertIn("deterministic-receipt-integrity", passed)


if __name__ == "__main__":
    unittest.main()
