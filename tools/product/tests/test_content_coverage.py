from __future__ import annotations

import json
import hashlib
import tempfile
import unittest
from pathlib import Path

from tools.product.content_coverage import (ContentCoverageError,
                                            build_content_coverage_ledger)
from tools.product.behavioral_runner import build_scenario_plan


class ContentCoverageTests(unittest.TestCase):
    def _inputs(self, root: Path) -> tuple[Path, Path, Path, Path]:
        def write(name: str, value: object) -> Path:
            path = root / name
            path.write_text(json.dumps(value), encoding="utf-8")
            return path
        payload = lambda classification, mapping: {
            "classification": classification, "static_mapping": mapping,
            "container": "/DISC/PAYLOAD", "container_sha256": "a" * 64,
            "inventory_sha256": "b" * 64, "expanded_sha256": "c" * 64}
        content = write("content.json", {
            "schema": "wo3se.content-static-coverage.v1",
            "summary": {"cataloged_payloads": 2, "authenticated_executable_payloads": 2},
            "media_payloads": [payload("authenticated-main-executable", "main-exe"),
                               payload("authenticated-runtime-executable",
                                       "nfe-entry5-route-variant")],
            "media_exclusions": [{"logical_path": f"/TRACK{number:02}.WAV"}
                                 for number in range(1, 14)],
            "additional_executable_candidates": []})
        images = [{"name": name, "authorized_instruction_count": count,
                   "compiled_instruction_count": count, "compiled_region_count": 1,
                   "excluded_instruction_count": 0}
                  for name, count in (("main-exe", 104002),
                                      ("nfe-entry5-route-variant", 31827))]
        shards = write("shards.json", {
            "schema": "wo3se.generated-game-shards.v1", "images": images,
            "compiled_instruction_count": 135829, "compiled_region_count": 2,
            "unresolved_cfi_site_count": 0})
        media = write("media.json", {"format_version": 1,
            "game": {"id": "SCES-02845", "cue_tracks": 14},
            "audio_tracks": [{"track": number, "size": 2352, "md5": "d" * 32}
                             for number in range(2, 15)]})
        policy = write("policy.json", {
            "schema": "wipeout3se.content-coverage-policy.v1", "game_id": "SCES-02845",
            "media": {"source": "verified-external-multi-file-cue-bin",
                      "redistribute_source_media": False,
                      "runtime_access": "cue-track-sector-and-cdda-routing"},
            "behavioral_routes": {"modes": ["single-race"], "tracks": ["porto-kora"]}})
        return content, shards, media, policy

    def test_media_and_static_pass_while_unexecuted_behavior_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            content, shards, media, policy = self._inputs(root)
            first = build_content_coverage_ledger(
                content_path=content, shards_path=shards,
                media_manifest_path=media, policy_path=policy)
            second = build_content_coverage_ledger(
                content_path=content, shards_path=shards,
                media_manifest_path=media, policy_path=policy)
            self.assertEqual(first, second)
            self.assertEqual(first["media_availability"]["status"], "pass")
            self.assertEqual(first["static_executable_coverage"]["status"], "pass")
            self.assertEqual(first["behavioral_execution"]["status"], "fail")
            self.assertEqual(first["behavioral_execution"]["unexecuted"], 2)

    def test_behavioral_evidence_must_be_clean_installed_product(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            content, shards, media, policy = self._inputs(root)
            path = root / "bad.json"
            path.write_text(json.dumps({"schema": "quarantined-route.v1"}), encoding="utf-8")
            with self.assertRaisesRegex(ContentCoverageError, "not eligible"):
                build_content_coverage_ledger(
                    content_path=content, shards_path=shards,
                    media_manifest_path=media, policy_path=policy,
                    behavioral_evidence=[path])

    def test_authenticated_execution_updates_only_proved_routes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            content, shards, media, policy = self._inputs(root)
            plan = build_scenario_plan(policy)
            plan_hash = hashlib.sha256((json.dumps(
                plan, sort_keys=True, separators=(",", ":")) + "\n").encode()).hexdigest()
            capture = {"path": "capture.bin", "sha256": "a" * 64, "bytes": 16}
            routes = ["modes:single-race", "tracks:porto-kora"]
            evidence = root / "execution.json"
            evidence.write_text(json.dumps({
                "schema": "wipeout3se.clean-aot.behavioral-execution.v1",
                "status": "passed", "game_id": "SCES-02845",
                "runtime": "installed-compiler-free-product",
                "external_media_verified": True,
                "media_manifest_sha256": hashlib.sha256(media.read_bytes()).hexdigest(),
                "package_manifest_sha256": "b" * 64, "executable_sha256": "c" * 64,
                "scenario_plan_sha256": plan_hash, "routes": routes,
                "executions": [{"route": route, "game_instructions": 100,
                    "authenticated_epoch_sha256": "d" * 64,
                    "captures": {kind: dict(capture) for kind in
                                 ("static_trace", "frame", "audio", "save")}}
                    for route in routes]}), encoding="utf-8")
            ledger = build_content_coverage_ledger(
                content_path=content, shards_path=shards, media_manifest_path=media,
                policy_path=policy, behavioral_evidence=[evidence])
            self.assertEqual(ledger["behavioral_execution"]["status"], "pass")
            self.assertEqual(ledger["behavioral_execution"]["executed"], 2)


if __name__ == "__main__":
    unittest.main()
