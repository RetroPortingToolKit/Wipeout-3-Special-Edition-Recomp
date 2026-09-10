import re
import subprocess
from pathlib import Path

from verify_release_freshness import (
    SETUP_REBUILD_DIRS,
    SETUP_REBUILD_FILES,
    SETUP_REBUILD_TREES,
)

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "scripts/package_setup_release.sh"
PACKAGER = ROOT / "psxrecomp/tools/package_setup_host.sh"

REQUIRED_FILES = {
    "CMakeLists.txt",
    "README.md",
    "VERSION",
    "assets/psxrecomp.ico",
    "codegen_setup.c",
    "codegen_setup.h",
    "game.toml",
    "game_options.toml",
    "mods/state.toml",
}
REQUIRED_DIRS = {
    "bezels",
    "launcher_assets",
    "mods/preloaded",
    "seeds",
    "src/mods",
}
PROJECT_DIRS = REQUIRED_DIRS - {"bezels"}
REQUIRED_TREES = {"psxrecomp", "recomp-ui"}


def _literal_arguments(option: str) -> set[str]:
    text = WRAPPER.read_text(encoding="utf-8")
    return set(re.findall(rf"{re.escape(option)}\s+([^\s\\]+)", text))


def test_setup_archive_contains_title_rebuild_inputs() -> None:
    packaged_files = _literal_arguments("--project-file")
    packaged_dirs = _literal_arguments("--project-dir")

    assert REQUIRED_FILES <= packaged_files
    assert PROJECT_DIRS <= packaged_dirs


def test_verifier_rebuild_contract_is_independently_complete() -> None:
    assert SETUP_REBUILD_FILES == REQUIRED_FILES
    assert SETUP_REBUILD_DIRS == REQUIRED_DIRS
    assert SETUP_REBUILD_TREES == REQUIRED_TREES


def test_framework_trees_are_copied_from_clean_tracked_inputs() -> None:
    text = PACKAGER.read_text(encoding="utf-8")
    assert 'ls-files -z --recurse-submodules' in text
    assert 'diff --quiet --ignore-submodules=none' in text
    assert 'diff --cached --quiet --ignore-submodules=none' in text
    for tree in REQUIRED_TREES:
        assert f'copy_tree_filtered "${{ROOT}}/{tree}"' in text


def test_literal_rebuild_inputs_exist_and_are_tracked() -> None:
    for relative in sorted(REQUIRED_FILES | REQUIRED_DIRS):
        assert (ROOT / relative).exists(), relative
        result = subprocess.run(
            ["git", "-C", str(ROOT), "ls-files", "--error-unmatch", relative],
            check=False,
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, relative
