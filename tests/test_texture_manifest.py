from __future__ import annotations

import json
import re
import unittest
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "manifests" / "wip3out-hd-textures.json"
SHA256_RE = re.compile(r"[0-9a-f]{64}")
KEY_RE = re.compile(r"[0-9a-f]{1,8}-[0-9a-f]{1,8}")


class TextureManifestTests(unittest.TestCase):
    def test_committed_manifest_is_canonical_and_self_consistent(self) -> None:
        raw = MANIFEST.read_text(encoding="utf-8")
        report = json.loads(raw)
        self.assertEqual(raw, json.dumps(report, indent=2, sort_keys=True) + "\n")
        self.assertEqual(report["schema"], "wo3se.external-texture-pack.v1")
        self.assertEqual(report["status"], "valid-partial-pack")
        self.assertEqual(
            report["source"]["commit"],
            "039a0690448047401cef251594327a25fcc73fad",
        )

        textures = report["textures"]
        missing = report["missing_mappings"]
        self.assertEqual(report["texture_count"], len(textures))
        self.assertEqual(report["missing_mapping_count"], len(missing))
        self.assertEqual(report["mapping_count"], len(textures) + len(missing))
        self.assertEqual(
            report["total_texture_bytes"], sum(entry["size"] for entry in textures)
        )

        texture_keys = [entry["key"] for entry in textures]
        missing_keys = [entry["key"] for entry in missing]
        self.assertEqual(texture_keys, sorted(texture_keys))
        self.assertEqual(missing_keys, sorted(missing_keys))
        self.assertEqual(len(set(texture_keys + missing_keys)), report["mapping_count"])
        for entry in textures:
            self.assertIsNotNone(KEY_RE.fullmatch(entry["key"]))
            self.assertEqual(entry["file"], entry["key"] + ".png")
            self.assertIsNotNone(SHA256_RE.fullmatch(entry["sha256"]))
            self.assertGreater(entry["size"], 0)
            self.assertGreater(entry["png"]["width"], 0)
            self.assertGreater(entry["png"]["height"], 0)

        sha_to_keys: dict[str, list[str]] = defaultdict(list)
        for entry in textures:
            sha_to_keys[entry["sha256"]].append(entry["key"])
        expected_duplicates = [
            {"sha256": sha256, "keys": sorted(keys)}
            for sha256, keys in sorted(sha_to_keys.items())
            if len(keys) > 1
        ]
        self.assertEqual(report["duplicate_payloads"], expected_duplicates)


if __name__ == "__main__":
    unittest.main()
