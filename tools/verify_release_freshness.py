"""Fail-closed freshness and source-profile contract for a release artifact."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import unicodedata
import zipfile
from pathlib import Path

import tomllib
from framework_pins import validate_authoritative_framework_pins

BASE_PROFILE_FILES = ("game.toml", "game_options.toml", "mods/state.toml")
FRAMEWORK_PINS_FILE = "framework_pins.txt"
WINDOWS_DRIVE_PREFIX = re.compile(r"^[A-Za-z]:")
WINDOWS_RESERVED_NAMES = frozenset(
    {"aux", "con", "nul", "prn"}
    | {f"com{index}" for index in range(1, 10)}
    | {f"lpt{index}" for index in range(1, 10)}
)
SETUP_REBUILD_FILES = frozenset(
    {
        "CMakeLists.txt",
        "VERSION",
        "README.md",
        "assets/psxrecomp.ico",
        "codegen_setup.c",
        "codegen_setup.h",
        "game.toml",
        "game_options.toml",
        "mods/state.toml",
    }
)
SETUP_REBUILD_DIRS = frozenset(
    {"bezels", "launcher_assets", "mods/preloaded", "seeds", "src/mods"}
)
SETUP_REBUILD_TREES = frozenset({"psxrecomp", "recomp-ui"})
OPTIONAL_SETUP_REBUILD_FILES = frozenset({"catalog_identity.json"})
SETUP_TREE_EXTRA_FILES = frozenset({"psxrecomp/retcomm-sdk.json"})
SETUP_TREE_EXTRA_PREFIXES = ("psxrecomp/recompiler/build/",)


def _normalize_zip_member(name: str, errors: list[str]) -> str | None:
    """Return a safe archive-relative member name, or record why it is unsafe."""

    normalized = name.replace("\\", "/")
    if normalized.startswith("/"):
        errors.append(f"ZIP contains unsafe absolute member path: {name!r}")
        return None

    # ZIP producers commonly spell archive-root entries with one leading `./`.
    # Preserve that compatibility, including duplicate-candidate detection, but
    # do not recursively strip path syntax.
    normalized = normalized.removeprefix("./")
    if normalized.startswith("/"):
        errors.append(f"ZIP contains unsafe absolute member path: {name!r}")
        return None

    if WINDOWS_DRIVE_PREFIX.match(normalized):
        errors.append(f"ZIP contains unsafe drive-prefixed member path: {name!r}")
        return None

    parts = normalized.split("/")
    path_parts = parts[:-1] if parts[-1:] == [""] else parts
    if not path_parts or "" in path_parts:
        errors.append(f"ZIP contains unsafe repeated path separator: {name!r}")
        return None
    if ".." in path_parts:
        errors.append(f"ZIP contains unsafe parent-traversal member path: {name!r}")
        return None
    if "." in path_parts:
        errors.append(f"ZIP contains unsafe non-canonical member path: {name!r}")
        return None
    for part in path_parts:
        if any(ord(char) < 32 for char in part):
            errors.append(f"ZIP contains unsafe control character in member path: {name!r}")
            return None
        if part.endswith((" ", ".")):
            errors.append(f"ZIP contains unsafe Windows-trimmed member path: {name!r}")
            return None
        if ":" in part:
            errors.append(f"ZIP contains unsafe colon in member path: {name!r}")
            return None
        if part.split(".", 1)[0].casefold() in WINDOWS_RESERVED_NAMES:
            errors.append(f"ZIP contains unsafe Windows device member path: {name!r}")
            return None
    return normalized


def _zip_member_map(
    archive: zipfile.ZipFile, errors: list[str]
) -> dict[str, list[str]]:
    """Collect safe names and reject extraction-equivalent collisions globally."""

    members: dict[str, list[str]] = {}
    canonical: dict[str, list[tuple[str, bool]]] = {}
    for name in archive.namelist():
        normalized = _normalize_zip_member(name, errors)
        if normalized is None:
            continue
        members.setdefault(normalized, []).append(name)
        is_directory = normalized.endswith("/")
        path = normalized.removesuffix("/")
        key = unicodedata.normalize("NFC", path).casefold()
        canonical.setdefault(key, []).append((name, is_directory))

    for entries in canonical.values():
        if len(entries) > 1:
            errors.append(
                "ZIP contains extraction-colliding members: "
                + ", ".join(name for name, _is_directory in entries)
            )

    file_keys = {
        key
        for key, entries in canonical.items()
        if any(not is_directory for _name, is_directory in entries)
    }
    conflicts: set[tuple[str, str]] = set()
    for key, entries in canonical.items():
        parts = key.split("/")
        for index in range(1, len(parts)):
            parent = "/".join(parts[:index])
            if parent in file_keys:
                conflicts.add((canonical[parent][0][0], entries[0][0]))
    for parent, child in sorted(conflicts):
        errors.append(
            f"ZIP contains file/directory prefix conflict: {parent!r}, {child!r}"
        )
    return members


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
) -> tuple[dict[str, str], set[str] | None, set[str] | None, list[str]]:
    files: dict[str, str] = {}
    errors: list[str] = []
    staged_packages: set[str] | None = None
    staged_members: set[str] | None = None
    if artifact.suffix.lower() == ".zip":
        try:
            with zipfile.ZipFile(artifact) as archive:
                matches = {name: [] for name in profile_names}
                staged_packages = set()
                staged_members = set()
                member_map = _zip_member_map(archive, errors)
                for normalized, names in member_map.items():
                    staged_members.add(normalized)
                    parts = normalized.split("/")
                    if len(parts) >= 3 and parts[:2] == ["mods", "packages"]:
                        staged_packages.add(parts[2])
                    for profile_name in profile_names:
                        if normalized == profile_name:
                            matches[profile_name].extend(names)
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
                    files[name] = path.read_bytes().decode("utf-8")
                except (OSError, UnicodeDecodeError) as exc:
                    errors.append(f"cannot decode staged {name}: {exc}")
        package_root = release / "mods" / "packages"
        if package_root.is_dir():
            staged_packages = {path.name for path in package_root.iterdir() if path.is_dir()}
    return files, staged_packages, staged_members, errors


def _git_tracked_source_paths(
    root: Path,
    pathspecs: tuple[str, ...],
    label: str,
    errors: list[str],
    *,
    recurse_submodules: bool = False,
    clean_pathspecs: tuple[str, ...] | None = None,
) -> list[Path] | None:
    """Return clean tracked paths, or None only for an intentionally vendored tree."""

    git_marker = root / ".git"
    if not (
        git_marker.is_file()
        or git_marker.is_dir()
        or git_marker.is_symlink()
    ):
        return None

    try:
        probe = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--is-inside-work-tree"],
            check=False,
            capture_output=True,
        )
    except OSError as exc:
        errors.append(f"cannot inspect Git-backed setup source {label}: {exc}")
        return []
    if probe.returncode != 0 or probe.stdout.strip() != b"true":
        errors.append(f"cannot inspect Git-backed setup source: {label}")
        return []

    if clean_pathspecs is None:
        clean_pathspecs = pathspecs
    for diff_args in (("diff", "--quiet"), ("diff", "--cached", "--quiet")):
        if not clean_pathspecs:
            break
        result = subprocess.run(
            [
                "git",
                "-C",
                str(root),
                *diff_args,
                "--ignore-submodules=none",
                "--",
                *clean_pathspecs,
            ],
            check=False,
            capture_output=True,
        )
        if result.returncode == 1:
            errors.append(f"source setup rebuild input has dirty tracked files: {label}")
        elif result.returncode != 0:
            errors.append(f"cannot inspect source setup rebuild input: {label}")

    command = ["git", "-C", str(root), "ls-files", "-z"]
    if recurse_submodules:
        command.append("--recurse-submodules")
    command.extend(("--", *pathspecs))
    result = subprocess.run(command, check=False, capture_output=True)
    if result.returncode != 0:
        errors.append(f"cannot enumerate source setup rebuild input: {label}")
        return []
    try:
        names = result.stdout.decode("utf-8").split("\0")
    except UnicodeDecodeError as exc:
        errors.append(f"cannot decode source setup rebuild input {label}: {exc}")
        return []
    return [root / Path(name) for name in names if name]


def _source_setup_rebuild_files(
    source_root: Path,
) -> tuple[dict[str, bytes], list[str]]:
    files: dict[str, bytes] = {}
    errors: list[str] = []
    required_files = set(SETUP_REBUILD_FILES)
    required_files.update(
        name
        for name in OPTIONAL_SETUP_REBUILD_FILES
        if (source_root / name).is_file()
    )
    title_pathspecs = tuple(sorted(required_files | SETUP_REBUILD_DIRS))
    title_paths = _git_tracked_source_paths(
        source_root,
        title_pathspecs,
        "title project",
        errors,
        clean_pathspecs=tuple(
            path for path in title_pathspecs if path != "VERSION"
        ),
    )
    if title_paths is None:
        title_paths = [source_root / name for name in sorted(required_files)]
        for directory in sorted(SETUP_REBUILD_DIRS):
            root = source_root / directory
            if root.is_dir():
                title_paths.extend(path for path in root.rglob("*") if path.is_file())

    tracked_title_names = {
        path.relative_to(source_root).as_posix()
        for path in title_paths
        if path.is_relative_to(source_root)
    }
    for name in sorted(required_files):
        path = source_root / name
        if not path.is_file():
            errors.append(f"source setup rebuild file is missing: {name}")
        elif name not in tracked_title_names:
            errors.append(f"source setup rebuild file is not tracked: {name}")
    for directory in sorted(SETUP_REBUILD_DIRS):
        if not (source_root / directory).is_dir():
            errors.append(f"source setup rebuild directory is missing: {directory}")

    for path in sorted(set(title_paths)):
        if not path.is_file():
            errors.append(
                "tracked source setup rebuild input is missing: "
                + path.relative_to(source_root).as_posix()
            )
            continue
        name = path.relative_to(source_root).as_posix()
        if name not in required_files and not any(
            name.startswith(f"{directory}/") for directory in SETUP_REBUILD_DIRS
        ):
            continue
        try:
            files[name] = path.read_bytes()
        except OSError as exc:
            errors.append(f"cannot read source setup rebuild file {name}: {exc}")

    for tree in sorted(SETUP_REBUILD_TREES):
        root = source_root / tree
        if not root.is_dir():
            errors.append(f"source setup rebuild tree is missing: {tree}")
            continue

        paths = _git_tracked_source_paths(
            root,
            (".",),
            tree,
            errors,
            recurse_submodules=True,
        )
        if paths is None:
            paths = []
            for path in root.rglob("*"):
                relative = path.relative_to(root)
                if any(
                    part in {".git", "__pycache__", "build"}
                    or part.startswith("build-")
                    for part in relative.parts
                ):
                    continue
                if tree == "psxrecomp" and relative.parts[:1] == ("generated",):
                    continue
                if path.is_file():
                    paths.append(path)

        for path in sorted(paths):
            if not path.is_file():
                errors.append(
                    f"tracked source setup rebuild input is missing: "
                    f"{tree}/{path.relative_to(root).as_posix()}"
                )
                continue
            name = f"{tree}/{path.relative_to(root).as_posix()}"
            try:
                files[name] = path.read_bytes()
            except OSError as exc:
                errors.append(f"cannot read source setup rebuild file {name}: {exc}")
    return files, errors


def _check_setup_rebuild_closure(
    artifact: Path, source_root: Path, staged_members: set[str] | None
) -> list[str]:
    if staged_members is None:
        return ["setup rebuild closure requires a ZIP artifact"]

    source_files, errors = _source_setup_rebuild_files(source_root)
    expected = set(source_files)
    missing_files = sorted(expected - staged_members)
    if missing_files:
        errors.append("setup archive is missing rebuild files: " + ", ".join(missing_files))
    closure_prefixes = SETUP_REBUILD_DIRS | SETUP_REBUILD_TREES
    actual_directory_files = {
        name
        for name in staged_members
        if not name.endswith("/")
        and any(name.startswith(f"{directory}/") for directory in closure_prefixes)
    }
    unexpected_files = sorted(
        name
        for name in actual_directory_files - expected - SETUP_TREE_EXTRA_FILES
        if not any(name.startswith(prefix) for prefix in SETUP_TREE_EXTRA_PREFIXES)
    )
    if unexpected_files:
        errors.append(
            "setup archive contains unexpected rebuild files: "
            + ", ".join(unexpected_files)
        )

    normalized_names: dict[str, list[str]] = {}
    try:
        with zipfile.ZipFile(artifact) as archive:
            normalized_names = _zip_member_map(archive, [])
            for name, source_bytes in source_files.items():
                candidates = normalized_names.get(name, [])
                if len(candidates) > 1:
                    errors.append(
                        f"setup archive contains multiple candidates for rebuild input "
                        f"{name}: " + ", ".join(candidates)
                    )
                elif candidates and archive.read(candidates[0]) != source_bytes:
                    errors.append(
                        f"setup archive rebuild input does not match source: {name}"
                    )
    except (OSError, zipfile.BadZipFile) as exc:
        errors.append(f"cannot inspect setup rebuild closure: {exc}")
    return errors


def validate_release(
    artifact: Path, source_root: Path, *, require_setup_closure: bool = False
) -> list[str]:
    """Return failures; an empty list certifies the artifact."""
    errors: list[str] = []
    if not artifact.is_file() or artifact.suffix.lower() not in {".exe", ".zip"}:
        return ["artifact must be an existing .exe or .zip"]
    if not source_root.is_dir():
        return ["source root must be an existing directory"]

    source, package_refs, source_errors = _source_profile(source_root)
    errors.extend(source_errors)

    source_pins: str | None = None
    source_pins_path = source_root / FRAMEWORK_PINS_FILE
    if source_pins_path.is_file():
        try:
            source_pins = source_pins_path.read_bytes().decode("utf-8")
        except (OSError, UnicodeDecodeError) as exc:
            errors.append(f"cannot read source {FRAMEWORK_PINS_FILE}: {exc}")
        else:
            errors.extend(
                validate_authoritative_framework_pins(source_pins, source_root)
            )

    source_paths = [source_root / Path(name) for name in BASE_PROFILE_FILES]
    if source_pins is not None:
        source_paths.append(source_pins_path)
    preloaded = source_root / "mods" / "preloaded"
    relevant = [path for path in source_paths if path.is_file()]
    if preloaded.is_dir():
        relevant.extend(path for path in preloaded.rglob("*") if path.is_file())
    if not relevant:
        errors.append("relevant source/config files are missing")
    elif artifact.stat().st_mtime < max(p.stat().st_mtime for p in relevant):
        errors.append("release artifact predates relevant source/config")

    artifact_files = tuple(source)
    if source_pins is not None:
        artifact_files += (FRAMEWORK_PINS_FILE,)
    staged, staged_packages, staged_members, read_errors = _read_artifact_files(
        artifact, artifact_files
    )
    errors.extend(read_errors)
    if require_setup_closure:
        errors.extend(
            _check_setup_rebuild_closure(artifact, source_root, staged_members)
        )
    errors.extend(_check_texts(staged, source))
    if source_pins is not None:
        staged_pins = staged.get(FRAMEWORK_PINS_FILE)
        if staged_pins is None:
            errors.append(f"missing staged {FRAMEWORK_PINS_FILE}")
        elif staged_pins != source_pins:
            errors.append(
                f"staged {FRAMEWORK_PINS_FILE} does not match the current "
                "source snapshot"
            )
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
    parser.add_argument(
        "--require-setup-closure",
        action="store_true",
        help="require the ZIP to contain every title rebuild input",
    )
    args = parser.parse_args(argv)
    errors = validate_release(
        args.artifact,
        args.source_root,
        require_setup_closure=args.require_setup_closure,
    )
    if errors:
        print("FAIL release freshness")
        print("\n".join(f"- {error}" for error in errors))
        return 1
    print("PASS release freshness: current source/config profile verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
