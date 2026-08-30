import re
import subprocess
import unittest
from pathlib import Path

import tomllib

ROOT = Path(__file__).resolve().parents[1]
CMAKE = ROOT / "CMakeLists.txt"
TITLE_PACKAGES = ROOT / "mods/preloaded/packages"


def _compiled_plugin_sources() -> set[Path]:
    text = CMAKE.read_text(encoding="utf-8")
    match = re.search(
        r"set\(WIPEOUT3_MOD_PLUGIN_SOURCES(?P<body>.*?)\n\)", text, re.DOTALL
    )
    if match is None:
        raise AssertionError("missing explicit WIPEOUT3_MOD_PLUGIN_SOURCES list")
    return {
        ROOT / "src/mods" / name
        for name in re.findall(r"src/mods/([^\"]+\.c)", match.group("body"))
    }


def _registered_plugin_ids(source: Path) -> set[str]:
    return set(
        re.findall(
            r"psx_mod_register_(?:activation|vblank|function_entry)_plugin\s*"
            r"\(\s*\"([^\"]+)\"",
            source.read_text(encoding="utf-8"),
        )
    )


class ModPluginContractTests(unittest.TestCase):
    def test_shipped_manifests_match_compiled_registrations(self) -> None:
        required: set[str] = set()
        for manifest_path in TITLE_PACKAGES.glob("*/*/manifest.toml"):
            manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
            package_id, version = manifest_path.parts[-3:-1]
            self.assertEqual(manifest.get("id"), package_id)
            self.assertEqual(manifest.get("version"), version)
            for plugin in manifest.get("plugin", []):
                required.add(plugin["id"])

        sources = _compiled_plugin_sources()
        registered = set().union(*(_registered_plugin_ids(path) for path in sources))
        self.assertEqual(registered, required)

    def test_compiled_sources_are_explicit_tracked_files(self) -> None:
        cmake_text = CMAKE.read_text(encoding="utf-8")
        self.assertNotRegex(cmake_text, r"file\(GLOB\s+WIPEOUT3_MOD_PLUGIN_SOURCES")
        for source in _compiled_plugin_sources():
            self.assertTrue(source.is_file(), source)
            result = subprocess.run(
                ["git", "-C", str(ROOT), "ls-files", "--error-unmatch", source],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, source)


if __name__ == "__main__":
    unittest.main()
