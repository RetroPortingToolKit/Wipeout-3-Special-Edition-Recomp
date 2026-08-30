import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
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
PINS = """\
psxrecomp=1111111 (1111111111111111111111111111111111111111)
recomp-ui=2222222 (2222222222222222222222222222222222222222)
recomp-net=3333333 (3333333333333333333333333333333333333333)
retcomm-rbengine=4444444 (4444444444444444444444444444444444444444)
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

    def test_setup_zip_must_contain_the_complete_rebuild_closure(self):
        archive = self.root / "setup-host.zip"
        for name in verifier.SETUP_REBUILD_FILES - set(verifier.BASE_PROFILE_FILES):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"source:{name}\n".encode())
        for directory in verifier.SETUP_REBUILD_DIRS:
            path = self.root / directory / "payload.bin"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"source:{directory}\n".encode())

        source_files: dict[str, bytes] = {}
        for name in verifier.SETUP_REBUILD_FILES:
            source_files[name] = (self.root / name).read_bytes()
        for directory in verifier.SETUP_REBUILD_DIRS:
            for path in (self.root / directory).rglob("*"):
                if path.is_file():
                    source_files[path.relative_to(self.root).as_posix()] = (
                        path.read_bytes()
                    )

        def write_archive(
            *,
            complete: bool,
            stale: str | None = None,
            unexpected: bool = False,
        ) -> None:
            with ZipFile(archive, "w") as zip_file:
                if complete:
                    for name, contents in source_files.items():
                        zip_file.writestr(
                            name, b"stale" if name == stale else contents
                        )
                else:
                    zip_file.writestr("game.toml", CONFIG)
                    zip_file.writestr("mods/state.toml", STATE)
                zip_file.writestr(
                    "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                    MANIFEST,
                )
                if unexpected:
                    zip_file.writestr("src/mods/retired.c", b"stale")

        write_archive(complete=False)
        errors = verifier.validate_release(
            archive, self.root, require_setup_closure=True
        )
        self.assertTrue(any("missing rebuild files" in error for error in errors))

        write_archive(complete=True)
        self.assertEqual(
            verifier.validate_release(
                archive, self.root, require_setup_closure=True
            ),
            [],
        )

        write_archive(complete=True, stale="codegen_setup.c")
        errors = verifier.validate_release(
            archive, self.root, require_setup_closure=True
        )
        self.assertTrue(any("codegen_setup.c" in error for error in errors))

        write_archive(complete=True, unexpected=True)
        errors = verifier.validate_release(
            archive, self.root, require_setup_closure=True
        )
        self.assertTrue(any("src/mods/retired.c" in error for error in errors))

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

    @mock.patch.object(verifier, "validate_authoritative_framework_pins", return_value=[])
    def test_directory_framework_pins_must_match_source(self, _validate_pins):
        (self.root / "framework_pins.txt").write_text(PINS, encoding="utf-8")
        (self.release / "framework_pins.txt").write_text(
            PINS.replace("1111111", "aaaaaaaa", 1), encoding="utf-8"
        )
        errors = verifier.validate_release(self.exe, self.root)
        self.assertTrue(
            any(
                "framework_pins.txt" in error and "source snapshot" in error
                for error in errors
            )
        )

    @mock.patch.object(verifier, "validate_authoritative_framework_pins", return_value=[])
    def test_zip_framework_pins_must_match_source(self, _validate_pins):
        (self.root / "framework_pins.txt").write_text(PINS, encoding="utf-8")
        archive = self.root / "release.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
            zip_file.writestr(
                "framework_pins.txt", PINS.replace("2222222", "bbbbbbb", 1)
            )
        errors = verifier.validate_release(archive, self.root)
        self.assertTrue(
            any(
                "framework_pins.txt" in error and "source snapshot" in error
                for error in errors
            )
        )

    @mock.patch.object(verifier, "validate_authoritative_framework_pins", return_value=[])
    def test_zip_matching_framework_pins_passes(self, _validate_pins):
        source_pins = self.root / "framework_pins.txt"
        source_pins.write_text(PINS, encoding="utf-8")
        archive = self.root / "release.zip"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr("game.toml", CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
            zip_file.writestr("framework_pins.txt", source_pins.read_bytes())
        self.assertEqual(verifier.validate_release(archive, self.root), [])

    def test_framework_pins_are_optional(self):
        self.assertFalse((self.root / "framework_pins.txt").exists())
        self.assertEqual(verifier.validate_release(self.exe, self.root), [])

    @mock.patch.object(
        verifier,
        "validate_authoritative_framework_pins",
        return_value=["framework pin mismatch for psxrecomp"],
    )
    def test_source_framework_pins_must_match_gitlinks(self, _validate_pins):
        (self.root / "framework_pins.txt").write_text(PINS, encoding="utf-8")
        (self.release / "framework_pins.txt").write_text(PINS, encoding="utf-8")
        errors = verifier.validate_release(self.exe, self.root)
        self.assertIn("framework pin mismatch for psxrecomp", errors)

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

    def test_zip_rejects_rooted_and_drive_prefixed_profiles(self):
        unsafe_names = (
            "/game.toml",
            r"\game.toml",
            ".//game.toml",
            "C:/game.toml",
            r"C:\game.toml",
        )
        for index, unsafe_name in enumerate(unsafe_names):
            with self.subTest(name=unsafe_name):
                archive = self.root / f"unsafe-root-{index}.zip"
                with ZipFile(archive, "w") as zip_file:
                    zip_file.writestr(unsafe_name, CONFIG)
                    zip_file.writestr("mods/state.toml", STATE)
                    zip_file.writestr(
                        "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                        MANIFEST,
                    )
                errors = verifier.validate_release(archive, self.root)
                self.assertTrue(any("unsafe" in error for error in errors))
                self.assertIn("missing staged game.toml", errors)

    def test_zip_rejects_parent_component_inside_member_path(self):
        archive = self.root / "unsafe-internal-traversal.zip"
        unsafe_name = "nested/../game.toml"
        with ZipFile(archive, "w") as zip_file:
            zip_file.writestr(unsafe_name, CONFIG)
            zip_file.writestr("mods/state.toml", STATE)
            zip_file.writestr(
                "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml",
                MANIFEST,
            )
        errors = verifier.validate_release(archive, self.root)
        self.assertTrue(
            any("unsafe" in error and repr(unsafe_name) in error for error in errors)
        )
        self.assertIn("missing staged game.toml", errors)

    @mock.patch.object(verifier, "validate_authoritative_framework_pins", return_value=[])
    def test_traversal_prefixed_profiles_cannot_satisfy_contract(self, _validate_pins):
        (self.root / "framework_pins.txt").write_text(PINS, encoding="utf-8")
        manifest_name = (
            "mods/packages/wipeout3.enhancement.skip-fmv/1.0.0/manifest.toml"
        )
        cases = (
            ("game.toml", "../game.toml", CONFIG),
            ("mods/state.toml", r"..\mods\state.toml", STATE),
            (manifest_name, f"../{manifest_name}", MANIFEST),
            ("framework_pins.txt", r"..\framework_pins.txt", PINS),
        )
        for index, (profile_name, unsafe_name, contents) in enumerate(cases):
            with self.subTest(profile=profile_name, name=unsafe_name):
                archive = self.root / f"unsafe-traversal-{index}.zip"
                members = {
                    "game.toml": CONFIG,
                    "mods/state.toml": STATE,
                    manifest_name: MANIFEST,
                    "framework_pins.txt": PINS,
                }
                members.pop(profile_name)
                members[unsafe_name] = contents
                with ZipFile(archive, "w") as zip_file:
                    for name, text in members.items():
                        zip_file.writestr(name, text)

                errors = verifier.validate_release(archive, self.root)
                self.assertTrue(any("unsafe" in error for error in errors))
                self.assertIn(f"missing staged {profile_name}", errors)

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
