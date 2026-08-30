"""Fail-closed freshness and source-profile contract for a release artifact."""
from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

import tomllib

BASE_PROFILE_FILES = ("game.toml", "mods/state.toml")


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


def _package_refs(state: dict, origin: str, errors: list[str]) -> dict[str, str]:
    packages = state.get("package")
    if not isinstance(packages, list) or not packages:
        errors.append(f"{origin} mods/state.toml has no package catalog")
        return {}

    refs: dict[str, str] = {}
    for index, package in enumerate(packages):
        if not isinstance(package, dict):
            errors.append(f"{origin} package entry {index} is not a TOML table")
            continue
        package_id = package.get("id")
        version = package.get("version")
        if not isinstance(package_id, str) or not package_id:
            errors.append(f"{origin} package entry {index} has no id")
            continue
        if not isinstance(version, str) or not version:
            errors.append(f"{origin} package {package_id} has no version")
            continue
        if package_id in refs:
            errors.append(f"{origin} package catalog repeats {package_id}")
            continue
        refs[package_id] = version
    return refs


def _source_profile(source_root: Path) -> tuple[dict[str, str], dict[str, str], list[str]]:
    files: dict[str, str] = {}
    errors: list[str] = []
    source_paths = {name: source_root / Path(name) for name in BASE_PROFILE_FILES}
    for name, path in source_paths.items():
        try:
            files[name] = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"cannot read source {name}: {exc}")

    state_text = files.get("mods/state.toml")
    if state_text is None:
        return files, {}, errors
    state = _parse_toml("mods/state.toml", state_text, "source", errors)
    if state is None:
        return files, {}, errors
    package_refs = _package_refs(state, "source", errors)
    for package_id, version in package_refs.items():
        relative = Path(package_id) / version / "manifest.toml"
        candidates = (
            source_root / "mods" / "preloaded" / "packages" / relative,
            source_root / "psxrecomp" / "mods" / "builtin" / "packages" / relative,
        )
        matches = [path for path in candidates if path.is_file()]
        profile_name = f"mods/packages/{relative.as_posix()}"
        if len(matches) != 1:
            errors.append(
                f"source package {package_id} {version} has {len(matches)} manifest candidates"
            )
            continue
        try:
            files[profile_name] = matches[0].read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"cannot read source {profile_name}: {exc}")
    return files, package_refs, errors


def _check_texts(staged: dict[str, str], source: dict[str, str]) -> list[str]:
    """Require staged defaults to be structurally identical to source defaults."""

    errors: list[str] = []
    for name in source:
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


def _read_artifact_files(
    artifact: Path, profile_names: tuple[str, ...]
) -> tuple[dict[str, str], set[str] | None, list[str]]:
    files: dict[str, str] = {}
    errors: list[str] = []
    staged_packages: set[str] | None = None
    if artifact.suffix.lower() == ".zip":
        try:
            with zipfile.ZipFile(artifact) as archive:
                matches = {name: [] for name in profile_names}
                staged_packages = set()
                for name in archive.namelist():
                    normalized = name.replace("\\", "/").lstrip("./")
                    parts = normalized.split("/")
                    for index in range(len(parts) - 2):
                        if parts[index : index + 2] == ["mods", "packages"]:
                            staged_packages.add(parts[index + 2])
                    for profile_name in profile_names:
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
        for name in profile_names:
            path = release / name
            if path.is_file():
                try:
                    files[name] = path.read_text(encoding="utf-8")
                except (OSError, UnicodeDecodeError) as exc:
                    errors.append(f"cannot decode staged {name}: {exc}")
        package_root = release / "mods" / "packages"
        if package_root.is_dir():
            staged_packages = {path.name for path in package_root.iterdir() if path.is_dir()}
    return files, staged_packages, errors


def validate_release(artifact: Path, source_root: Path) -> list[str]:
    """Return failures; an empty list certifies the artifact."""
    errors: list[str] = []
    if not artifact.is_file() or artifact.suffix.lower() not in {".exe", ".zip"}:
        return ["artifact must be an existing .exe or .zip"]
    if not source_root.is_dir():
        return ["source root must be an existing directory"]

    source, package_refs, source_errors = _source_profile(source_root)
    errors.extend(source_errors)

    source_paths = [source_root / "game.toml", source_root / "mods" / "state.toml"]
    preloaded = source_root / "mods" / "preloaded"
    relevant = [path for path in source_paths if path.is_file()]
    if preloaded.is_dir():
        relevant.extend(path for path in preloaded.rglob("*") if path.is_file())
    if not relevant:
        errors.append("relevant source/config files are missing")
    elif artifact.stat().st_mtime < max(p.stat().st_mtime for p in relevant):
        errors.append("release artifact predates relevant source/config")

    staged, staged_packages, read_errors = _read_artifact_files(
        artifact, tuple(source)
    )
    errors.extend(read_errors)
    errors.extend(_check_texts(staged, source))
    expected_packages = set(package_refs)
    if staged_packages is None:
        errors.append("missing staged mods/packages catalog")
    elif staged_packages != expected_packages:
        missing = sorted(expected_packages - staged_packages)
        unexpected = sorted(staged_packages - expected_packages)
        if missing:
            errors.append("staged package catalog is missing: " + ", ".join(missing))
        if unexpected:
            errors.append("staged package catalog is unexpected: " + ", ".join(unexpected))
    return errors


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
