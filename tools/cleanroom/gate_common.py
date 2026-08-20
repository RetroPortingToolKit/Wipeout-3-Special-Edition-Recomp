#!/usr/bin/env python3
"""Deterministic source and release-link isolation gates.

This module is intentionally standalone.  It does not import, execute, or
copy any of the legacy project's diagnostics.  The source gate is a
conservative textual admission check for the generic clean-AOT lane.  The
link-map gate is stricter: a release map containing a forbidden symbol never
passes the absence proof, even if an allowlist record mentions that symbol.
"""

from __future__ import annotations

import hashlib
import json
import os
import posixpath
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


ALLOWLIST_FORMAT = "psxrecomp-cleanroom-allowlist-v1"
SOURCE_RECEIPT_FORMAT = "psxrecomp-cleanroom-source-receipt-v2"
LINK_MAP_RECEIPT_FORMAT = "psxrecomp-cleanroom-link-map-receipt-v2"

SOURCE_RULES = frozenset({"title_name", "serial", "guest_address", "shipping_symbol"})

# These names are the known title contamination boundary for the generic lane.
# Short project aliases are intentionally conservative: a generic use of a
# token such as ``w3`` needs a narrow reviewed exception rather than leaving a
# trivial title-policy bypass in the admission gate.
_TITLE_PATTERNS: tuple[tuple[str, str, str], ...] = (
    ("title_name", "title.wipeout", r"\bwip[e3]out(?:[-_ ]*3)?(?:[-_ ]*se)?\b"),
    ("title_name", "title.wo3_alias", r"(?<![A-Za-z0-9])wo3(?:[-_ ]*se)?(?![A-Za-z0-9])"),
    ("title_name", "title.w3_alias", r"(?<![A-Za-z0-9])w3(?:[-_ ]*se)?(?![A-Za-z0-9])"),
    ("title_name", "title.mega_man_x", r"\bmega[\s_-]+man[\s_-]+x[\s_-]*[56]\b"),
    ("title_name", "title.mmx", r"\bmmx[56]\b"),
    ("title_name", "title.tomba", r"\btomba(?:[\s_-]*[12])?\b"),
    ("title_name", "title.ape_escape", r"\bape[\s_-]+escape\b"),
    ("title_name", "title.crash_bash", r"\bcrash[\s_-]+bash\b"),
    ("serial", "serial.sces_02845", r"(?<![A-Za-z0-9])SCES[-_ .]*0?28[-_ .]*45(?![A-Za-z0-9])"),
)
_TEXT_RULES = tuple(
    (category, rule_id, re.compile(pattern, re.IGNORECASE))
    for category, rule_id, pattern in _TITLE_PATTERNS
)

# The address rules cover the PS1's normal kernel/user RAM aliases, BIOS,
# scratchpad, cache control, and hardware registers.  C/C++ digit separators,
# Rust/Python underscores, and decimal spellings are normalized before range
# classification so formatting cannot evade the gate.
_HEX_LITERAL_RE = re.compile(
    r"(?<![0-9A-Za-z_])(?P<literal>0[xX](?P<digits>"
    r"[0-9A-Fa-f](?:[0-9A-Fa-f]|['_](?=[0-9A-Fa-f]))*))"
    r"(?P<suffix>(?:[A-Za-z_][A-Za-z0-9_]*)?)(?![0-9A-Za-z_'])"
)
_DECIMAL_LITERAL_RE = re.compile(
    r"(?<![0-9A-Za-z_])(?P<literal>(?P<digits>[0-9](?:[0-9]|['_](?=[0-9]))*))"
    r"(?P<suffix>(?:[A-Za-z_][A-Za-z0-9_]*)?)(?![0-9A-Za-z_'])"
)
_ADDRESS_RANGES: tuple[tuple[str, int, int], ...] = (
    ("ram_kseg0", 0x80000000, 0x801FFFFF),
    ("ram_kseg1", 0xA0000000, 0xA01FFFFF),
    ("scratchpad", 0x1F800000, 0x1F8003FF),
    ("bios", 0xBFC00000, 0xBFC7FFFF),
    ("bios_physical", 0x1FC00000, 0x1FC7FFFF),
    ("mmio", 0x1F801000, 0x1F802FFF),
    ("cache_control", 0xFFFE0130, 0xFFFE0130),
)

# These are shipping-link identities, not a general ban on words such as
# "debug" or "compile".  Longer names precede shorter aliases so a single
# occurrence has one stable finding.
_COMMON_FORBIDDEN_SYMBOLS: tuple[tuple[str, str, str], ...] = (
    ("dirty_interpreter", "shipping.dirty_interpreter", "dirty_ram_dispatch_inner"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "dirty_ram_dispatch"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "dirty_ram_interp"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "dirty_ram_execute"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "psx_dirty_ram"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "psx_unknown_dispatch"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "unknown_dispatch"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "interpreter_dispatch"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "execute_interpreter"),
    ("runtime_compiler", "shipping.runtime_compiler", "psx_runtime_compiler"),
    ("runtime_compiler", "shipping.runtime_compiler", "psx_runtime_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "runtime_compiler"),
    ("runtime_compiler", "shipping.runtime_compiler", "runtime_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "autocompile"),
    ("runtime_compiler", "shipping.runtime_compiler", "auto_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "compile_overlays"),
    ("runtime_compiler", "shipping.runtime_compiler", "compile_overlay"),
    ("runtime_compiler", "shipping.runtime_compiler", "overlay_loader"),
    ("runtime_compiler", "shipping.runtime_compiler", "psx_compile_overlay"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "gcc_compile"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "beetle_debug_server"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "duckstation"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "debug_server"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "debug_client"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "debug_command"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "freeze_heartbeat"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "fntrace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "trace_ring"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "wtrace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "event_trace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "parity_trace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "device_trace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "audio_trace"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "overlay_capture"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "cosim"),
    ("debug_oracle_trace", "shipping.debug_oracle_trace", "oracle"),
)

# These identities are valid only in offline/build/test components.  They are
# therefore link-map violations but not generic-source violations.
_LINK_ONLY_FORBIDDEN_SYMBOLS: tuple[tuple[str, str, str], ...] = (
    ("dirty_interpreter", "shipping.dirty_interpreter", "interpreter_fallback"),
    ("dirty_interpreter", "shipping.dirty_interpreter", "reference_interpreter"),
    ("runtime_compiler", "shipping.runtime_compiler", "runtime_compilation"),
    ("runtime_compiler", "shipping.runtime_compiler", "runtime_codegen"),
    ("runtime_compiler", "shipping.runtime_compiler", "jit_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "dynarec"),
    ("runtime_compiler", "shipping.runtime_compiler", "capture_compiler"),
    ("runtime_compiler", "shipping.runtime_compiler", "capture_and_compile"),
    ("runtime_compiler", "shipping.runtime_compiler", "clean_aot_compiler"),
    ("runtime_compiler", "shipping.runtime_compiler", "libtcc"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_new"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_delete"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_add_file"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_compile_string"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_relocate"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_get_symbol"),
    ("runtime_compiler", "shipping.runtime_compiler", "tcc_run"),
    ("compatibility_dispatch", "shipping.compatibility_dispatch", "compatibility_dispatch"),
    ("compatibility_dispatch", "shipping.compatibility_dispatch", "compat_dispatch"),
    (
        "debug_oracle_trace",
        "shipping.debug_oracle_trace",
        "r3000a_semantic_reference_oracle",
    ),
)

_LINK_FORBIDDEN_SYMBOLS = _COMMON_FORBIDDEN_SYMBOLS + _LINK_ONLY_FORBIDDEN_SYMBOLS
_SOURCE_FORBIDDEN_SYMBOLS = tuple(
    item for item in _COMMON_FORBIDDEN_SYMBOLS if item[2] != "oracle"
)
_SYMBOL_INFO = {
    symbol: (category, rule_id)
    for category, rule_id, symbol in _LINK_FORBIDDEN_SYMBOLS
}


def _compile_symbol_pattern(
    symbols: Sequence[tuple[str, str, str]],
) -> re.Pattern[str]:
    longest_first = sorted(
        (item[2] for item in symbols), key=lambda item: (-len(item), item)
    )
    return re.compile(
        r"(?<![A-Za-z0-9])(?P<symbol>"
        + "|".join(re.escape(symbol) for symbol in longest_first)
        + r")(?![A-Za-z0-9])"
    )


_SOURCE_SYMBOL_RE = _compile_symbol_pattern(_SOURCE_FORBIDDEN_SYMBOLS)
_LINK_SYMBOL_RE = _compile_symbol_pattern(_LINK_FORBIDDEN_SYMBOLS)
_MANGLED_TOKEN_RE = re.compile(r"(?<![A-Za-z0-9])_Z[A-Za-z0-9_]+")
_ALWAYS_IGNORED_SOURCE_DIRECTORIES = frozenset({"__pycache__"})


class GateError(ValueError):
    """Raised for malformed inputs or an invalid allowlist."""


def _is_link_like(path: Path) -> bool:
    """Return true for symlinks and Windows directory junctions."""

    try:
        if path.is_symlink():
            return True
        is_junction = getattr(path, "is_junction", None)
        return bool(is_junction is not None and is_junction())
    except OSError as error:
        raise GateError(f"cannot inspect filesystem entry {path}: {error}") from error


def _reject_link_like(path: Path, kind: str) -> None:
    if _is_link_like(path):
        raise GateError(f"{kind} must not be a symlink or junction: {path}")


def _is_generated_build_directory(path: Path) -> bool:
    name = path.name.casefold()
    looks_like_build = (
        name == "build"
        or name == "out"
        or name.startswith(".build-")
        or name.startswith("build-")
        or name.startswith("cmake-build-")
    )
    if not looks_like_build:
        return False
    for marker_name in ("CMakeCache.txt", "build.ninja"):
        marker = path / marker_name
        if marker.exists():
            _reject_link_like(marker, "generated build marker")
            if marker.is_file():
                return True
    return False


def _read_utf8_text(path: Path, kind: str) -> tuple[bytes, str]:
    try:
        payload = path.read_bytes()
        text = payload.decode("utf-8-sig")
    except (OSError, UnicodeError) as error:
        raise GateError(f"cannot read UTF-8 {kind} {path}: {error}") from error
    bad_control = next(
        (
            character
            for character in text
            if (ord(character) < 0x20 and character not in "\t\n\r\f")
            or ord(character) == 0x7F
        ),
        None,
    )
    if bad_control is not None:
        raise GateError(
            f"{kind} contains a forbidden control byte U+{ord(bad_control):04X}: {path}"
        )
    return payload, text


def _read_json(path: Path) -> tuple[Any, bytes]:
    def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise GateError(f"duplicate JSON key {key!r} in {path}")
            result[key] = value
        return result

    _reject_link_like(path, "allowlist")
    payload, text = _read_utf8_text(path, "JSON")
    try:
        return json.loads(text, object_pairs_hook=reject_duplicate_keys), payload
    except json.JSONDecodeError as error:
        raise GateError(f"cannot read JSON {path}: {error}") from error


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _normalise_allowlist_path(value: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise GateError("allowlist path must be a non-empty string")
    if value != value.strip():
        raise GateError("allowlist paths cannot have leading or trailing whitespace")
    candidate = value.replace("\\", "/")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        raise GateError("allowlist paths cannot contain control characters")
    if candidate.startswith("/") or re.match(r"^[A-Za-z]:", candidate):
        raise GateError(f"allowlist paths must be relative: {value!r}")
    if any(marker in candidate for marker in ("*", "?", "[", "]")):
        raise GateError(f"allowlist paths cannot contain glob markers: {value!r}")
    normalised = posixpath.normpath(candidate)
    if normalised in {"", ".", ".."} or normalised.startswith("../"):
        raise GateError(f"allowlist path escapes its root: {value!r}")
    return normalised


def _resolve_root(root: Path | None) -> Path | None:
    if root is None:
        return None
    try:
        resolved = root.resolve(strict=True)
    except OSError as error:
        raise GateError(f"root does not resolve: {root}: {error}") from error
    if not resolved.is_dir():
        raise GateError(f"root is not a directory: {root}")
    return resolved


def _resolved_under_root(path: Path, root: Path | None, kind: str) -> Path:
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise GateError(f"{kind} does not resolve: {path}: {error}") from error
    if root is not None:
        try:
            common = os.path.commonpath((os.fspath(root), os.fspath(resolved)))
        except ValueError as error:
            raise GateError(f"{kind} is outside root: {path}") from error
        if os.path.normcase(common) != os.path.normcase(os.fspath(root)):
            raise GateError(f"{kind} is outside root: {path}")
    return resolved


def _display_path(path: Path, root: Path | None, kind: str = "input") -> str:
    if root is not None:
        resolved = _resolved_under_root(path, root, kind)
        value = os.path.relpath(resolved, root)
    else:
        value = os.fspath(path)
    return value.replace("\\", "/")


def _path_equal(left: str, right: str) -> bool:
    return left == right


def _literal_equal(left: str, right: str) -> bool:
    return left == right


@dataclass(frozen=True)
class Allowlist:
    """Validated exact-match exceptions from the v1 allowlist format."""

    source: tuple[Mapping[str, Any], ...] = ()
    link_map: tuple[Mapping[str, Any], ...] = ()
    path: Path | None = None
    sha256: str | None = None

    @property
    def entries(self) -> int:
        return len(self.source) + len(self.link_map)

    def matching_indexes(
        self,
        records: Sequence[Mapping[str, Any]],
        finding: Mapping[str, Any],
    ) -> tuple[int, ...]:
        matches: list[int] = []
        for index, record in enumerate(records):
            if record["rule"] != finding["rule"]:
                continue
            if not _path_equal(record["path"], finding["path"]):
                continue
            if record["line"] != finding["line"]:
                continue
            if not _literal_equal(record["match"], finding["match"]):
                continue
            if "column" in record and record["column"] != finding["column"]:
                continue
            matches.append(index)
        return tuple(matches)


def _validate_record(record: Any, index: int, *, link_map: bool) -> dict[str, Any]:
    if not isinstance(record, dict):
        section = "link_map" if link_map else "source"
        raise GateError(f"allowlist {section}[{index}] must be an object")
    required = {"path", "line", "reason"}
    required.add("symbol" if link_map else "rule")
    required.add("symbol" if link_map else "match")
    allowed_keys = required | {"column"}
    unknown = set(record) - allowed_keys
    if unknown:
        raise GateError(f"allowlist record {index} has unknown keys: {sorted(unknown)}")
    missing = required - set(record)
    if missing:
        raise GateError(f"allowlist record {index} is missing: {sorted(missing)}")

    result = dict(record)
    result["path"] = _normalise_allowlist_path(result["path"])
    if (
        isinstance(result["line"], bool)
        or not isinstance(result["line"], int)
        or result["line"] < 1
    ):
        raise GateError(f"allowlist record {index} line must be a positive integer")
    if "column" in result:
        if (
            isinstance(result["column"], bool)
            or not isinstance(result["column"], int)
            or result["column"] < 1
        ):
            raise GateError(f"allowlist record {index} column must be a positive integer")
    if not isinstance(result["reason"], str) or not result["reason"].strip():
        raise GateError(f"allowlist record {index} reason must be non-empty")

    if link_map:
        if not isinstance(result["symbol"], str) or result["symbol"] not in _SYMBOL_INFO:
            raise GateError(f"allowlist link_map[{index}] symbol is not a known forbidden symbol")
        result["rule"] = "shipping_symbol"
        result["match"] = result.pop("symbol")
    else:
        if not isinstance(result["rule"], str) or result["rule"] not in SOURCE_RULES:
            raise GateError(f"allowlist source[{index}] has unknown rule {result['rule']!r}")
        if not isinstance(result["match"], str) or not result["match"]:
            raise GateError(f"allowlist source[{index}] match must be non-empty")
        if any(ord(character) < 0x20 or ord(character) == 0x7F for character in result["match"]):
            raise GateError(f"allowlist source[{index}] match contains control characters")
    return result


def load_allowlist(path: Path | None) -> Allowlist:
    if path is None:
        return Allowlist()
    data, payload = _read_json(path)
    if not isinstance(data, dict):
        raise GateError("allowlist root must be an object")
    expected_keys = {"format", "source", "link_map"}
    unknown = set(data) - expected_keys
    if unknown:
        raise GateError(f"allowlist has unknown keys: {sorted(unknown)}")
    if data.get("format") != ALLOWLIST_FORMAT:
        raise GateError(f"allowlist format must be {ALLOWLIST_FORMAT!r}")
    source = data.get("source", [])
    link_map = data.get("link_map", [])
    if not isinstance(source, list) or not isinstance(link_map, list):
        raise GateError("allowlist source and link_map must be arrays")

    source_records = tuple(
        _validate_record(item, index, link_map=False)
        for index, item in enumerate(source)
    )
    link_records = tuple(
        _validate_record(item, index, link_map=True)
        for index, item in enumerate(link_map)
    )
    for section_name, records in (("source", source_records), ("link_map", link_records)):
        seen: set[tuple[Any, ...]] = set()
        for item in records:
            selector = (
                item["rule"],
                item["path"],
                item["line"],
                item["match"],
                item.get("column"),
            )
            if selector in seen:
                raise GateError(
                    f"allowlist {section_name} contains duplicate exception selectors"
                )
            seen.add(selector)
    return Allowlist(source_records, link_records, path, _sha256_bytes(payload))


def _iter_source_files(inputs: Sequence[Path], root: Path | None) -> list[Path]:
    if not inputs:
        raise GateError("at least one source path is required")
    candidates: dict[str, Path] = {}

    def add_candidate(candidate: Path) -> None:
        _reject_link_like(candidate, "source file")
        try:
            is_file = candidate.is_file()
        except OSError as error:
            raise GateError(f"cannot inspect source file {candidate}: {error}") from error
        if not is_file:
            raise GateError(f"source entry is not a regular file: {candidate}")
        resolved = _resolved_under_root(candidate, root, "source file")
        candidates[os.path.normcase(os.fspath(resolved))] = candidate

    def walk_error(error: OSError) -> None:
        raise GateError(f"cannot traverse source tree: {error}") from error

    for input_path in inputs:
        if not input_path.exists():
            raise GateError(f"source path does not exist: {input_path}")
        _reject_link_like(input_path, "source path")
        _resolved_under_root(input_path, root, "source path")
        if input_path.is_file():
            add_candidate(input_path)
            continue
        if not input_path.is_dir():
            raise GateError(f"source path is neither a file nor directory: {input_path}")
        for directory, directory_names, file_names in os.walk(
            input_path,
            topdown=True,
            onerror=walk_error,
            followlinks=False,
        ):
            directory_path = Path(directory)
            directory_names.sort(key=str.casefold)
            file_names.sort(key=str.casefold)
            retained_directories: list[str] = []
            for name in directory_names:
                child = directory_path / name
                if name == ".git":
                    continue
                _reject_link_like(child, "source directory")
                if (
                    name in _ALWAYS_IGNORED_SOURCE_DIRECTORIES
                    or _is_generated_build_directory(child)
                ):
                    continue
                _resolved_under_root(child, root, "source directory")
                retained_directories.append(name)
            directory_names[:] = retained_directories
            for name in file_names:
                if name == ".git":
                    continue
                add_candidate(directory_path / name)
    if not candidates:
        raise GateError("source inputs contain no regular files")
    return sorted(
        candidates.values(),
        key=lambda item: (
            _display_path(item, root, "source file").casefold(),
            _display_path(item, root, "source file"),
        ),
    )


def _address_classification(value: int) -> str | None:
    for classification, lower, upper in _ADDRESS_RANGES:
        if lower <= value <= upper:
            return classification
    return None


def _address_hits(line: str) -> list[tuple[int, str, str]]:
    """Return `(column, literal, classification)` address findings."""

    hits: list[tuple[int, str, str]] = []
    for pattern, base, maximum_digits in (
        (_HEX_LITERAL_RE, 16, 16),
        (_DECIMAL_LITERAL_RE, 10, 20),
    ):
        for match in pattern.finditer(line):
            digits = match.group("digits").replace("'", "").replace("_", "")
            significant = digits.lstrip("0") or "0"
            if len(significant) > maximum_digits:
                continue
            value = int(digits, base)
            classification = _address_classification(value)
            if classification is not None:
                hits.append((match.start("literal") + 1, match.group("literal"), classification))
    hits.sort(key=lambda item: (item[0], item[1]))
    return hits


def _make_finding(
    *,
    rule: str,
    rule_id: str,
    path: str,
    line: int,
    column: int,
    match: str,
    classification: str | None = None,
) -> dict[str, Any]:
    finding: dict[str, Any] = {
        "column": column,
        "line": line,
        "match": match,
        "path": path,
        "rule": rule,
        "rule_id": rule_id,
    }
    if classification is not None:
        finding["classification"] = classification
    return finding


def _source_findings(text: str, display_path: str) -> list[dict[str, Any]]:
    findings: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        for rule, rule_id, pattern in _TEXT_RULES:
            for match in pattern.finditer(line):
                findings.append(
                    _make_finding(
                        rule=rule,
                        rule_id=rule_id,
                        path=display_path,
                        line=line_number,
                        column=match.start() + 1,
                        match=match.group(0),
                    )
                )
        for column, literal, classification in _address_hits(line):
            findings.append(
                _make_finding(
                    rule="guest_address",
                    rule_id=f"guest_address.{classification}",
                    path=display_path,
                    line=line_number,
                    column=column,
                    match=literal,
                    classification=classification,
                )
            )
        for column, symbol in _symbol_hits(line, link_map=False):
            category, rule_id = _SYMBOL_INFO[symbol]
            findings.append(
                _make_finding(
                    rule="shipping_symbol",
                    rule_id=rule_id,
                    path=display_path,
                    line=line_number,
                    column=column,
                    match=symbol,
                    classification=category,
                )
            )
    return findings


def _symbol_hits(line: str, *, link_map: bool) -> list[tuple[int, str]]:
    """Return stable `(one_based_column, symbol)` hits from one source line.

    Link maps may print Itanium names such as `_Z18psx_unknown_dispatchv`
    or `_ZN3psx12debug_serverEv`.  The forbidden symbol is still
    unambiguously present in that mangled identity, so the link-map mode also
    searches inside `_Z...` tokens.  Source mode remains
    identifier-boundary based.
    """

    hits: list[tuple[int, int, str]] = []
    symbol_pattern = _LINK_SYMBOL_RE if link_map else _SOURCE_SYMBOL_RE
    symbols = _LINK_FORBIDDEN_SYMBOLS if link_map else _SOURCE_FORBIDDEN_SYMBOLS
    for match in symbol_pattern.finditer(line):
        hits.append((match.start(), match.end(), match.group("symbol")))

    if link_map:
        for mangled in _MANGLED_TOKEN_RE.finditer(line):
            for _category, _rule_id, symbol in symbols:
                start = line.find(symbol, mangled.start(), mangled.end())
                if start < 0:
                    continue
                end = start + len(symbol)
                if not any(
                    start < existing_end and end > existing_start
                    for existing_start, existing_end, _ in hits
                ):
                    hits.append((start, end, symbol))

    hits.sort(key=lambda item: (item[0], -(item[1] - item[0]), item[2]))
    selected: list[tuple[int, str]] = []
    occupied: list[tuple[int, int]] = []
    for start, end, symbol in hits:
        if any(
            start < existing_end and end > existing_start
            for existing_start, existing_end in occupied
        ):
            continue
        selected.append((start + 1, symbol))
        occupied.append((start, end))
    return selected


def _finding_sort_key(finding: Mapping[str, Any]) -> tuple[Any, ...]:
    return (
        str(finding["path"]).casefold(),
        int(finding["line"]),
        int(finding["column"]),
        str(finding["rule"]),
        str(finding["match"]).casefold(),
        str(finding["rule_id"]),
    )


def _allowlist_receipt(allowlist: Allowlist, root: Path | None) -> dict[str, Any]:
    result: dict[str, Any] = {"entries": allowlist.entries}
    if allowlist.path is not None:
        _reject_link_like(allowlist.path, "allowlist")
        result["path"] = _display_path(allowlist.path, root, "allowlist")
        result["sha256"] = allowlist.sha256
    return result


def _annotate(
    findings: Iterable[dict[str, Any]],
    allowlist: Allowlist,
    *,
    link_map: bool,
) -> tuple[list[dict[str, Any]], int, int]:
    records = allowlist.link_map if link_map else allowlist.source
    section_name = "link_map" if link_map else "source"
    usage = [0] * len(records)
    annotated: list[dict[str, Any]] = []
    allowed_count = 0
    for finding in findings:
        matching_indexes = allowlist.matching_indexes(records, finding)
        if len(matching_indexes) > 1:
            raise GateError(
                f"multiple allowlist {section_name} records match "
                f"{finding['path']}:{finding['line']}:{finding['column']}"
            )
        allowed = bool(matching_indexes)
        if allowed:
            index = matching_indexes[0]
            usage[index] += 1
            if usage[index] > 1:
                raise GateError(
                    f"allowlist {section_name}[{index}] matches multiple findings; "
                    "add an exact column"
                )
        item = dict(finding)
        item["allowed"] = allowed
        annotated.append(item)
        allowed_count += int(allowed)
    unused = [index for index, count in enumerate(usage) if count == 0]
    if unused:
        raise GateError(f"unused allowlist {section_name} records: {unused}")
    return annotated, allowed_count, len(annotated) - allowed_count


def scan_source(
    paths: Sequence[Path],
    *,
    root: Path | None = None,
    allowlist: Allowlist | None = None,
    allowlist_path: Path | None = None,
) -> dict[str, Any]:
    """Scan source files and return a deterministic, JSON-serializable receipt."""

    if allowlist is not None and allowlist_path is not None:
        raise GateError("pass allowlist or allowlist_path, not both")
    resolved_root = _resolve_root(root)
    effective_allowlist = allowlist if allowlist is not None else load_allowlist(allowlist_path)
    files = _iter_source_files(paths, resolved_root)
    file_receipts: list[dict[str, Any]] = []
    findings: list[dict[str, Any]] = []
    for path in files:
        _reject_link_like(path, "source file")
        _resolved_under_root(path, resolved_root, "source file")
        payload, text = _read_utf8_text(path, "source")
        display = _display_path(path, resolved_root, "source file")
        current = _source_findings(text, display)
        findings.extend(current)
        file_receipts.append(
            {
                "bytes": len(payload),
                "findings": len(current),
                "path": display,
                "sha256": _sha256_bytes(payload),
            }
        )

    findings.sort(key=_finding_sort_key)
    annotated, allowed_count, violation_count = _annotate(
        findings, effective_allowlist, link_map=False
    )
    return {
        "allowlist": _allowlist_receipt(effective_allowlist, resolved_root),
        "files": file_receipts,
        "findings": annotated,
        "format": SOURCE_RECEIPT_FORMAT,
        "status": "pass" if violation_count == 0 else "fail",
        "summary": {
            "allowlisted": allowed_count,
            "files": len(file_receipts),
            "matches": len(annotated),
            "violations": violation_count,
        },
    }


def _detect_link_map_dialect(text: str) -> str:
    if not text.strip():
        raise GateError("link map is empty")
    if (
        "Memory Configuration" in text
        and "Linker script and memory map" in text
        and re.search(r"(?m)^\s*\.[A-Za-z0-9_.$-]+\s+0x[0-9A-Fa-f]+", text)
    ):
        return "gnu-ld"
    if (
        re.search(
            r"(?m)^\s*(?:VMA\s+LMA\s+|Address\s+)Size\s+Align\s+Out\s+In\s+Symbol\s*$",
            text,
        )
        and re.search(
            r"(?m)^\s*(?:0x)?[0-9A-Fa-f]{4,16}\s+.*\.(?:text|data|rdata|rodata|bss)(?:\s|$)",
            text,
        )
    ):
        return "llvm-lld"
    if (
        re.search(r"(?m)^\s*Start\s+Length\s+Name\s+Class\s*$", text)
        and re.search(
            r"(?m)^\s*Address\s+Publics by Value\s+Rva\+Base\s+Lib:Object\s*$",
            text,
        )
        and re.search(
            r"(?m)^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+[0-9A-Fa-f]+H?\s+\S+\s+(?:CODE|DATA)\s*$",
            text,
        )
    ):
        return "msvc-link"
    if (
        all(marker in text for marker in ("# Path:", "# Arch:", "# Sections:", "# Symbols:"))
        and re.search(
            r"(?m)^0x[0-9A-Fa-f]+\s+0x[0-9A-Fa-f]+\s+__\S+\s+__\S+\s*$",
            text,
        )
    ):
        return "apple-ld64"
    raise GateError("unrecognized or incomplete GNU/LLVM/MSVC/Apple link map")


def scan_link_map(
    path: Path,
    *,
    root: Path | None = None,
    allowlist: Allowlist | None = None,
    allowlist_path: Path | None = None,
) -> dict[str, Any]:
    """Scan a linker map and return a strict forbidden-symbol absence proof."""

    if allowlist is not None and allowlist_path is not None:
        raise GateError("pass allowlist or allowlist_path, not both")
    resolved_root = _resolve_root(root)
    effective_allowlist = allowlist if allowlist is not None else load_allowlist(allowlist_path)
    _reject_link_like(path, "link map")
    resolved_path = _resolved_under_root(path, resolved_root, "link map")
    if not resolved_path.is_file():
        raise GateError(f"link map is not a regular file: {path}")
    payload, text = _read_utf8_text(path, "link map")
    dialect = _detect_link_map_dialect(text)

    display = _display_path(path, resolved_root, "link map")
    findings: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        for column, symbol in _symbol_hits(line, link_map=True):
            category, rule_id = _SYMBOL_INFO[symbol]
            findings.append(
                _make_finding(
                    rule="shipping_symbol",
                    rule_id=rule_id,
                    path=display,
                    line=line_number,
                    column=column,
                    match=symbol,
                    classification=category,
                )
            )
    findings.sort(key=_finding_sort_key)
    annotated, allowed_count, unallowlisted_count = _annotate(
        findings, effective_allowlist, link_map=True
    )
    # A release link map is an absence proof.  An allowlisted hit remains a
    # real linked symbol and therefore cannot make this gate pass.
    absent = not findings
    return {
        "allowlist": _allowlist_receipt(effective_allowlist, resolved_root),
        "forbidden_symbols_absent": absent,
        "findings": annotated,
        "format": LINK_MAP_RECEIPT_FORMAT,
        "link_map": {
            "bytes": len(payload),
            "dialect": dialect,
            "lines": len(text.splitlines()),
            "path": display,
            "sha256": _sha256_bytes(payload),
        },
        "status": "pass" if absent else "fail",
        "summary": {
            "allowlisted": allowed_count,
            "matches": len(findings),
            "unallowlisted": unallowlisted_count,
        },
    }


def compact_json(payload: Mapping[str, Any]) -> str:
    """Serialize a receipt without whitespace or machine-specific metadata."""

    return json.dumps(payload, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def write_receipt(path: Path, payload: Mapping[str, Any]) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(compact_json(payload) + "\n")
    except FileExistsError as error:
        raise GateError(f"refusing to overwrite receipt: {path}") from error
    except OSError as error:
        raise GateError(f"cannot write receipt {path}: {error}") from error
