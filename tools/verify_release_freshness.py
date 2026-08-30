"""Fail-closed freshness and source-profile contract for a release artifact."""
from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

import tomllib

PROFILE_FILES = ("game.toml", "mods/state.toml")


def _parse_toml(name: str, text: str, origin: str, errors: list[str]) -> dict | None:
    try:
        value = tomllib.loads(text)
    except tomllib.TOMLDecodeError as exc:
        errors.append(f"cannot parse {origin} {name}: {exc}")
        return None
    if not isinstance(value, dict):
        errors.append(f"{origin} {name} is not a TOML table")
        return None
    return value


def _check_texts(staged: dict[str, str], source: dict[str, str]) -> list[str]:
    """Require staged defaults to be structurally identical to source defaults."""

    errors: list[str] = []
    for name in PROFILE_FILES:
        staged_text = staged.get(name)
        source_text = source.get(name)
        if staged_text is None:
            errors.append(f"missing staged {name}")
            continue
        if source_text is None:
            errors.append(f"missing source {name}")
            continue
        staged_value = _parse_toml(name, staged_text, "staged", errors)
        source_value = _parse_toml(name, source_text, "source", errors)
        if staged_value is not None and source_value is not None and staged_value != source_value:
            errors.append(f"staged {name} does not match the current source profile")
    return errors


def _read_artifact_files(artifact: Path) -> tuple[dict[str, str], list[str]]:
    files: dict[str, str] = {}
    errors: list[str] = []
    if artifact.suffix.lower() == ".zip":
        try:
            with zipfile.ZipFile(artifact) as archive:
                matches = {name: [] for name in PROFILE_FILES}
                for name in archive.namelist():
                    normalized = name.replace("\\", "/").lstrip("./")
                    for profile_name in PROFILE_FILES:
                        if normalized == profile_name or normalized.endswith(f"/{profile_name}"):
                            matches[profile_name].append(name)
                for profile_name, names in matches.items():
                    if len(names) > 1:
                        errors.append(
                            f"ZIP contains multiple candidates for {profile_name}: "
                            + ", ".join(names)
                        )
                    elif names:
                        files[profile_name] = archive.read(names[0]).decode("utf-8")
        except (OSError, zipfile.BadZipFile, UnicodeDecodeError) as exc:
            errors.append(f"cannot inspect ZIP: {exc}")
    else:
        release = artifact.parent
        for name in PROFILE_FILES:
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

    source_paths = {
        "game.toml": source_root / "game.toml",
        "mods/state.toml": source_root / "mods" / "state.toml",
    }
    preloaded = source_root / "mods" / "preloaded"
    relevant = [path for path in source_paths.values() if path.is_file()]
    if preloaded.is_dir():
        relevant.extend(path for path in preloaded.rglob("*") if path.is_file())
    if not relevant:
        errors.append("relevant source/config files are missing")
    elif artifact.stat().st_mtime < max(p.stat().st_mtime for p in relevant):
        errors.append("release artifact predates relevant source/config")

    source: dict[str, str] = {}
    for name, path in source_paths.items():
        if not path.is_file():
            continue
        try:
            source[name] = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"cannot read source {name}: {exc}")

    staged, read_errors = _read_artifact_files(artifact)
    return errors + read_errors + _check_texts(staged, source)


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
    print("PASS release freshness: current source/config profile verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
