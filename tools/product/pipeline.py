"""Policy layer for the offline clean-AOT product build.

The provenance programs in ``tools/provenance`` own media parsing, patching,
sector cooking, ISO inventory, PS-X EXE inventory, and bounded PBP expansion.
This module deliberately calls those programs through their command-line
contracts.  It owns only product policy: exact profile matching, bounded
selection, compiler/catalog manifests, output isolation, and receipts.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unicodedata
from collections.abc import Mapping, Sequence
from pathlib import Path, PurePosixPath
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
PROVENANCE = ROOT / "tools" / "provenance"
CATALOG_ROOT = ROOT / "clean-aot" / "catalog"
DEFAULT_PROFILE = ROOT / "clean-aot" / "product" / "product_profile.json"
DEFAULT_MEDIA_MANIFEST = ROOT / "assets" / "manifests" / "wipeout3se-media.json"

PROFILE_SCHEMA = "psxrecomp.clean-aot.product-profile.v1"
REGIONS_SCHEMA = "psxrecomp.clean-aot.product-regions.v1"
ARTIFACTS_SCHEMA = "psxrecomp.clean-aot.product-artifacts.v1"
CATALOG_INPUT_SCHEMA = "psxrecomp.clean-aot.product-catalog-input.v1"
RECEIPT_SCHEMA = "psxrecomp.clean-aot.product-receipt.v1"
FINALIZE_RECEIPT_SCHEMA = "psxrecomp.clean-aot.product-finalize-receipt.v1"
COMPILER_MANIFEST_SCHEMA = "clean-aot-compiler-manifest-v1"

ADDRESS_LIMIT = 1 << 32
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")
MD5_RE = re.compile(r"^[0-9a-f]{32}$")
TOKEN_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:-]*$")
FILE_RE = re.compile(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+\S+\s*$', re.IGNORECASE)
WINDOWS_FORBIDDEN = frozenset('<>:"\\|?*')
PORTABLE_PATH_FORBIDDEN = WINDOWS_FORBIDDEN | frozenset({"$", ";", "#"})
WINDOWS_RESERVED = frozenset(
    {"con", "prn", "aux", "nul", "clock$"}
    | {f"com{index}" for index in range(1, 10)}
    | {f"lpt{index}" for index in range(1, 10)}
)
SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".cxx"})
FORBIDDEN_TOOL_MARKERS = (
    "psxrecomp",
    "duck" + "station",
    "goosestation",
    "interpreter",
    "jit",
    "capturecompiler",
    "runtimecompiler",
)


class ProductError(RuntimeError):
    """Raised for any fail-closed product setup error."""


class UnknownVariantError(ProductError):
    """Raised when a payload or executable is not in the pinned profile."""


class ToolInvocationError(ProductError):
    """Raised when a bounded provenance/compiler subprocess fails."""


def _reject_json_constant(value: str) -> Any:
    raise ProductError(f"non-finite JSON number is not allowed: {value}")


def _reject_json_float(value: str) -> Any:
    raise ProductError(f"floating-point JSON number is not allowed: {value}")


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProductError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def load_json(path: Path, *, max_bytes: int = 16 * 1024 * 1024) -> Any:
    if not path.is_file() or path.is_symlink():
        raise ProductError(f"missing or indirect JSON input: {path.name}")
    size = path.stat().st_size
    if size > max_bytes:
        raise ProductError(f"JSON input exceeds the bounded size: {path.name}")
    try:
        text = path.read_text(encoding="utf-8")
        return json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_json_constant,
            parse_float=_reject_json_float,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProductError(f"cannot read strict JSON {path.name}: {error}") from error


def _canonicalize(value: Any, path: str = "$") -> Any:
    if value is None or isinstance(value, bool):
        return value
    if type(value) is int:
        if not -(1 << 53) < value < (1 << 53):
            raise ProductError(f"integer outside interoperable JSON range at {path}")
        return value
    if isinstance(value, str):
        try:
            value.encode("utf-8", errors="strict")
        except UnicodeEncodeError as error:
            raise ProductError(f"invalid Unicode scalar at {path}") from error
        return value
    if isinstance(value, list):
        return [
            _canonicalize(item, f"{path}[{index}]") for index, item in enumerate(value)
        ]
    if isinstance(value, dict):
        if any(not isinstance(key, str) for key in value):
            raise ProductError(f"non-string object key at {path}")
        return {
            key: _canonicalize(value[key], f"{path}.{key}") for key in sorted(value)
        }
    raise ProductError(f"unsupported JSON value at {path}: {type(value).__name__}")


def canonical_json_bytes(value: Any) -> bytes:
    normalized = _canonicalize(value)
    try:
        return json.dumps(
            normalized,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=False,
        ).encode("utf-8")
    except (TypeError, UnicodeError, ValueError) as error:
        raise ProductError(f"cannot serialize canonical JSON: {error}") from error


def canonical_json_text(value: Any) -> str:
    return canonical_json_bytes(value).decode("utf-8") + "\n"


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path, *, max_bytes: int | None = None) -> tuple[str, int]:
    digest = hashlib.sha256()
    total = 0
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                total += len(chunk)
                if max_bytes is not None and total > max_bytes:
                    raise ProductError(f"file exceeds bounded size: {path.name}")
                digest.update(chunk)
    except OSError as error:
        raise ProductError(f"cannot hash file {path.name}: {error}") from error
    return digest.hexdigest(), total


def _strict_keys(value: Any, expected: set[str], path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ProductError(f"{path} must be an object")
    actual = set(value)
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    if missing:
        raise ProductError(f"{path} missing keys: {','.join(missing)}")
    if extra:
        raise ProductError(f"{path} unknown keys: {','.join(extra)}")
    return value


def _text(value: Any, path: str, *, token: bool = False) -> str:
    if not isinstance(value, str) or not value or value.strip() != value:
        raise ProductError(f"{path} must be a non-empty trimmed string")
    if any(ord(character) < 0x20 for character in value):
        raise ProductError(f"{path} contains a control character")
    if token and TOKEN_RE.fullmatch(value) is None:
        raise ProductError(f"{path} is not a safe identifier")
    return value


def _digest(value: Any, path: str) -> str:
    result = _text(value, path)
    if SHA256_RE.fullmatch(result) is None:
        raise ProductError(f"{path} must be lower-case SHA-256 hex")
    return result


def _integer(value: Any, path: str, *, minimum: int | None = None) -> int:
    if type(value) is not int:
        raise ProductError(f"{path} must be an integer")
    if not -(1 << 53) < value < (1 << 53):
        raise ProductError(f"{path} is outside the interoperable JSON range")
    if minimum is not None and value < minimum:
        raise ProductError(f"{path} must be >= {minimum}")
    return value


def _safe_base_name(value: Any, path: str) -> str:
    result = _safe_relative_path(value, path)
    if "/" in result or result.startswith((".", "~")):
        raise ProductError(f"{path} must be a portable base name")
    return result


def _safe_relative_path(value: Any, path: str) -> str:
    result = _text(value, path)
    if unicodedata.normalize("NFC", result) != result:
        raise ProductError(f"{path} must use Unicode NFC")
    if result.startswith(("/", "\\")) or ":" in result or "\\" in result:
        raise ProductError(f"{path} must be a relative POSIX path")
    parts = result.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise ProductError(f"{path} contains traversal or empty components")
    for part in parts:
        if part != part.strip() or part.endswith((".", " ")):
            raise ProductError(f"{path} contains a non-portable component")
        if any(character in PORTABLE_PATH_FORBIDDEN for character in part):
            raise ProductError(f"{path} contains a reserved path character")
        if part.split(".", 1)[0].casefold() in WINDOWS_RESERVED:
            raise ProductError(f"{path} contains a reserved device name")
        if any(
            unicodedata.category(character) in {"Cc", "Cf", "Cs"} for character in part
        ):
            raise ProductError(f"{path} contains a control/formatting character")
    return result


def _safe_logical_path(value: Any, path: str) -> str:
    result = _text(value, path)
    if not result.startswith("/") or result.startswith("//"):
        raise ProductError(f"{path} must be an absolute ISO logical path")
    _safe_relative_path(result[1:], path)
    return result


def _address_range(value: Any, path: str) -> dict[str, int]:
    fields = _strict_keys(value, {"start", "end_exclusive"}, path)
    start = _integer(fields["start"], f"{path}.start", minimum=0)
    end = _integer(fields["end_exclusive"], f"{path}.end_exclusive", minimum=1)
    if not 0 <= start < end <= ADDRESS_LIMIT or start % 4 or end % 4:
        raise ProductError(f"{path} must be a non-empty 4-byte aligned 32-bit range")
    return {"start": start, "end_exclusive": end}


def _range_length(value: Mapping[str, int]) -> int:
    return int(value["end_exclusive"]) - int(value["start"])


def _range_list(value: Any, path: str) -> list[dict[str, int]]:
    if not isinstance(value, list) or not value:
        raise ProductError(f"{path} must be a non-empty range array")
    result = [
        _address_range(item, f"{path}[{index}]") for index, item in enumerate(value)
    ]
    previous_end = -1
    for item in result:
        if item["start"] < previous_end:
            raise ProductError(f"{path} must be sorted and non-overlapping")
        if item["start"] == previous_end:
            raise ProductError(f"{path} must be coalesced")
        previous_end = item["end_exclusive"]
    return result


def _is_link_or_junction(path: Path) -> bool:
    if path.is_symlink():
        return True
    is_junction = getattr(path, "is_junction", None)
    return bool(is_junction is not None and is_junction())


def _require_regular_file(
    path: Path, label: str, *, max_bytes: int | None = None
) -> Path:
    try:
        _reject_link_ancestors(path)
        if _is_link_or_junction(path) or not path.is_file():
            raise ProductError(f"{label} must be a regular non-symlink file")
        resolved = path.resolve(strict=True)
        if _is_link_or_junction(resolved) or not resolved.is_file():
            raise ProductError(f"{label} resolves to an indirect/non-file path")
        if max_bytes is not None and resolved.stat().st_size > max_bytes:
            raise ProductError(f"{label} exceeds its bounded size")
        return resolved
    except OSError as error:
        raise ProductError(f"cannot inspect {label}: {error}") from error


def _reject_link_ancestors(path: Path) -> None:
    current = path
    while True:
        if _is_link_or_junction(current):
            raise ProductError(f"path contains a symlink/junction: {current.name}")
        if current.parent == current:
            return
        current = current.parent


def _assert_external_output(path: Path, *, must_exist: bool = False) -> Path:
    _reject_link_ancestors(path)
    candidate = path.resolve(strict=False)
    repo = ROOT.resolve()
    try:
        candidate.relative_to(repo)
    except ValueError:
        pass
    else:
        raise ProductError("product output must be outside the repository")
    _reject_link_ancestors(candidate.parent)
    if candidate.exists() and _is_link_or_junction(candidate):
        raise ProductError("product output cannot be a symlink/junction")
    if must_exist and (not candidate.exists() or not candidate.is_dir()):
        raise ProductError("existing product output directory is unavailable")
    if not must_exist and candidate.exists():
        raise ProductError(
            f"refusing to overwrite existing product output: {candidate}"
        )
    return candidate


def _write_atomic_new(path: Path, payload: bytes) -> None:
    _reject_link_ancestors(path)
    if path.exists():
        raise ProductError(f"refusing to overwrite output: {path.name}")
    path.parent.mkdir(parents=True, exist_ok=True)
    _reject_link_ancestors(path.parent)
    temporary = path.with_name(path.name + ".partial")
    if temporary.exists():
        raise ProductError(f"stale partial output exists: {temporary.name}")
    try:
        with temporary.open("xb") as stream:
            stream.write(payload)
        temporary.replace(path)
    except OSError as error:
        temporary.unlink(missing_ok=True)
        raise ProductError(
            f"cannot publish atomic output {path.name}: {error}"
        ) from error


def _safe_command_marker(path: Path) -> None:
    lowered = str(path).casefold()
    if any(marker in lowered for marker in FORBIDDEN_TOOL_MARKERS):
        raise ProductError("compiler/provenance command names a forbidden runtime tool")


def _run_process(
    label: str,
    argv: Sequence[str],
    *,
    cwd: Path,
    max_output_bytes: int,
    timeout_seconds: int = 300,
) -> dict[str, Any]:
    if not argv:
        raise ProductError(f"{label} command is empty")
    env = os.environ.copy()
    env.update({"PYTHONHASHSEED": "0", "PYTHONUTF8": "1", "TZ": "UTC"})
    with tempfile.TemporaryDirectory(prefix="product-tool-", dir=str(cwd)) as logs:
        stdout_path = Path(logs) / "stdout"
        stderr_path = Path(logs) / "stderr"
        try:
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                completed = subprocess.run(
                    list(argv),
                    cwd=str(cwd),
                    env=env,
                    stdin=subprocess.DEVNULL,
                    stdout=stdout,
                    stderr=stderr,
                    check=False,
                    timeout=timeout_seconds,
                )
        except (OSError, subprocess.SubprocessError) as error:
            raise ToolInvocationError(f"{label} could not run: {error}") from error
        stdout_size = stdout_path.stat().st_size
        stderr_size = stderr_path.stat().st_size
        if stdout_size > max_output_bytes or stderr_size > max_output_bytes:
            raise ToolInvocationError(f"{label} exceeded its bounded output size")
        stdout_bytes = stdout_path.read_bytes()
        stderr_bytes = stderr_path.read_bytes()
    receipt = {
        "tool": label,
        "returncode": completed.returncode,
        "stdout_bytes": stdout_size,
        "stdout_sha256": sha256_bytes(stdout_bytes),
        "stderr_bytes": stderr_size,
        "stderr_sha256": sha256_bytes(stderr_bytes),
    }
    if completed.returncode != 0:
        detail = stderr_bytes.decode("utf-8", "replace").strip()
        if len(detail) > 2000:
            detail = detail[:2000]
        raise ToolInvocationError(f"{label} failed ({completed.returncode}): {detail}")
    return receipt


def _provenance_command(script: str, *arguments: str) -> list[str]:
    return [sys.executable, str(PROVENANCE / script), *arguments]


def _load_profile(path: Path) -> tuple[dict[str, Any], str]:
    document = load_json(path)
    fields = _strict_keys(
        document,
        {
            "schema",
            "game",
            "bios",
            "hueponik_patch",
            "patched_track_01",
            "main_executable",
            "catalog",
            "runtime_variants",
            "limits",
        },
        "profile",
    )
    if fields["schema"] != PROFILE_SCHEMA:
        raise ProductError("unsupported product profile schema")
    game = _strict_keys(fields["game"], {"id", "title", "cue_tracks"}, "profile.game")
    _text(game["id"], "profile.game.id", token=True)
    _text(game["title"], "profile.game.title")
    _integer(game["cue_tracks"], "profile.game.cue_tracks", minimum=1)
    bios = _strict_keys(fields["bios"], {"id", "size", "sha256"}, "profile.bios")
    _text(bios["id"], "profile.bios.id", token=True)
    _integer(bios["size"], "profile.bios.size", minimum=1)
    _digest(bios["sha256"], "profile.bios.sha256")
    patch = _strict_keys(
        fields["hueponik_patch"],
        {"repository", "branch", "commit", "filename", "size", "sha256"},
        "profile.hueponik_patch",
    )
    _text(patch["repository"], "profile.hueponik_patch.repository")
    _text(patch["branch"], "profile.hueponik_patch.branch", token=True)
    commit = _text(patch["commit"], "profile.hueponik_patch.commit")
    if COMMIT_RE.fullmatch(commit) is None:
        raise ProductError("profile.hueponik_patch.commit must be lower-case Git SHA-1")
    _safe_base_name(patch["filename"], "profile.hueponik_patch.filename")
    _integer(patch["size"], "profile.hueponik_patch.size", minimum=1)
    _digest(patch["sha256"], "profile.hueponik_patch.sha256")
    track = _strict_keys(
        fields["patched_track_01"], {"size", "sha256"}, "profile.patched_track_01"
    )
    _integer(track["size"], "profile.patched_track_01.size", minimum=1)
    _digest(track["sha256"], "profile.patched_track_01.sha256")

    executable = _strict_keys(
        fields["main_executable"],
        {
            "logical_path",
            "file_size",
            "file_sha256",
            "entry_pc",
            "load_range",
            "text_sha256",
        },
        "profile.main_executable",
    )
    logical_path = _safe_logical_path(
        executable["logical_path"], "profile.main_executable.logical_path"
    )
    if logical_path != "/SCES_028.45":
        raise ProductError("profile main executable path must be /SCES_028.45")
    _integer(executable["file_size"], "profile.main_executable.file_size", minimum=2048)
    _digest(executable["file_sha256"], "profile.main_executable.file_sha256")
    entry_pc = _integer(
        executable["entry_pc"], "profile.main_executable.entry_pc", minimum=0
    )
    executable_range = _address_range(
        executable["load_range"], "profile.main_executable.load_range"
    )
    if not executable_range["start"] <= entry_pc < executable_range["end_exclusive"]:
        raise ProductError("profile main entry is outside its text range")
    _digest(executable["text_sha256"], "profile.main_executable.text_sha256")
    if executable["file_size"] != 2048 + _range_length(executable_range):
        raise ProductError("profile PS-X EXE file size does not match its text range")

    catalog = _strict_keys(
        fields["catalog"], {"id", "page_size", "stack_window"}, "profile.catalog"
    )
    _text(catalog["id"], "profile.catalog.id", token=True)
    page_size = _integer(catalog["page_size"], "profile.catalog.page_size", minimum=4)
    if page_size & (page_size - 1):
        raise ProductError("profile catalog page_size must be a power of two")
    _address_range(catalog["stack_window"], "profile.catalog.stack_window")
    limits = _strict_keys(
        fields["limits"],
        {
            "max_cue_bytes",
            "max_raw_track_bytes",
            "max_cooked_image_bytes",
            "max_iso_files",
            "max_extracted_bytes",
            "max_pbp_bytes",
            "max_pbp_entries",
            "max_runtime_variants",
            "max_generated_files",
            "max_generated_bytes",
            "max_generated_file_bytes",
            "max_compiler_output_bytes",
        },
        "profile.limits",
    )
    for key, value in limits.items():
        _integer(value, f"profile.limits.{key}", minimum=1)

    variants = fields["runtime_variants"]
    if not isinstance(variants, list) or len(variants) > limits["max_runtime_variants"]:
        raise ProductError("profile.runtime_variants is not a bounded array")
    previous_id = ""
    for index, raw_variant in enumerate(variants):
        variant = _strict_keys(
            raw_variant,
            {
                "id",
                "kind",
                "logical_path",
                "container_size",
                "container_sha256",
                "entry_index",
                "stored_size",
                "stored_sha256",
                "expanded_size",
                "expanded_sha256",
                "load_range",
                "relocation_metadata",
            },
            f"profile.runtime_variants[{index}]",
        )
        variant_id = _text(
            variant["id"], f"profile.runtime_variants[{index}].id", token=True
        )
        if variant_id <= previous_id:
            raise ProductError("profile.runtime_variants must be sorted by id")
        previous_id = variant_id
        if variant["kind"] != "pbp-expanded-payload":
            raise ProductError("profile runtime variant kind is unsupported")
        _safe_logical_path(
            variant["logical_path"], f"profile.runtime_variants[{index}].logical_path"
        )
        _integer(
            variant["container_size"],
            f"profile.runtime_variants[{index}].container_size",
            minimum=1,
        )
        _digest(
            variant["container_sha256"],
            f"profile.runtime_variants[{index}].container_sha256",
        )
        entry_index = _integer(
            variant["entry_index"],
            f"profile.runtime_variants[{index}].entry_index",
            minimum=0,
        )
        if entry_index >= limits["max_pbp_entries"]:
            raise ProductError("profile runtime entry exceeds the PBP entry bound")
        _integer(
            variant["stored_size"],
            f"profile.runtime_variants[{index}].stored_size",
            minimum=1,
        )
        _digest(
            variant["stored_sha256"], f"profile.runtime_variants[{index}].stored_sha256"
        )
        _integer(
            variant["expanded_size"],
            f"profile.runtime_variants[{index}].expanded_size",
            minimum=4,
        )
        _digest(
            variant["expanded_sha256"],
            f"profile.runtime_variants[{index}].expanded_sha256",
        )
        runtime_range = _address_range(
            variant["load_range"], f"profile.runtime_variants[{index}].load_range"
        )
        if _range_length(runtime_range) != variant["expanded_size"]:
            raise ProductError(
                "runtime variant range length does not match expanded size"
            )
        if variant["relocation_metadata"] is not None and not isinstance(
            variant["relocation_metadata"], dict
        ):
            raise ProductError("runtime relocation_metadata must be an object or null")
    return fields, sha256_bytes(canonical_json_bytes(fields))


def _profile_variant_map(profile: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    main = dict(profile["main_executable"])
    main["id"] = "main-exe"
    main["kind"] = "ps-x-exe"
    main["load_range"] = dict(main["load_range"])
    main["sha256"] = main["text_sha256"]
    main["image_sha256"] = main["file_sha256"]
    result = {"main-exe": main}
    for variant in profile["runtime_variants"]:
        item = dict(variant)
        item["sha256"] = item["expanded_sha256"]
        item["image_sha256"] = item["expanded_sha256"]
        result[item["id"]] = item
    return result


def _validate_regions(document: Any, profile: Mapping[str, Any]) -> dict[str, Any]:
    fields = _strict_keys(
        document,
        {"schema", "catalog_id", "page_size", "stack_window", "variants"},
        "regions",
    )
    if fields["schema"] != REGIONS_SCHEMA:
        raise ProductError("unsupported product region schema")
    if fields["catalog_id"] != profile["catalog"]["id"]:
        raise ProductError("region catalog_id does not match the product profile")
    if fields["page_size"] != profile["catalog"]["page_size"]:
        raise ProductError("region page_size does not match the product profile")
    expected_stack = _address_range(
        profile["catalog"]["stack_window"], "profile.catalog.stack_window"
    )
    if _address_range(fields["stack_window"], "regions.stack_window") != expected_stack:
        raise ProductError("region stack_window does not match the product profile")
    variants = fields["variants"]
    if not isinstance(variants, list) or not variants:
        raise ProductError(
            "regions.variants must contain at least one explicit variant"
        )
    known = _profile_variant_map(profile)
    result_ids: set[str] = set()
    normalized_variants: list[dict[str, Any]] = []
    for index, raw in enumerate(variants):
        item = _strict_keys(
            raw,
            {
                "id",
                "compile",
                "entry_points",
                "code_ranges",
                "data_ranges",
                "external_targets",
                "indirect_target_sets",
                "delay_slot_owners",
                "maximum_instructions_per_shard",
            },
            f"regions.variants[{index}]",
        )
        variant_id = _text(item["id"], f"regions.variants[{index}].id", token=True)
        if variant_id in result_ids or variant_id not in known:
            raise UnknownVariantError(
                f"region manifest names an unknown variant: {variant_id}"
            )
        result_ids.add(variant_id)
        if type(item["compile"]) is not bool:
            raise ProductError(f"regions.variants[{index}].compile must be boolean")
        image = known[variant_id]
        image_range = _address_range(
            image["load_range"], f"profile variant {variant_id}.load_range"
        )
        code_ranges = _range_list(
            item["code_ranges"], f"regions.variants[{index}].code_ranges"
        )
        data_ranges = item["data_ranges"]
        if not isinstance(data_ranges, list):
            raise ProductError(
                f"regions.variants[{index}].data_ranges must be an array"
            )
        normalized_data = [
            _address_range(value, f"regions.variants[{index}].data_ranges[{n}]")
            for n, value in enumerate(data_ranges)
        ]
        previous_end = -1
        for data_range in normalized_data:
            if (
                data_range["start"] < previous_end
                or data_range["start"] == previous_end
            ):
                raise ProductError(
                    "data ranges must be sorted, disjoint, and coalesced"
                )
            previous_end = data_range["end_exclusive"]
        for candidate in [*code_ranges, *normalized_data]:
            if (
                not image_range["start"]
                <= candidate["start"]
                < candidate["end_exclusive"]
                <= image_range["end_exclusive"]
            ):
                raise ProductError(f"region {variant_id} range lies outside its image")
        for code_range in code_ranges:
            if any(
                code_range["start"] < data_range["end_exclusive"]
                and data_range["start"] < code_range["end_exclusive"]
                for data_range in normalized_data
            ):
                raise ProductError(f"region {variant_id} code/data ranges overlap")
        entries = item["entry_points"]
        if not isinstance(entries, list) or not entries:
            raise ProductError(f"region {variant_id} needs at least one entry point")
        normalized_entries = sorted(
            {
                _integer(value, f"regions.variants[{index}].entry_points", minimum=0)
                for value in entries
            }
        )
        if any(
            value % 4
            or not any(r["start"] <= value < r["end_exclusive"] for r in code_ranges)
            for value in normalized_entries
        ):
            raise ProductError(
                f"region {variant_id} has an entry outside executable ranges"
            )
        if (
            variant_id == "main-exe"
            and profile["main_executable"]["entry_pc"] not in normalized_entries
        ):
            raise ProductError("main-exe regions must include the pinned PS-X entry PC")
        external = item["external_targets"]
        if not isinstance(external, list):
            raise ProductError(f"region {variant_id}.external_targets must be an array")
        normalized_external = sorted(
            {
                _integer(
                    value, f"regions.variants[{index}].external_targets", minimum=0
                )
                for value in external
            }
        )
        if any(value >= ADDRESS_LIMIT or value % 4 for value in normalized_external):
            raise ProductError(f"region {variant_id} has an unaligned external target")
        if any(
            any(
                r["start"] <= value < r["end_exclusive"]
                for r in [*code_ranges, *normalized_data]
            )
            for value in normalized_external
        ):
            raise ProductError(
                f"region {variant_id} external target collides with an image range"
            )
        indirect = item["indirect_target_sets"]
        if not isinstance(indirect, list):
            raise ProductError(
                f"region {variant_id}.indirect_target_sets must be an array"
            )
        normalized_indirect: list[dict[str, Any]] = []
        previous_site = -1
        for set_index, raw_set in enumerate(indirect):
            set_fields = _strict_keys(
                raw_set,
                {"site", "targets"},
                f"regions.variants[{index}].indirect_target_sets[{set_index}]",
            )
            site = _integer(set_fields["site"], "indirect.site", minimum=0)
            targets = set_fields["targets"]
            if site <= previous_site or site % 4:
                raise ProductError("indirect target sites must be sorted and aligned")
            previous_site = site
            if not isinstance(targets, list) or not targets:
                raise ProductError("indirect target set must not be empty")
            normalized_targets = sorted(
                {_integer(value, "indirect.target", minimum=0) for value in targets}
            )
            if not any(r["start"] <= site < r["end_exclusive"] for r in code_ranges):
                raise ProductError("indirect target site is outside executable ranges")
            if any(
                value % 4
                or (
                    not any(
                        r["start"] <= value < r["end_exclusive"] for r in code_ranges
                    )
                    and value not in normalized_external
                )
                for value in normalized_targets
            ):
                raise ProductError(
                    "indirect target is not authorized by code/external ranges"
                )
            normalized_indirect.append({"site": site, "targets": normalized_targets})
        delay = item["delay_slot_owners"]
        if not isinstance(delay, list):
            raise ProductError("delay_slot_owners must be an array")
        normalized_delay: list[dict[str, int]] = []
        previous_branch = -1
        for delay_index, raw_delay in enumerate(delay):
            delay_fields = _strict_keys(
                raw_delay,
                {"branch_pc", "delay_slot_pc", "owner_root"},
                f"delay_slot_owners[{delay_index}]",
            )
            branch = _integer(delay_fields["branch_pc"], "delay.branch_pc", minimum=0)
            slot = _integer(
                delay_fields["delay_slot_pc"], "delay.delay_slot_pc", minimum=0
            )
            owner = _integer(delay_fields["owner_root"], "delay.owner_root", minimum=0)
            if (
                branch <= previous_branch
                or slot != branch + 4
                or branch % 4
                or slot % 4
            ):
                raise ProductError("delay-slot ownership must be sorted and adjacent")
            previous_branch = branch
            if owner not in normalized_entries:
                raise ProductError("delay-slot owner_root is not a region entry")
            if not any(
                r["start"] <= branch and slot < r["end_exclusive"] for r in code_ranges
            ):
                raise ProductError("delay-slot ownership lies outside code")
            normalized_delay.append(
                {"branch_pc": branch, "delay_slot_pc": slot, "owner_root": owner}
            )
        shard = _integer(
            item["maximum_instructions_per_shard"],
            "maximum_instructions_per_shard",
            minimum=64,
        )
        if shard > 65536:
            raise ProductError("maximum_instructions_per_shard exceeds compiler bound")
        normalized_variants.append(
            {
                "id": variant_id,
                "compile": item["compile"],
                "entry_points": normalized_entries,
                "code_ranges": code_ranges,
                "data_ranges": normalized_data,
                "external_targets": normalized_external,
                "indirect_target_sets": normalized_indirect,
                "delay_slot_owners": normalized_delay,
                "maximum_instructions_per_shard": shard,
            }
        )
    if "main-exe" not in result_ids:
        raise ProductError("regions must authorize the main-exe variant")
    normalized_variants.sort(key=lambda item: item["id"])
    return {
        "schema": REGIONS_SCHEMA,
        "catalog_id": fields["catalog_id"],
        "page_size": fields["page_size"],
        "stack_window": expected_stack,
        "variants": normalized_variants,
    }


def _parse_cue_references(cue: Path, *, max_bytes: int) -> list[Path]:
    cue = _require_regular_file(cue, "CUE", max_bytes=max_bytes)
    try:
        lines = cue.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeError) as error:
        raise ProductError(f"cannot read CUE: {error}") from error
    references: list[Path] = []
    for line_number, line in enumerate(lines, 1):
        match = FILE_RE.match(line)
        if not match:
            continue
        raw = match.group(1) or match.group(2)
        if raw is None:
            raise ProductError(f"CUE FILE has no path at line {line_number}")
        normalized = raw.replace("\\", "/")
        relative = PurePosixPath(normalized)
        if (
            relative.is_absolute()
            or ":" in normalized
            or any(part in {"", ".", ".."} for part in relative.parts)
        ):
            raise ProductError(f"CUE FILE path is unsafe at line {line_number}")
        candidate = cue.parent.joinpath(*relative.parts)
        references.append(
            _require_regular_file(
                candidate, f"CUE track at line {line_number}", max_bytes=268435456
            )
        )
    if not references or len(references) > 64:
        raise ProductError("CUE has an invalid/bounded FILE count")
    return references


def _preflight_inputs(
    *,
    cue: Path,
    bios: Path,
    patch: Path,
    xdelta3: Path,
    compiler: Path | None,
    profile: Mapping[str, Any],
) -> dict[str, Path]:
    limits = profile["limits"]
    cue_path = _require_regular_file(cue, "CUE", max_bytes=limits["max_cue_bytes"])
    references = _parse_cue_references(cue_path, max_bytes=limits["max_cue_bytes"])
    bios_path = _require_regular_file(bios, "BIOS", max_bytes=profile["bios"]["size"])
    patch_path = _require_regular_file(
        patch, "hueponik patch", max_bytes=max(profile["hueponik_patch"]["size"], 1)
    )
    if compiler is not None:
        compiler_path = _require_regular_file(
            compiler, "clean-AOT compiler", max_bytes=limits["max_generated_file_bytes"]
        )
        _safe_command_marker(compiler_path)
        if "clean_aot_compiler" not in compiler_path.name.casefold():
            raise ProductError(
                "compiler executable must be named as a clean_aot_compiler tool"
            )
    else:
        compiler_path = Path()
    return {
        "cue": cue_path,
        "bios": bios_path,
        "patch": patch_path,
        "xdelta3": _require_regular_file(
            xdelta3, "xdelta3", max_bytes=64 * 1024 * 1024
        ),
        "compiler": compiler_path,
        "track_reference": references[0],
    }


def _read_receipt(path: Path) -> dict[str, Any]:
    value = load_json(path, max_bytes=32 * 1024 * 1024)
    if not isinstance(value, dict):
        raise ProductError(f"tool receipt is not an object: {path.name}")
    return value


def _path_from_logical(root: Path, logical_path: str) -> Path:
    relative = PurePosixPath(logical_path.lstrip("/"))
    if not relative.parts or any(part in {"", ".", ".."} for part in relative.parts):
        raise ProductError("inventory returned an unsafe logical path")
    candidate = root.joinpath(*relative.parts)
    resolved_root = root.resolve(strict=True)
    resolved = candidate.resolve(strict=True)
    try:
        resolved.relative_to(resolved_root)
    except ValueError as error:
        raise ProductError("inventory extraction escaped its root") from error
    return _require_regular_file(
        resolved, "extracted inventory file", max_bytes=268435456
    )


def _run_media_pipeline(
    *,
    paths: Mapping[str, Path],
    profile: Mapping[str, Any],
    media_manifest: Path,
    work: Path,
) -> dict[str, Any]:
    limits = profile["limits"]
    prepared = work / "prepared-media"
    prepare_receipt = work / "prepare.json"
    run_receipts: list[dict[str, Any]] = []
    run_receipts.append(
        _run_process(
            "prepare-user-media",
            _provenance_command(
                "prepare_user_media.py",
                "--cue",
                str(paths["cue"]),
                "--output",
                str(prepared),
                "--manifest",
                str(media_manifest),
                "--patch",
                str(paths["patch"]),
                "--xdelta3",
                str(paths["xdelta3"]),
                "--bios",
                str(paths["bios"]),
                "--json-out",
                str(prepare_receipt),
            ),
            cwd=work,
            max_output_bytes=limits["max_compiler_output_bytes"],
        )
    )
    prepared_report = _read_receipt(prepare_receipt)
    if prepared_report.get("game_id") != profile["game"]["id"]:
        raise ProductError(
            "prepared media game identity does not match product profile"
        )
    if prepared_report.get("track_01", {}).get("identity") != "hueponik_pal":
        raise ProductError("prepared Track 01 is not the pinned hueponik PAL target")
    track_report = prepared_report.get("track_01", {})
    if (
        track_report.get("size") != profile["patched_track_01"]["size"]
        or track_report.get("hashes", {}).get("sha256")
        != profile["patched_track_01"]["sha256"]
    ):
        raise ProductError(
            "prepared Track 01 identity differs from the product profile"
        )
    bios_report = prepared_report.get("bios", {})
    if (
        bios_report.get("size") != profile["bios"]["size"]
        or bios_report.get("hashes", {}).get("sha256") != profile["bios"]["sha256"]
    ):
        raise ProductError("prepared BIOS identity differs from the product profile")
    patch_report = prepared_report.get("patch", {})
    if (
        patch_report.get("hashes", {}).get("sha256")
        != profile["hueponik_patch"]["sha256"]
    ):
        raise ProductError("hueponik patch identity differs from the product profile")

    cue_name = paths["cue"].name
    staged_cue = _require_regular_file(
        prepared / cue_name, "prepared CUE", max_bytes=limits["max_cue_bytes"]
    )
    staged_refs = _parse_cue_references(staged_cue, max_bytes=limits["max_cue_bytes"])
    if len(staged_refs) != profile["game"]["cue_tracks"]:
        raise ProductError("prepared CUE track count differs from the product profile")
    staged_track = _require_regular_file(
        staged_refs[0], "prepared Track 01", max_bytes=limits["max_raw_track_bytes"]
    )

    cooked = work / "track01.iso"
    cook_receipt = work / "cook.json"
    run_receipts.append(
        _run_process(
            "cook-raw-track",
            _provenance_command(
                "cook_raw_track.py",
                "--input",
                str(staged_track),
                "--output",
                str(cooked),
                "--json-out",
                str(cook_receipt),
            ),
            cwd=work,
            max_output_bytes=limits["max_compiler_output_bytes"],
        )
    )
    if cooked.stat().st_size > limits["max_cooked_image_bytes"]:
        raise ProductError("cooked ISO exceeds the product bound")

    extracted = work / "iso-files"
    iso_receipt = work / "iso.json"
    run_receipts.append(
        _run_process(
            "inventory-iso9660",
            _provenance_command(
                "inventory_iso9660.py",
                "--iso",
                str(cooked),
                "--extract-root",
                str(extracted),
                "--extract-suffix",
                ".45",
                "--extract-suffix",
                ".PB",
                "--json-out",
                str(iso_receipt),
            ),
            cwd=work,
            max_output_bytes=limits["max_compiler_output_bytes"],
        )
    )
    iso_report = _read_receipt(iso_receipt)
    if (
        iso_report.get("file_count", limits["max_iso_files"] + 1)
        > limits["max_iso_files"]
    ):
        raise ProductError("ISO inventory contains too many files")
    extraction = iso_report.get("extraction", {})
    if (
        extraction.get("file_count", limits["max_iso_files"] + 1)
        > limits["max_iso_files"]
    ):
        raise ProductError("ISO extraction contains too many files")
    total_extracted = 0
    for file_info in extraction.get("files", []):
        total_extracted += int(
            file_info.get("bytes", limits["max_extracted_bytes"] + 1)
        )
    if total_extracted > limits["max_extracted_bytes"]:
        raise ProductError("ISO extraction exceeds the total byte bound")

    exe_paths = [
        item.get("logical_path")
        for item in iso_report.get("files", [])
        if item.get("signature") == "ps-x-exe"
    ]
    expected_exe_path = profile["main_executable"]["logical_path"]
    if exe_paths != [expected_exe_path]:
        raise UnknownVariantError(
            "ISO does not contain exactly the profile-listed PS-X EXE"
        )
    exe_path = _path_from_logical(extracted, expected_exe_path)
    exe_receipt = work / "exe.json"
    run_receipts.append(
        _run_process(
            "inventory-psx-exe",
            _provenance_command(
                "inventory_psx_exe.py",
                "--input",
                str(exe_path),
                "--json-out",
                str(exe_receipt),
            ),
            cwd=work,
            max_output_bytes=limits["max_compiler_output_bytes"],
        )
    )
    exe_report = _read_receipt(exe_receipt)
    expected_exe = profile["main_executable"]
    if (
        exe_report.get("file", {}).get("bytes") != expected_exe["file_size"]
        or exe_report.get("file", {}).get("sha256") != expected_exe["file_sha256"]
    ):
        raise UnknownVariantError(
            "PS-X EXE file identity is not the pinned PAL variant"
        )
    header = exe_report.get("header", {})
    if (
        header.get("initial_pc") != expected_exe["entry_pc"]
        or header.get("load_address") != expected_exe["load_range"]["start"]
        or header.get("text_end_exclusive")
        != expected_exe["load_range"]["end_exclusive"]
        or header.get("sha256") is None
    ):
        raise ProductError(
            "PS-X EXE header/range identity differs from the product profile"
        )
    if (
        exe_report.get("executable_identity", {}).get("sha256")
        != expected_exe["text_sha256"]
    ):
        raise ProductError("PS-X EXE text identity differs from the product profile")

    runtime_records: list[dict[str, Any]] = []
    for variant in profile["runtime_variants"]:
        logical = variant["logical_path"]
        matching_files = [
            item
            for item in iso_report.get("files", [])
            if item.get("logical_path") == logical
        ]
        if len(matching_files) != 1:
            raise UnknownVariantError(
                f"profile runtime container is absent or ambiguous: {logical}"
            )
        container_info = matching_files[0]
        if (
            container_info.get("hash_status") != "ok"
            or container_info.get("bytes") != variant["container_size"]
            or container_info.get("sha256") != variant["container_sha256"]
        ):
            raise UnknownVariantError(
                f"runtime container identity is not pinned: {logical}"
            )
        pb_path = _path_from_logical(extracted, logical)
        if pb_path.stat().st_size > limits["max_pbp_bytes"]:
            raise ProductError("runtime PBP exceeds the product bound")
        runtime_dir = work / "runtime" / variant["id"]
        runtime_receipt = work / "runtime" / f"{variant['id']}.json"
        run_receipts.append(
            _run_process(
                f"inventory-pbp-{variant['id']}",
                _provenance_command(
                    "inventory_pbp.py",
                    "--input",
                    str(pb_path),
                    "--verify-expanded",
                    "--extract-expanded",
                    str(runtime_dir),
                    "--json-out",
                    str(runtime_receipt),
                ),
                cwd=work,
                max_output_bytes=limits["max_compiler_output_bytes"],
            )
        )
        pbp_report = _read_receipt(runtime_receipt)
        if (
            pbp_report.get("entry_count", limits["max_pbp_entries"] + 1)
            > limits["max_pbp_entries"]
        ):
            raise ProductError("runtime PBP contains too many entries")
        if (
            pbp_report.get("container", {}).get("bytes") != variant["container_size"]
            or pbp_report.get("container", {}).get("sha256")
            != variant["container_sha256"]
        ):
            raise UnknownVariantError(
                f"runtime PBP container changed during inventory: {logical}"
            )
        entry_matches = [
            entry
            for entry in pbp_report.get("entries", [])
            if entry.get("index") == variant["entry_index"]
        ]
        if len(entry_matches) != 1:
            raise UnknownVariantError(f"runtime entry is absent: {variant['id']}")
        entry = entry_matches[0]
        if (
            entry.get("bytes") != variant["stored_size"]
            or entry.get("sha256") != variant["stored_sha256"]
            or entry.get("expanded_bytes") != variant["expanded_size"]
            or entry.get("expanded_sha256") != variant["expanded_sha256"]
        ):
            raise UnknownVariantError(
                f"runtime entry identity is not pinned: {variant['id']}"
            )
        expanded_name = (
            f"{variant['entry_index']:04d}-{variant['expanded_sha256'][:16]}.bin"
        )
        expanded_path = _require_regular_file(
            runtime_dir / expanded_name,
            f"expanded runtime {variant['id']}",
            max_bytes=limits["max_generated_file_bytes"] * 4,
        )
        actual_hash, actual_size = sha256_file(
            expanded_path, max_bytes=limits["max_generated_bytes"]
        )
        if (
            actual_hash != variant["expanded_sha256"]
            or actual_size != variant["expanded_size"]
        ):
            raise UnknownVariantError(
                f"expanded runtime bytes changed: {variant['id']}"
            )
        runtime_records.append(
            {
                "id": variant["id"],
                "kind": variant["kind"],
                "image_kind": "raw-payload",
                "image_path": expanded_path,
                "image_sha256": variant["expanded_sha256"],
                "sha256": variant["expanded_sha256"],
                "load_range": dict(variant["load_range"]),
                "relocation_metadata": variant["relocation_metadata"],
                "container": {
                    "logical_path": logical,
                    "size": variant["container_size"],
                    "sha256": variant["container_sha256"],
                    "entry_index": variant["entry_index"],
                    "stored_size": variant["stored_size"],
                    "stored_sha256": variant["stored_sha256"],
                },
            }
        )

    main_record = {
        "id": "main-exe",
        "kind": "ps-x-exe",
        "image_kind": "ps-x-exe",
        "image_path": exe_path,
        "image_sha256": expected_exe["file_sha256"],
        "sha256": expected_exe["text_sha256"],
        "load_range": dict(expected_exe["load_range"]),
        "relocation_metadata": None,
    }
    return {
        "prepared_report": prepared_report,
        "iso_report": iso_report,
        "exe_report": exe_report,
        "main": main_record,
        "runtime": runtime_records,
        "run_receipts": run_receipts,
        "cue_hash": sha256_file(paths["cue"], max_bytes=limits["max_cue_bytes"])[0],
        "staged_track_name": staged_track.name,
        "staged_cue_name": staged_cue.name,
    }


def _source_media(
    profile: Mapping[str, Any], paths: Mapping[str, Path], media: Mapping[str, Any]
) -> list[dict[str, Any]]:
    report = media["prepared_report"]
    files: list[dict[str, Any]] = [
        {
            "role": "bios",
            "name": paths["bios"].name,
            "size": profile["bios"]["size"],
            "sha256": profile["bios"]["sha256"],
        },
        {
            "role": "cue",
            "name": paths["cue"].name,
            "size": paths["cue"].stat().st_size,
            "sha256": media["cue_hash"],
        },
        {
            "role": "hueponik-patch",
            "name": paths["patch"].name,
            "size": profile["hueponik_patch"]["size"],
            "sha256": profile["hueponik_patch"]["sha256"],
        },
        {
            "role": "track-01",
            "name": media["staged_track_name"],
            "size": profile["patched_track_01"]["size"],
            "sha256": profile["patched_track_01"]["sha256"],
        },
    ]
    audio = report.get("audio_tracks", [])
    if not isinstance(audio, list):
        raise ProductError("prepared media audio report is malformed")
    if len(audio) != profile["game"]["cue_tracks"] - 1:
        raise ProductError("prepared media audio track count is not exact")
    for index, item in enumerate(audio, start=2):
        track = index
        name = _safe_base_name(item.get("name"), "audio track name")
        size = _integer(item.get("size"), "audio track size", minimum=1)
        hashes = item.get("hashes")
        md5 = None if not isinstance(hashes, dict) else hashes.get("md5")
        if not isinstance(md5, str) or MD5_RE.fullmatch(md5) is None:
            raise ProductError("audio track report does not contain a hash")
        # The existing media manifest currently exposes MD5 for audio.  A
        # catalog source file needs SHA-256, so hash the staged local copy once;
        # the bytes are still retained only in the temporary media workspace.
        # The prepared report is authoritative for the file name/size; the
        # actual SHA-256 is filled by the caller from the staged tree.
        files.append(
            {
                "role": f"track-{track:02d}",
                "name": name,
                "size": size,
                "sha256": "0" * 64,
            }
        )
    files.sort(key=lambda item: (item["role"], item["name"]))
    names = [(item["role"], item["name"].casefold()) for item in files]
    if len(names) != len(set(names)):
        raise ProductError("source media contains portable duplicate names")
    return files


def _complete_source_media(
    files: list[dict[str, Any]], media: Mapping[str, Any], work: Path
) -> list[dict[str, Any]]:
    prepared_dir = (work / "prepared-media").resolve(strict=True)
    result: list[dict[str, Any]] = []
    for item in files:
        output = {key: value for key, value in item.items() if not key.startswith("_")}
        if output["sha256"] == "0" * 64:
            candidate = prepared_dir / output["name"]
            candidate = _require_regular_file(
                candidate, f"staged audio {output['name']}", max_bytes=268435456
            )
            digest, size = sha256_file(candidate, max_bytes=268435456)
            if size != output["size"]:
                raise ProductError(f"staged audio size changed: {output['name']}")
            output["sha256"] = digest
        result.append(output)
    return result


def _identity_sha256(record: Mapping[str, Any]) -> str:
    return sha256_bytes(
        canonical_json_bytes(
            {
                "load_range": record["load_range"],
                "sha256": record["sha256"],
                "relocation_metadata": record["relocation_metadata"],
            }
        )
    )


def _region_map(regions: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(item["id"]): item for item in regions["variants"]}


def _catalog_input(
    profile: Mapping[str, Any],
    profile_sha256: str,
    media_files: list[dict[str, Any]],
    media: Mapping[str, Any],
    regions: Mapping[str, Any],
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    region_by_id = _region_map(regions)
    records = [media["main"], *media["runtime"]]
    variants: list[dict[str, Any]] = []
    for record in records:
        region = region_by_id.get(record["id"])
        variant_media = list(media_files)
        if record["id"] != "main-exe":
            container = record["container"]
            variant_media.append(
                {
                    "role": "payload-container",
                    "name": PurePosixPath(container["logical_path"]).name,
                    "size": container["size"],
                    "sha256": container["sha256"],
                }
            )
            variant_media.sort(key=lambda item: (item["role"], item["name"]))
        variants.append(
            {
                "id": record["id"],
                "kind": record["kind"],
                "image_kind": record["image_kind"],
                "identity": {
                    "load_range": record["load_range"],
                    "sha256": record["sha256"],
                    "relocation_metadata": record["relocation_metadata"],
                },
                "image_sha256": record["image_sha256"],
                "dispatch_roots": [] if region is None else region["entry_points"],
                "executed_ranges": [] if region is None else region["code_ranges"],
                "delay_slot_owners": []
                if region is None
                else region["delay_slot_owners"],
                "source_media": {
                    "source_id": f"{profile['game']['id']}-hueponik-pal",
                    "format": "redump-bin-cue",
                    "files": variant_media,
                },
                "compile": False if region is None else region["compile"],
                "artifact": None,
            }
        )
    variants.sort(key=lambda item: item["id"])
    document = {
        "schema": CATALOG_INPUT_SCHEMA,
        "profile_sha256": profile_sha256,
        "catalog_id": profile["catalog"]["id"],
        "address_bits": 32,
        "page_size": profile["catalog"]["page_size"],
        "stack_window": profile["catalog"]["stack_window"],
        "variants": variants,
    }
    return document, {item["id"]: item for item in variants}


def _validate_catalog_input(
    document: Any,
    *,
    profile: Mapping[str, Any],
    profile_sha256: str,
) -> dict[str, Any]:
    fields = _strict_keys(
        document,
        {
            "schema",
            "profile_sha256",
            "catalog_id",
            "address_bits",
            "page_size",
            "stack_window",
            "variants",
        },
        "catalog-input",
    )
    if fields["schema"] != CATALOG_INPUT_SCHEMA:
        raise ProductError("unsupported catalog-input schema")
    if fields["profile_sha256"] != profile_sha256:
        raise ProductError("catalog-input profile identity does not match the profile")
    _digest(fields["profile_sha256"], "catalog-input.profile_sha256")
    if fields["catalog_id"] != profile["catalog"]["id"]:
        raise ProductError(
            "catalog-input catalog_id does not match the product profile"
        )
    if fields["address_bits"] != 32:
        raise ProductError("catalog-input address_bits must be 32")
    if fields["page_size"] != profile["catalog"]["page_size"]:
        raise ProductError("catalog-input page_size does not match the product profile")
    expected_stack = _address_range(
        profile["catalog"]["stack_window"], "profile.catalog.stack_window"
    )
    if (
        _address_range(fields["stack_window"], "catalog-input.stack_window")
        != expected_stack
    ):
        raise ProductError(
            "catalog-input stack_window does not match the product profile"
        )

    variants = fields["variants"]
    if not isinstance(variants, list) or not variants or len(variants) > 4096:
        raise ProductError("catalog-input.variants must be a bounded non-empty array")
    known = _profile_variant_map(profile)
    expected_ids = set(known)
    seen_ids: set[str] = set()
    previous_id = ""
    for index, raw in enumerate(variants):
        item = _strict_keys(
            raw,
            {
                "id",
                "kind",
                "image_kind",
                "identity",
                "image_sha256",
                "dispatch_roots",
                "executed_ranges",
                "delay_slot_owners",
                "source_media",
                "compile",
                "artifact",
            },
            f"catalog-input.variants[{index}]",
        )
        variant_id = _text(
            item["id"], f"catalog-input.variants[{index}].id", token=True
        )
        if variant_id <= previous_id or variant_id in seen_ids:
            raise ProductError("catalog-input variants must be sorted and unique")
        previous_id = variant_id
        seen_ids.add(variant_id)
        if variant_id not in known:
            raise UnknownVariantError(
                f"catalog-input names an unknown variant: {variant_id}"
            )
        expected = known[variant_id]
        kind = _text(item["kind"], f"catalog-input.variants[{index}].kind", token=True)
        image_kind = _text(
            item["image_kind"],
            f"catalog-input.variants[{index}].image_kind",
            token=True,
        )
        expected_kind = expected["kind"]
        expected_image_kind = "ps-x-exe" if variant_id == "main-exe" else "raw-payload"
        if kind != expected_kind or image_kind != expected_image_kind:
            raise UnknownVariantError(
                f"catalog-input variant kind is not pinned: {variant_id}"
            )
        identity = _strict_keys(
            item["identity"],
            {"load_range", "sha256", "relocation_metadata"},
            f"catalog-input.variants[{index}].identity",
        )
        identity_range = _address_range(
            identity["load_range"],
            f"catalog-input.variants[{index}].identity.load_range",
        )
        identity_sha = _digest(
            identity["sha256"], f"catalog-input.variants[{index}].identity.sha256"
        )
        relocation = identity["relocation_metadata"]
        if relocation is not None and not isinstance(relocation, dict):
            raise ProductError(
                "catalog-input relocation_metadata must be an object or null"
            )
        if (
            identity_range != expected["load_range"]
            or identity_sha != expected["sha256"]
            or relocation != expected.get("relocation_metadata")
        ):
            raise UnknownVariantError(
                f"catalog-input executable identity is not pinned: {variant_id}"
            )
        image_sha = _digest(
            item["image_sha256"], f"catalog-input.variants[{index}].image_sha256"
        )
        if image_sha != expected["image_sha256"]:
            raise UnknownVariantError(
                f"catalog-input image identity is not pinned: {variant_id}"
            )

        roots = item["dispatch_roots"]
        if not isinstance(roots, list):
            raise ProductError("catalog-input dispatch_roots must be an array")
        normalized_roots = [
            _integer(
                value, f"catalog-input.variants[{index}].dispatch_roots", minimum=0
            )
            for value in roots
        ]
        if normalized_roots != sorted(set(normalized_roots)):
            raise ProductError("catalog-input dispatch_roots must be sorted and unique")
        if any(
            value % 4
            or not identity_range["start"] <= value < identity_range["end_exclusive"]
            for value in normalized_roots
        ):
            raise ProductError("catalog-input dispatch_roots must lie in the image")

        executed = item["executed_ranges"]
        if not isinstance(executed, list):
            raise ProductError("catalog-input executed_ranges must be an array")
        normalized_executed = (
            []
            if not executed
            else _range_list(
                executed, f"catalog-input.variants[{index}].executed_ranges"
            )
        )
        if any(
            not identity_range["start"] <= value["start"]
            or value["end_exclusive"] > identity_range["end_exclusive"]
            for value in normalized_executed
        ):
            raise ProductError("catalog-input executed range lies outside the image")

        delay = item["delay_slot_owners"]
        if not isinstance(delay, list):
            raise ProductError("catalog-input delay_slot_owners must be an array")
        previous_branch = -1
        for delay_index, raw_delay in enumerate(delay):
            delay_fields = _strict_keys(
                raw_delay,
                {"branch_pc", "delay_slot_pc", "owner_root"},
                f"catalog-input.variants[{index}].delay_slot_owners[{delay_index}]",
            )
            branch = _integer(
                delay_fields["branch_pc"], "catalog delay branch_pc", minimum=0
            )
            slot = _integer(
                delay_fields["delay_slot_pc"], "catalog delay delay_slot_pc", minimum=0
            )
            owner = _integer(
                delay_fields["owner_root"], "catalog delay owner_root", minimum=0
            )
            if (
                branch <= previous_branch
                or slot != branch + 4
                or branch % 4
                or slot % 4
            ):
                raise ProductError(
                    "catalog-input delay slots must be sorted and adjacent"
                )
            previous_branch = branch
            if owner not in normalized_roots:
                raise ProductError("catalog-input delay owner is not a dispatch root")
            if (
                not identity_range["start"] <= branch
                or slot >= identity_range["end_exclusive"]
            ):
                raise ProductError("catalog-input delay slot lies outside the image")

        source_media = _strict_keys(
            item["source_media"],
            {"source_id", "format", "files"},
            f"catalog-input.variants[{index}].source_media",
        )
        source_id = _text(
            source_media["source_id"],
            f"catalog-input.variants[{index}].source_media.source_id",
            token=True,
        )
        source_format = _text(
            source_media["format"],
            f"catalog-input.variants[{index}].source_media.format",
            token=True,
        )
        if (
            source_id != f"{profile['game']['id']}-hueponik-pal"
            or source_format != "redump-bin-cue"
        ):
            raise ProductError(
                "catalog-input source media identity is not the pinned product"
            )
        source_files = source_media["files"]
        if (
            not isinstance(source_files, list)
            or not source_files
            or len(source_files) > 4096
        ):
            raise ProductError("catalog-input source_media.files must be non-empty")
        portable_names: set[tuple[str, str]] = set()
        previous_file: tuple[str, str] | None = None
        source_total = 0
        for file_index, raw_file in enumerate(source_files):
            file_fields = _strict_keys(
                raw_file,
                {"role", "name", "size", "sha256"},
                f"catalog-input.variants[{index}].source_media.files[{file_index}]",
            )
            role = _text(
                file_fields["role"],
                f"catalog-input source role {file_index}",
                token=True,
            )
            name = _safe_base_name(
                file_fields["name"], f"catalog-input source name {file_index}"
            )
            size = _integer(
                file_fields["size"],
                f"catalog-input source size {file_index}",
                minimum=0,
            )
            _digest(file_fields["sha256"], f"catalog-input source hash {file_index}")
            source_total += size
            if source_total > 1 << 30:
                raise ProductError("catalog-input source media exceeds the byte bound")
            key = (role.casefold(), name.casefold())
            if key in portable_names:
                raise ProductError(
                    "catalog-input source files contain portable duplicates"
                )
            portable_names.add(key)
            sort_key = (role, name)
            if previous_file is not None and sort_key < previous_file:
                raise ProductError("catalog-input source files must be sorted")
            previous_file = sort_key
        if type(item["compile"]) is not bool:
            raise ProductError("catalog-input compile must be boolean")
        if item["artifact"] is not None:
            raise ProductError(
                "catalog-input artifact must be null before finalization"
            )

    if seen_ids != expected_ids:
        raise ProductError("catalog-input variants do not cover the pinned profile")
    return fields


def _load_artifacts(
    path: Path, root: Path, *, limits: Mapping[str, int]
) -> dict[str, dict[str, Any]]:
    document = load_json(path)
    fields = _strict_keys(document, {"schema", "artifacts"}, "artifacts")
    if fields["schema"] != ARTIFACTS_SCHEMA:
        raise ProductError("unsupported product artifacts schema")
    _reject_link_ancestors(root)
    artifact_root = root.resolve(strict=True)
    if _is_link_or_junction(artifact_root) or not artifact_root.is_dir():
        raise ProductError("artifact root must be a real directory")
    artifacts = fields["artifacts"]
    if (
        not isinstance(artifacts, list)
        or len(artifacts) > limits["max_generated_files"]
    ):
        raise ProductError("artifacts array is not bounded")
    result: dict[str, dict[str, Any]] = {}
    total_size = 0
    for index, raw in enumerate(artifacts):
        item = _strict_keys(
            raw, {"variant_id", "id", "path", "sha256"}, f"artifacts[{index}]"
        )
        variant_id = _text(
            item["variant_id"], f"artifacts[{index}].variant_id", token=True
        )
        if variant_id in result:
            raise ProductError("duplicate artifact variant_id")
        artifact_id = _text(item["id"], f"artifacts[{index}].id", token=True)
        relative = _safe_relative_path(item["path"], f"artifacts[{index}].path")
        digest = _digest(item["sha256"], f"artifacts[{index}].sha256")
        candidate = artifact_root.joinpath(*relative.split("/"))
        _reject_link_ancestors(candidate)
        resolved = candidate.resolve(strict=False)
        try:
            resolved.relative_to(artifact_root)
        except ValueError as error:
            raise ProductError("artifact path escapes artifact root") from error
        file_path = _require_regular_file(
            resolved,
            f"artifact {variant_id}",
            max_bytes=limits["max_generated_file_bytes"],
        )
        actual, size = sha256_file(
            file_path, max_bytes=limits["max_generated_file_bytes"]
        )
        if actual != digest:
            raise ProductError(f"artifact hash mismatch: {variant_id}")
        total_size += size
        if total_size > limits["max_generated_bytes"]:
            raise ProductError("artifacts exceed the total generated-byte bound")
        result[variant_id] = {
            "id": artifact_id,
            "path": relative,
            "sha256": digest,
            "size": size,
        }
    return result


def _catalog_document(
    catalog_input: Mapping[str, Any],
    variants_by_id: Mapping[str, Mapping[str, Any]],
    artifacts: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    output_variants: list[dict[str, Any]] = []
    compiled_ids = {
        variant_id
        for variant_id, source in variants_by_id.items()
        if source["compile"] is True
    }
    if set(artifacts) != compiled_ids:
        missing = sorted(compiled_ids - set(artifacts))
        extra = sorted(set(artifacts) - compiled_ids)
        raise ProductError(
            f"catalog artifacts must exactly cover compiled variants; missing={missing}, extra={extra}"
        )
    source_hashes = {
        item["sha256"]
        for source in variants_by_id.values()
        for item in source["source_media"]["files"]
    }
    for variant_id, artifact in sorted(artifacts.items()):
        if variant_id not in variants_by_id:
            raise UnknownVariantError(
                f"artifact names an unknown variant: {variant_id}"
            )
        source = variants_by_id[variant_id]
        if source["compile"] is not True:
            raise ProductError(
                f"catalog artifact is attached to an uncompiled variant: {variant_id}"
            )
        if not source["executed_ranges"] or not source["dispatch_roots"]:
            raise ProductError(
                f"catalog variant lacks reviewed region authorization: {variant_id}"
            )
        if artifact["sha256"] in source_hashes:
            raise ProductError(
                f"catalog artifact reuses source-media bytes: {variant_id}"
            )
        output_variants.append(
            {
                "load_range": source["identity"]["load_range"],
                "sha256": source["identity"]["sha256"],
                "relocation_metadata": source["identity"]["relocation_metadata"],
                "dispatch_roots": source["dispatch_roots"],
                "executed_ranges": source["executed_ranges"],
                "delay_slot_owners": source["delay_slot_owners"],
                "source_media": source["source_media"],
                "artifact": {
                    "id": artifact["id"],
                    "path": artifact["path"],
                    "sha256": artifact["sha256"],
                },
            }
        )
    output_variants.sort(key=lambda item: _identity_sha256(item))
    return {
        "schema": "psxrecomp.clean-aot.catalog.v1",
        "catalog_id": catalog_input["catalog_id"],
        "address_bits": 32,
        "page_size": catalog_input["page_size"],
        "stack_window": catalog_input["stack_window"],
        "variants": output_variants,
    }


def _validate_catalog_with_cli(
    document: Mapping[str, Any], work: Path, *, max_output_bytes: int
) -> tuple[str, dict[str, Any]]:
    catalog_path = work / "catalog-to-validate.json"
    catalog_path.write_bytes(canonical_json_bytes(document) + b"\n")
    env_path = str(CATALOG_ROOT)
    old_pythonpath = os.environ.get("PYTHONPATH")
    if old_pythonpath:
        env_path += os.pathsep + old_pythonpath
    env = os.environ.copy()
    env["PYTHONPATH"] = env_path
    with tempfile.TemporaryDirectory(prefix="catalog-tool-", dir=str(work)) as logs:
        stdout_path = Path(logs) / "stdout"
        stderr_path = Path(logs) / "stderr"
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            completed = subprocess.run(
                [sys.executable, "-m", "psx_catalog", "validate", str(catalog_path)],
                cwd=str(CATALOG_ROOT),
                env=env,
                stdin=subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                check=False,
                timeout=120,
            )
        if (
            stdout_path.stat().st_size > max_output_bytes
            or stderr_path.stat().st_size > max_output_bytes
        ):
            raise ToolInvocationError("psx-catalog exceeded its bounded output size")
        if completed.returncode != 0:
            detail = stderr_path.read_text(encoding="utf-8", errors="replace")[:2000]
            raise ToolInvocationError(f"psx-catalog validation failed: {detail}")
        stdout_bytes = stdout_path.read_bytes()
        stderr_bytes = stderr_path.read_bytes()
    return sha256_bytes(canonical_json_bytes(document)), {
        "tool": "psx-catalog-validate",
        "returncode": completed.returncode,
        "stdout_bytes": len(stdout_bytes),
        "stdout_sha256": sha256_bytes(stdout_bytes),
        "stderr_bytes": len(stderr_bytes),
        "stderr_sha256": sha256_bytes(stderr_bytes),
    }


def _compiler_manifest(
    record: Mapping[str, Any],
    region: Mapping[str, Any],
    *,
    catalog_sha256: str,
) -> tuple[str, str]:
    variant_sha256 = _identity_sha256(record)
    lines = [
        f"schema={COMPILER_MANIFEST_SCHEMA}",
        f"image_sha256={record['image_sha256']}",
        f"catalog_sha256={catalog_sha256}",
        f"variant_sha256={variant_sha256}",
        f"image_kind={record['image_kind']}",
        f"load_start=0x{record['load_range']['start']:08X}",
        f"load_end_exclusive=0x{record['load_range']['end_exclusive']:08X}",
    ]
    lines.extend(f"entry=0x{value:08X}" for value in region["entry_points"])
    lines.extend(
        f"code=0x{item['start']:08X}-0x{item['end_exclusive']:08X}"
        for item in region["code_ranges"]
    )
    lines.extend(
        f"data=0x{item['start']:08X}-0x{item['end_exclusive']:08X}"
        for item in region["data_ranges"]
    )
    lines.extend(f"external=0x{value:08X}" for value in region["external_targets"])
    lines.extend(
        "indirect=0x{site:08X}:{targets}".format(
            site=item["site"],
            targets=",".join(f"0x{target:08X}" for target in item["targets"]),
        )
        for item in region["indirect_target_sets"]
    )
    lines.append(
        f"maximum_instructions_per_shard={region['maximum_instructions_per_shard']}"
    )
    return "\n".join(lines) + "\n", variant_sha256


def _compiler_command(
    compiler: Path,
    interface: str,
    record: Mapping[str, Any],
    manifest: Path,
    output: Path,
) -> list[str]:
    if interface not in {"clean-aot-v1", "clean-aot-image-v1"}:
        raise ProductError(f"unsupported compiler interface: {interface}")
    if record["image_kind"] == "raw-payload" and interface != "clean-aot-image-v1":
        raise ProductError(
            "raw runtime payload requires the explicit clean-aot-image-v1 compiler interface"
        )
    executable: list[str] = [str(compiler)]
    if compiler.suffix.casefold() == ".py":
        executable = [sys.executable, str(compiler)]
    if interface == "clean-aot-v1":
        return [
            *executable,
            "--exe",
            str(record["image_path"]),
            "--manifest",
            str(manifest),
            "--output",
            str(output),
        ]
    return [
        *executable,
        "--image",
        str(record["image_path"]),
        "--image-kind",
        str(record["image_kind"]),
        "--manifest",
        str(manifest),
        "--output",
        str(output),
    ]


def _scan_output_tree(
    root: Path,
    *,
    limits: Mapping[str, int],
    forbidden_hashes: set[str],
    forbidden_names: set[str],
) -> list[dict[str, Any]]:
    if not root.is_dir() or _is_link_or_junction(root):
        raise ProductError("generated output is not a real directory")
    files: list[dict[str, Any]] = []
    total = 0
    pending = [root]
    while pending:
        current = pending.pop()
        for child in sorted(current.iterdir(), key=lambda path: path.name.casefold()):
            if _is_link_or_junction(child):
                raise ProductError(
                    f"generated output contains an indirect path: {child.name}"
                )
            if child.is_dir():
                pending.append(child)
                continue
            if not child.is_file():
                raise ProductError(
                    f"generated output contains a non-file: {child.name}"
                )
            relative = child.relative_to(root).as_posix()
            _safe_relative_path(relative, "generated relative path")
            if child.name.casefold() in forbidden_names:
                raise ProductError(
                    f"generated output reuses an input filename: {child.name}"
                )
            digest, size = sha256_file(
                child, max_bytes=limits["max_generated_file_bytes"]
            )
            if digest in forbidden_hashes:
                raise ProductError(
                    f"generated output contains an input byte image: {child.name}"
                )
            total += size
            if total > limits["max_generated_bytes"]:
                raise ProductError("generated output exceeds the total byte bound")
            files.append({"path": relative, "bytes": size, "sha256": digest})
            if len(files) > limits["max_generated_files"]:
                raise ProductError("generated output contains too many files")
    files.sort(key=lambda item: item["path"])
    return files


def _write_sources_cmake(
    root: Path, files: Sequence[Mapping[str, Any]]
) -> dict[str, Any]:
    sources = [
        f"generated/{item['path']}"
        for item in files
        if Path(str(item["path"])).suffix.casefold() in SOURCE_SUFFIXES
    ]
    if not sources:
        raise ProductError("compiler produced no native C/C++ source files")
    lines = [
        "# Generated by the clean offline product pipeline; do not hand-edit.",
        'set(CLEAN_AOT_PRODUCT_GENERATED_ROOT "${CMAKE_CURRENT_LIST_DIR}")',
        "set(CLEAN_AOT_PRODUCT_GENERATED_SOURCES",
    ]
    lines.extend(
        f'    "${{CLEAN_AOT_PRODUCT_GENERATED_ROOT}}/{path}"' for path in sources
    )
    lines.append(")")
    payload = ("\n".join(lines) + "\n").encode("utf-8")
    _write_atomic_new(root / "sources.cmake", payload)
    digest, size = sha256_file(root / "sources.cmake")
    return {"path": "sources.cmake", "bytes": size, "sha256": digest}


def _build_dry_run_receipt(
    profile: Mapping[str, Any],
    profile_sha: str,
    *,
    cue: Path,
    bios: Path,
    patch: Path,
    xdelta3: Path,
    compiler: Path | None,
    regions: Path | None,
    output: Path | None,
) -> dict[str, Any]:
    return {
        "schema": RECEIPT_SCHEMA,
        "status": "dry-run",
        "profile": {"schema": PROFILE_SCHEMA, "sha256": profile_sha},
        "game_id": profile["game"]["id"],
        "inputs": {
            "cue_name": cue.name,
            "bios_name": bios.name,
            "patch_name": patch.name,
            "xdelta3_name": xdelta3.name,
            "compiler_name": None if compiler is None else compiler.name,
            "regions_name": None if regions is None else regions.name,
        },
        "planned_steps": [
            "verify exact Redump BIN/CUE and SCPH1001 BIOS through provenance CLI",
            "apply pinned hueponik dev PAL patch in a temporary workspace",
            "cook and inventory the patched Track 01 ISO",
            "inventory the exact PS-X EXE and profile-listed bounded PBP entries",
            "emit strict region/compiler/catalog metadata",
            "invoke the offline clean-AOT compiler into external generated output",
            "publish generated sources and receipts atomically",
        ],
        "output_policy": {
            "external_only": True,
            "media_bytes_persisted": False,
            "bios_bytes_persisted": False,
            "executable_bytes_persisted": False,
            "unknown_variants_fail_closed": True,
            "requested_output_name": None if output is None else output.name,
        },
    }


def _write_receipt(path: Path | None, document: Mapping[str, Any]) -> None:
    if path is not None:
        destination = _assert_external_output(path)
        _write_atomic_new(destination, canonical_json_bytes(document) + b"\n")


def setup_product(
    *,
    cue: Path,
    bios: Path,
    patch: Path,
    xdelta3: Path,
    compiler: Path,
    output: Path,
    profile_path: Path = DEFAULT_PROFILE,
    media_manifest: Path = DEFAULT_MEDIA_MANIFEST,
    regions_path: Path,
    compiler_interface: str = "clean-aot-v1",
    artifacts_path: Path | None = None,
    artifact_root: Path | None = None,
) -> dict[str, Any]:
    profile, profile_sha = _load_profile(profile_path)
    paths = _preflight_inputs(
        cue=cue,
        bios=bios,
        patch=patch,
        xdelta3=xdelta3,
        compiler=compiler,
        profile=profile,
    )
    media_manifest = _require_regular_file(
        media_manifest, "media manifest", max_bytes=16 * 1024 * 1024
    )
    regions = _validate_regions(load_json(regions_path), profile)
    if compiler_interface not in {"clean-aot-v1", "clean-aot-image-v1"}:
        raise ProductError(f"unsupported compiler interface: {compiler_interface}")
    output_root = _assert_external_output(output)
    partial = output_root.with_name(output_root.name + ".partial")
    if partial.exists():
        raise ProductError(f"stale product partial output exists: {partial.name}")
    partial.mkdir(parents=True)
    limits = profile["limits"]
    try:
        with tempfile.TemporaryDirectory(
            prefix="wipeout3se-product-", dir=tempfile.gettempdir()
        ) as temporary:
            work = Path(temporary)
            media = _run_media_pipeline(
                paths=paths, profile=profile, media_manifest=media_manifest, work=work
            )
            source_files = _complete_source_media(
                _source_media(profile, paths, media), media, work
            )
            catalog_input, catalog_variants = _catalog_input(
                profile, profile_sha, source_files, media, regions
            )
            _validate_catalog_input(
                catalog_input, profile=profile, profile_sha256=profile_sha
            )
            catalog_input_sha = sha256_bytes(canonical_json_bytes(catalog_input))
            (partial / "catalog-input.json").write_bytes(
                canonical_json_bytes(catalog_input) + b"\n"
            )

            catalog_sha = catalog_input_sha
            catalog_tool_receipt: dict[str, Any] | None = None
            artifacts: dict[str, dict[str, Any]] = {}
            if artifacts_path is not None:
                if artifact_root is None:
                    raise ProductError("--artifacts requires --artifact-root")
                artifacts = _load_artifacts(
                    artifacts_path, artifact_root, limits=limits
                )
                catalog, catalog_tool_receipt = _validate_catalog_with_cli(
                    _catalog_document(catalog_input, catalog_variants, artifacts),
                    work,
                    max_output_bytes=limits["max_compiler_output_bytes"],
                )
                catalog_sha = catalog
                (partial / "catalog.json").write_bytes(
                    canonical_json_bytes(
                        _catalog_document(catalog_input, catalog_variants, artifacts)
                    )
                    + b"\n"
                )

            compiler_receipts: list[dict[str, Any]] = []
            records = [media["main"], *media["runtime"]]
            region_by_id = _region_map(regions)
            forbidden_hashes = {
                profile["bios"]["sha256"],
                profile["hueponik_patch"]["sha256"],
                profile["patched_track_01"]["sha256"],
                profile["main_executable"]["file_sha256"],
                profile["main_executable"]["text_sha256"],
                *(item["image_sha256"] for item in records),
            }
            forbidden_names = {
                paths["bios"].name.casefold(),
                paths["patch"].name.casefold(),
                paths["cue"].name.casefold(),
                "scph1001.bin",
                "sces_028.45",
                "nfe.pb",
            }
            for record in records:
                region = region_by_id.get(record["id"])
                if region is None or not region["compile"]:
                    continue
                manifest_text, variant_sha = _compiler_manifest(
                    record, region, catalog_sha256=catalog_sha
                )
                manifest_path = (
                    partial / "compiler-manifests" / f"{record['id']}.manifest"
                )
                manifest_path.parent.mkdir(parents=True, exist_ok=True)
                manifest_path.write_text(manifest_text, encoding="utf-8", newline="\n")
                generated = partial / "generated" / record["id"]
                compiler_tool_receipt = _run_process(
                    f"clean-aot-compiler-{record['id']}",
                    _compiler_command(
                        compiler, compiler_interface, record, manifest_path, generated
                    ),
                    cwd=work,
                    max_output_bytes=limits["max_compiler_output_bytes"],
                )
                generated_files = _scan_output_tree(
                    generated,
                    limits=limits,
                    forbidden_hashes=forbidden_hashes,
                    forbidden_names=forbidden_names,
                )
                compiler_receipts.append(
                    {
                        "variant_id": record["id"],
                        "variant_sha256": variant_sha,
                        "image_kind": record["image_kind"],
                        "manifest": f"compiler-manifests/{record['id']}.manifest",
                        "generated_directory": f"generated/{record['id']}",
                        "generated_files": generated_files,
                        "tool": compiler_tool_receipt,
                    }
                )
            if not compiler_receipts:
                raise ProductError("regions authorize no compiled variant")

            all_generated = partial / "generated"
            generated_files = _scan_output_tree(
                all_generated,
                limits=limits,
                forbidden_hashes=forbidden_hashes,
                forbidden_names=forbidden_names,
            )
            source_manifest = _write_sources_cmake(partial, generated_files)
            output_files = _scan_output_tree(
                partial,
                limits=limits,
                forbidden_hashes=forbidden_hashes,
                forbidden_names=forbidden_names,
            )
            receipt: dict[str, Any] = {
                "schema": RECEIPT_SCHEMA,
                "status": "setup-complete",
                "profile": {"sha256": profile_sha},
                "game_id": profile["game"]["id"],
                "media": {
                    "cue_tracks": profile["game"]["cue_tracks"],
                    "patched_track_01": {
                        "size": profile["patched_track_01"]["size"],
                        "sha256": profile["patched_track_01"]["sha256"],
                    },
                    "bios": {
                        "size": profile["bios"]["size"],
                        "sha256": profile["bios"]["sha256"],
                    },
                    "runtime_variants": [
                        {
                            key: value
                            for key, value in item.items()
                            if key not in {"image_path"}
                        }
                        for item in media["runtime"]
                    ],
                },
                "catalog": {
                    "input_path": "catalog-input.json",
                    "input_sha256": catalog_input_sha,
                    "path": None if not artifacts else "catalog.json",
                    "sha256": catalog_sha,
                    "validated": catalog_tool_receipt is not None,
                    "binding_is_final_catalog": bool(artifacts),
                    "tool": catalog_tool_receipt,
                },
                "compiler": {
                    "interface": compiler_interface,
                    "manifests_directory": "compiler-manifests",
                    "variants": compiler_receipts,
                },
                "provenance_tools": media["run_receipts"],
                "sources_manifest": source_manifest,
                "output": {
                    "external_only": True,
                    "file_count": len(output_files),
                    "total_bytes": sum(int(item["bytes"]) for item in output_files),
                    "files": output_files,
                },
                "runtime_policy": {
                    "compiler_linked_at_runtime": False,
                    "interpreter_linked_at_runtime": False,
                    "jit_linked_at_runtime": False,
                    "capture_compiler_linked_at_runtime": False,
                    "legacy_or_emulator_runtime_linked_at_runtime": False,
                    "unknown_variant_behavior": "reject-before-dispatch",
                },
            }
            (partial / "receipt.json").write_bytes(
                canonical_json_bytes(receipt) + b"\n"
            )
        partial.replace(output_root)
        return receipt
    except Exception:
        shutil.rmtree(partial, ignore_errors=True)
        raise


def finalize_catalog(
    *,
    catalog_input_path: Path,
    artifacts_path: Path,
    artifact_root: Path,
    output: Path,
    profile_path: Path = DEFAULT_PROFILE,
) -> dict[str, Any]:
    profile, profile_sha = _load_profile(profile_path)
    fields = _validate_catalog_input(
        load_json(catalog_input_path), profile=profile, profile_sha256=profile_sha
    )
    catalog_input = fields
    limits = {
        "max_generated_files": 4096,
        "max_generated_bytes": 512 * 1024 * 1024,
        "max_generated_file_bytes": 67108864,
    }
    artifacts = _load_artifacts(artifacts_path, artifact_root, limits=limits)
    variants_by_id = {str(item["id"]): item for item in fields["variants"]}
    output_document = _catalog_document(fields, variants_by_id, artifacts)
    output_root = _assert_external_output(output, must_exist=True)
    if (output_root / "catalog.json").exists() or (
        output_root / "finalize-receipt.json"
    ).exists():
        raise ProductError(
            "refusing to overwrite an existing finalized catalog/receipt"
        )
    with tempfile.TemporaryDirectory(
        prefix="wipeout3se-finalize-", dir=tempfile.gettempdir()
    ) as temporary:
        catalog_hash, tool_receipt = _validate_catalog_with_cli(
            output_document, Path(temporary), max_output_bytes=1048576
        )
    catalog_payload = canonical_json_bytes(output_document) + b"\n"
    receipt = {
        "schema": FINALIZE_RECEIPT_SCHEMA,
        "status": "catalog-finalized",
        "catalog_input_sha256": sha256_bytes(canonical_json_bytes(catalog_input)),
        "catalog_sha256": catalog_hash,
        "catalog_path": "catalog.json",
        "tool": tool_receipt,
        "artifact_variant_ids": sorted(artifacts),
        "runtime_policy": {"unknown_variant_behavior": "reject-before-dispatch"},
    }
    _write_atomic_new(output_root / "catalog.json", catalog_payload)
    _write_atomic_new(
        output_root / "finalize-receipt.json", canonical_json_bytes(receipt) + b"\n"
    )
    return receipt


def run_dry_run(
    *,
    cue: Path,
    bios: Path,
    patch: Path,
    xdelta3: Path,
    compiler: Path | None,
    profile_path: Path = DEFAULT_PROFILE,
    regions_path: Path | None = None,
    output: Path | None = None,
    receipt_path: Path | None = None,
) -> dict[str, Any]:
    profile, profile_sha = _load_profile(profile_path)
    _preflight_inputs(
        cue=cue,
        bios=bios,
        patch=patch,
        xdelta3=xdelta3,
        compiler=compiler,
        profile=profile,
    )
    if regions_path is not None:
        _validate_regions(load_json(regions_path), profile)
    if output is not None:
        _assert_external_output(output)
    receipt = _build_dry_run_receipt(
        profile,
        profile_sha,
        cue=cue,
        bios=bios,
        patch=patch,
        xdelta3=xdelta3,
        compiler=compiler,
        regions=regions_path,
        output=output,
    )
    _write_receipt(receipt_path, receipt)
    return receipt
