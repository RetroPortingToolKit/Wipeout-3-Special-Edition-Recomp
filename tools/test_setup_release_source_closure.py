import re
import subprocess
from pathlib import Path

from verify_release_freshness import SETUP_REBUILD_DIRS, SETUP_REBUILD_FILES

ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "scripts/package_setup_release.sh"

REQUIRED_FILES = SETUP_REBUILD_FILES
REQUIRED_DIRS = SETUP_REBUILD_DIRS


def _literal_arguments(option: str) -> set[str]:
    text = WRAPPER.read_text(encoding="utf-8")
    return set(re.findall(rf"{re.escape(option)}\s+([^\s\\]+)", text))


def test_setup_archive_contains_title_rebuild_inputs() -> None:
    packaged_files = _literal_arguments("--project-file")
    packaged_dirs = _literal_arguments("--project-dir")

    assert REQUIRED_FILES <= packaged_files
    assert REQUIRED_DIRS <= packaged_dirs


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
