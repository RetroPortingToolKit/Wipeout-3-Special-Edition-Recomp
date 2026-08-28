#!/usr/bin/env python3

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_release_mod_hygiene import verify_project


DIRECT_MANIFEST = """\
format_version = 5
id = "wipeout3.enhancement.framerate-pal100"
version = "2.0.0"

[[plugin]]
feature = "framerate-pal100"
id = "wipeout3.framerate.pal-x2"
"""


class ReleaseModHygieneTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.catalog = self.root / "mods" / "preloaded" / "packages"
        package = self.catalog / "wipeout3.enhancement.framerate-pal100" / "2.0.0"
        package.mkdir(parents=True)
        (package / "manifest.toml").write_text(DIRECT_MANIFEST, encoding="utf-8")
        (self.root / "game.toml").write_text(
            """\
[recompiler]

[runtime]
overlay_cache = true
overlay_backend = "gcc"
overlay_region_floor = "0x16F000"
main_ram_mib = 8
""",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def assert_rejected(self, expected: str) -> None:
        errors = verify_project(self.root)
        self.assertTrue(any(expected in error for error in errors), errors)

    def test_direct_plugin_catalog_passes(self) -> None:
        self.assertEqual([], verify_project(self.root))

    def test_legacy_catalog_is_rejected(self) -> None:
        legacy = self.catalog / "wipeout3.enhancement.framerate-pal100" / "1.1.0"
        legacy.mkdir(parents=True)
        self.assert_rejected("retired legacy package")

    def test_overlay_payload_is_rejected(self) -> None:
        (self.catalog / "payload.overlay").write_bytes(b"legacy")
        self.assert_rejected("forbidden patch/overlay payload")

    def test_xdelta_payload_is_rejected(self) -> None:
        (self.catalog / "pal_track01.xdelta3").write_bytes(b"legacy")
        self.assert_rejected("forbidden patch/overlay payload")

    def test_patch_table_is_rejected(self) -> None:
        manifest = next(self.catalog.rglob("manifest.toml"))
        manifest.write_text(DIRECT_MANIFEST + "\n[[patch]]\naddress = 0x80100000\n", encoding="utf-8")
        self.assert_rejected("forbidden guest patch/overlay table")

    def test_high_ram_address_is_rejected(self) -> None:
        manifest = next(self.catalog.rglob("manifest.toml"))
        manifest.write_text(DIRECT_MANIFEST + "\nload_address = 0x80300000\n", encoding="utf-8")
        self.assert_rejected("forbidden high-RAM guest address")

    def test_track_4x4_selector_is_rejected(self) -> None:
        manifest = next(self.catalog.rglob("manifest.toml"))
        manifest.write_text(DIRECT_MANIFEST + '\n[[option]]\nid = "track-4x4"\n', encoding="utf-8")
        self.assert_rejected("forbidden Track 4x4 selector")

    def test_retired_generator_is_rejected(self) -> None:
        generator = self.root / "tools" / "gen_hueponik_framerate_mod.py"
        generator.parent.mkdir()
        generator.write_text("# retired\n", encoding="utf-8")
        self.assert_rejected("retired legacy source/generator")

    def test_aot_recompiler_patch_is_rejected(self) -> None:
        (self.root / "game.toml").write_text(
            "[[recompiler.patch]]\naddress = 0x80100000\nvalue = 0\n",
            encoding="utf-8",
        )
        self.assert_rejected("forbidden recompiler/AOT patch catalog")

    def test_malformed_manifest_fails_closed(self) -> None:
        manifest = next(self.catalog.rglob("manifest.toml"))
        manifest.write_text("[[plugin]\n", encoding="utf-8")
        self.assert_rejected("cannot validate manifest")


if __name__ == "__main__":
    unittest.main()
