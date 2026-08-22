from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.product.behavioral_runner import (BehavioralRunnerError,
                                             authenticate_observation,
                                             build_scenario_plan)


class BehavioralRunnerTests(unittest.TestCase):
    def test_plan_is_deterministic_and_covers_all_routes(self) -> None:
        policy = Path("clean-aot/product/content-coverage-policy.json")
        first = build_scenario_plan(policy)
        second = build_scenario_plan(policy)
        self.assertEqual(first, second)
        self.assertEqual(first["scenario_count"], 64)
        self.assertEqual(len({item["route"] for item in first["scenarios"]}), 64)
        self.assertTrue(all(item["actions"][0]["op"] == "await-authenticated-game-boot"
                            for item in first["scenarios"]))

    def _observation(self, root: Path, boot_status: str = "authenticated-game-running") -> tuple[Path, dict]:
        captures = {}
        for kind in ("static_trace", "frame", "audio", "save"):
            path = root / f"{kind}.bin"
            path.write_bytes((kind + "-authenticated").encode())
            captures[kind] = {"path": path.name,
                              "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                              "bytes": path.stat().st_size}
        document = {"schema": "wipeout3se.clean-aot.behavioral-observation.v1",
                    "status": "passed", "game_id": "SCES-02845",
                    "route": "modes:single-race", "scenario_sha256": "1" * 64,
                    "package_manifest_sha256": "2" * 64, "executable_sha256": "3" * 64,
                    "game_boot": {"status": boot_status, "game_instructions": 100,
                                  "authenticated_epoch_sha256": "4" * 64},
                    "external_media": {"verified": True, "manifest_sha256": "5" * 64},
                    "captures": captures}
        path = root / "observation.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return path, {"route": "modes:single-race"}

    def test_authenticated_capture_hashes_are_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path, scenario = self._observation(Path(temporary))
            result = authenticate_observation(
                observation_path=path, scenario=scenario, scenario_sha256="1" * 64,
                package_manifest_sha256="2" * 64, executable_sha256="3" * 64,
                media_manifest_sha256="5" * 64, game_id="SCES-02845")
            self.assertEqual(result["route"], "modes:single-race")
            self.assertEqual(set(result["captures"]), {"static_trace", "frame", "audio", "save"})

    def test_preboot_and_capture_mutation_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, scenario = self._observation(root, "bios-frontier")
            arguments = dict(observation_path=path, scenario=scenario,
                scenario_sha256="1" * 64, package_manifest_sha256="2" * 64,
                executable_sha256="3" * 64, media_manifest_sha256="5" * 64,
                game_id="SCES-02845")
            with self.assertRaisesRegex(BehavioralRunnerError, "dormant"):
                authenticate_observation(**arguments)
            path, scenario = self._observation(root)
            (root / "frame.bin").write_bytes(b"mutated")
            with self.assertRaisesRegex(BehavioralRunnerError, "identity mismatch"):
                authenticate_observation(**{**arguments, "observation_path": path,
                                            "scenario": scenario})


if __name__ == "__main__":
    unittest.main()
