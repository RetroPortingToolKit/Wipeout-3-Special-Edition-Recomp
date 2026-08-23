"""Fail-closed freshness and default contract for a portable release artifact."""
from __future__ import annotations

import argparse
import os
import re
import sys
import zipfile
from pathlib import Path


REQUIRED_PACKAGES = frozenset({
    "psx.enhancement.8mb-ram",
    "psx.enhancement.fast-loading",
    "psx.enhancement.pgxp",
    "psx.enhancement.skip-fmv",
    "wipeout3.enhancement.framerate-pal100",
    "wipeout3.enhancement.widescreen",
})
REQUIRED_CONFIG = {
    "cpu_overclock": "900",
    "geometry_correction": "true",
    "perspective_texturing": "true",
    "pgxp_depth": "true",
    "pgxp_cpu_mode": "true",
    "offer_ultrawide": "true",
    "hd_textures": "true",
    "supersampling": "4",
}


def _enabled_packages(state: str) -> set[str]:
    found: set[str] = set()
    for block in re.findall(r"\[\[feature\]\](.*?)(?=\[\[feature\]\]|\Z)", state, re.S):
        package = re.search(r'^\s*package_id\s*=\s*"([^"]+)"', block, re.M)
        enabled = re.search(r"^\s*enabled\s*=\s*true\s*$", block, re.M)
        if package and enabled:
            found.add(package.group(1))
    return found


def _check_texts(files: dict[str, str]) -> list[str]:
    errors: list[str] = []
    config = files.get("game.toml")
    state = files.get("mods/state.toml")
    if config is None:
        errors.append("missing staged game.toml")
    else:
        for key, value in REQUIRED_CONFIG.items():
            if not re.search(rf"^\s*{re.escape(key)}\s*=\s*{re.escape(value)}\s*$", config, re.M):
                errors.append(f"game.toml lacks current default {key} = {value}")
    if state is None:
        errors.append("missing staged mods/state.toml")
    else:
        actual = _enabled_packages(state)
        if actual != REQUIRED_PACKAGES:
            errors.append("enabled package catalog is not exactly the required six-package catalog")
    return errors


def _read_artifact_files(artifact: Path) -> tuple[dict[str, str], list[str]]:
    files: dict[str, str] = {}
    errors: list[str] = []
    if artifact.suffix.lower() == ".zip":
        try:
            with zipfile.ZipFile(artifact) as archive:
                for name in archive.namelist():
                    normalized = name.replace("\\", "/").lstrip("./")
                    if normalized.endswith("game.toml"):
                        files["game.toml"] = archive.read(name).decode()
                    elif normalized.endswith("mods/state.toml"):
                        files["mods/state.toml"] = archive.read(name).decode()
        except (OSError, zipfile.BadZipFile, UnicodeDecodeError) as exc:
            errors.append(f"cannot inspect ZIP: {exc}")
    else:
        release = artifact.parent
        for name in ("game.toml", "mods/state.toml"):
            path = release / name
            if path.is_file():
                try:
                    files[name] = path.read_text(encoding="utf-8")
                except UnicodeDecodeError as exc:
                    errors.append(f"cannot decode staged {name}: {exc}")
    return files, errors


def validate_release(artifact: Path, source_root: Path) -> list[str]:
    """Return failures; an empty list certifies the artifact."""
    errors: list[str] = []
    if not artifact.is_file() or artifact.suffix.lower() not in {".exe", ".zip"}:
        return ["artifact must be an existing .exe or .zip"]
    if not source_root.is_dir():
        return ["source root must be an existing directory"]

    source_files = [source_root / "game.toml", source_root / "mods" / "preloaded"]
    relevant = [p for p in source_files if p.is_file()]
    if source_files[1].is_dir():
        relevant.extend(p for p in source_files[1].rglob("*") if p.is_file())
    if not relevant:
        errors.append("relevant source/config files are missing")
    elif artifact.stat().st_mtime < max(p.stat().st_mtime for p in relevant):
        errors.append("release artifact predates relevant source/config")

    files, read_errors = _read_artifact_files(artifact)
    return errors + read_errors + _check_texts(files)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args(argv)
    errors = validate_release(args.artifact, args.source_root)
    if errors:
        print("FAIL release freshness")
        print("\n".join(f"- {error}" for error in errors))
        return 1
    print("PASS release freshness: current source/config and six required defaults verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
