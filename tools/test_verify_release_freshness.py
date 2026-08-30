import os
import sys
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).parent))
import verify_release_freshness as verifier

CONFIG = """\
[video]
cpu_overclock = 200
geometry_correction = false
supersampling = 4

[runtime]
overlay_cache = false
"""
STATE = """\
format_version = 2

[[package]]
id = "wipeout3.enhancement.skip-fmv"
version = "1.0.0"

[[feature]]
package_id = "wipeout3.enhancement.skip-fmv"
id = "skip_fmv"
enabled = true
"""
MANIFEST = """\
format_version = 5
id = "wipeout3.enhancement.skip-fmv"
version = "1.0.0"

[[feature]]
id = "skip_fmv"
default_enabled = true
"""


class ReleaseFreshnessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        source_package = (
            self.root
            / "mods/preloaded/packages/wipeout3.enhancement.skip-fmv/1.0.0"
        )
        source_package.mkdir(parents=True)
        (source_package / "manifest.toml").write_text(MANIFEST, encoding="utf-8")
        (self.root / "game.toml").write_text(CONFIG, encoding="utf-8")
        (self.root / "mods/state.toml").write_text(STATE, encoding="utf-8")
        self.release = self.root / "release"
        release_package = (
            self.release / "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0"
        )
        release_package.mkdir(parents=True)
        (release_package / "manifest.toml").write_text(MANIFEST, encoding="utf-8")
        (self.release / "game.toml").write_text(CONFIG, encoding="utf-8")
        (self.release / "mods/state.toml").write_text(STATE, encoding="utf-8")
        self.exe = self.release / "game.exe"
        self.exe.write_bytes(b"exe")
        now = max((self.root / "game.toml").stat().st_mtime, (self.release / "game.toml").stat().st_mtime) + 2
        os.utime(self.exe, (now, now))

    def tearDown(self):
        self.tmp.cleanup()

    def test_current_release_passes(self):
        self.assertEqual(verifier.validate_release(self.exe, self.root), [])

    def test_stale_artifact_fails_closed(self):
        os.utime(self.exe, (1, 1))
        self.assertTrue(any("predates" in error for error in verifier.validate_release(self.exe, self.root)))

    def test_staged_defaults_and_catalog_must_match_source(self):
        (self.release / "game.toml").write_text(CONFIG.replace("cpu_overclock = 200", "cpu_overclock = 600"), encoding="utf-8")
        (self.release / "mods/state.toml").write_text(STATE.replace("enabled = true", "enabled = false", 1), encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(any("game.toml" in error and "source profile" in error for error in errors))
        self.assertTrue(any("mods/state.toml" in error and "source profile" in error for error in errors))

    def test_toml_layout_differences_do_not_fail(self):
        reordered = """\
[runtime]
overlay_cache=false
[video]
supersampling=4
geometry_correction=false
cpu_overclock=200
"""
        (self.release / "game.toml").write_text(reordered, encoding="utf-8")
        self.assertEqual(verifier.validate_release(self.exe, self.root), [])

    def test_zip_is_inspected(self):
        archive = self.root / "release.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
        self.assertEqual(verifier.validate_release(archive, self.root), [])

    def test_zip_ignores_nested_project_profiles(self):
        archive = self.root / "setup-host.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
            zip_file.writestr("psxrecomp/tools/cycle_testrom/game.toml", "[game]\n")
        self.assertEqual(verifier.validate_release(archive, self.root), [])

    def test_zip_rejects_ambiguous_profiles(self):
        archive = self.root / "ambiguous.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("./game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
        errors = verifier.validate_release(archive, self.root)
        self.assertTrue(any("multiple candidates for game.toml" in error for error in errors))

    def test_invalid_staged_toml_fails_closed(self):
        (self.release / "game.toml").write_text("[video\n", encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(any("cannot parse staged game.toml" in error for error in errors))

    def test_package_manifests_must_match_source(self):
        manifest = (
            self.release
            / "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml"
        )
        manifest.write_text(MANIFEST.replace("default_enabled = true", "default_enabled = false"), encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(any("manifest.toml" in error and "source profile" in error for error in errors))

    def test_package_catalog_must_match_source(self):
        unexpected = self.release / "mods/packages/legacy.package/1.0.0"
        unexpected.mkdir(parents=True)
        (unexpected / "manifest.toml").write_text(MANIFEST, encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(any("unexpected" in error and "legacy.package" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
