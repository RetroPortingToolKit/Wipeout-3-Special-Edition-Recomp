import os
import sys
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).parent))
import verify_release_freshness as verifier


CONFIG = "\n".join(f"{key} = {value}" for key, value in verifier.REQUIRED_CONFIG.items())
STATE = "\n".join(
    f'[[feature]]\npackage_id = "{package}"\nenabled = true'
    for package in sorted(verifier.REQUIRED_PACKAGES)
)


class ReleaseFreshnessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "mods/preloaded/packages").mkdir(parents=True)
        (self.root / "game.toml").write_text(CONFIG, encoding="utf-8")
        self.release = self.root / "release"
        (self.release / "mods").mkdir(parents=True)
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

    def test_missing_default_and_wrong_catalog_fail(self):
        (self.release / "game.toml").write_text(CONFIG.replace("cpu_overclock = 900", "cpu_overclock = 600"), encoding="utf-8")
        (self.release / "mods/state.toml").write_text(STATE.replace("enabled = true", "enabled = false", 1), encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(any("cpu_overclock" in error for error in errors))
        self.assertTrue(any("catalog" in error for error in errors))

    def test_zip_is_inspected(self):
        archive = self.root / "release.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
        self.assertEqual(verifier.validate_release(archive, self.root), [])


if __name__ == "__main__":
    unittest.main()
