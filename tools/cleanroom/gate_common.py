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
SOURCE_RECEIPT_FORMAT = "psxrecomp-cleanroom-source-receipt-v1"
LINK_MAP_RECEIPT_FORMAT = "psxrecomp-cleanroom-link-map-receipt-v1"

SOURCE_RULES = frozenset({"title_name", "serial", "guest_address", "shipping_symbol"})

# These names are the known title contamination boundary for the generic lane.
# The patterns are deliberately explicit.  A new title must be added as a
# reviewed rule, rather than smuggled in through a broad substring heuristic.
_TITLE_PATTERNS: tuple[tuple[str, str, str], ...] = (
    ("title_name", "title.wipeout", r"\bwip[e3]out(?:[-_ ]*3)?(?:[-_ ]*se)?\b"),
    ("title_name", "title.mega_man_x", r"\bmega[\s_-]+man[\s_-]+x[\s_-]*[56]\b"),
    ("title_name", "title.mmx", r"\bmmx[56]\b"),
    ("title_name", "title.tomba", r"\btomba(?:[\s_-]*[12])?\b"),
    ("title_name", "title.ape_escape", r"\bape[\s_-]+escape\b"),
    ("title_name", "title.crash_bash", r"\bcrash[\s_-]+bash\b"),
    ("serial", "serial.sces_02845", r"\bSCES[-_ ]?02845\b"),
    ("serial", "serial.sces_028_45", r"\bSCES[-_ ]?028\.45\b"),
)
_TEXT_RULES = tuple(
    (category, rule_id, re.compile(pattern, re.IGNORECASE))
    for category, rule_id, pattern in _TITLE_PATTERNS
)

# The address rules cover the PS1's normal kernel/user RAM aliases, BIOS, and
# the hardware register window.  Only eight-digit hexadecimal constants are
# considered, so ordinary small offsets and host pointers are not promoted to
# findings by accident.
_ADDRESS_RE = re.compile(r"(?<![0-9A-Fa-f])0[xX]([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
_ADDRESS_RANGES: tuple[tuple[str, int, int], ...] = (
    ("ram_kseg0", 0x80000000, 0x801FFFFF),
    ("ram_kseg1", 0xA0000000, 0xA01FFFFF),
    ("bios", 0xBFC00000, 0xBFC7FFFF),
    ("mmio", 0x1F801000, 0x1F802FFF),
)

# These are shipping-link identities, not a general ban on words such as
# "debug" or "compile".  Longer names precede shorter aliases so a single
# occurrence has one stable finding.
_FORBIDDEN_SYMBOLS: tuple[tuple[str, str, str], ...] = (
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
_SYMBOL_INFO = {symbol: (category, rule_id) for category, rule_id, symbol in _FORBIDDEN_SYMBOLS}
_SYMBOL_RE = re.compile(
    r"(?<![A-Za-z0-9])(?P<symbol>"
    + "|".join(re.escape(item[2]) for item in _FORBIDDEN_SYMBOLS)
    + r")(?![A-Za-z0-9])"
)
_MANGLED_TOKEN_RE = re.compile(r"(?<![A-Za-z0-9])_Z[A-Za-z0-9_]+")

_SOURCE_SUFFIXES = frozenset(
    {
        ".c",
        ".cc",
        ".cpp",
        ".cxx",
        ".h",
        ".hh",
        ".hpp",
        ".hxx",
        ".m",
        ".mm",
        ".s",
        ".asm",
        ".inc",
        ".inl",
        ".rs",
        ".go",
        ".zig",
        ".py",
        ".java",
        ".cs",
        ".swift",
    }
)


class GateError(ValueError):
    """Raised for malformed inputs or an invalid allowlist."""


def _read_json(path: Path) -> Any:
    def reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise GateError(f"duplicate JSON key {key!r} in {path}")
            result[key] = value
        return result

    try:
        return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=reject_duplicate_keys)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GateError(f"cannot read JSON {path}: {error}") from error


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                size += len(chunk)
                digest.update(chunk)
    except OSError as error:
        raise GateError(f"cannot read {path}: {error}") from error
    return size, digest.hexdigest()


def _normalise_allowlist_path(value: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise GateError("allowlist path must be a non-empty string")
    candidate = value.strip().replace("\\", "/")
    if candidate.startswith("/") or re.match(r"^[A-Za-z]:($|/)", candidate):
        raise GateError(f"allowlist paths must be relative: {value!r}")
    if any(marker in candidate for marker in ("*", "?", "[", "]")):
        raise GateError(f"allowlist paths cannot contain glob markers: {value!r}")
    normalised = posixpath.normpath(candidate)
    if normalised in {"", ".", ".."} or normalised.startswith("../"):
        raise GateError(f"allowlist path escapes its root: {value!r}")
    return normalised


def _display_path(path: Path, root: Path | None) -> str:
    if root is not None:
        try:
            value = os.path.relpath(path.resolve(), root.resolve())
        except (OSError, ValueError):
            value = os.fspath(path)
    else:
        value = os.fspath(path)
    return value.replace("\\", "/")


def _path_equal(left: str, right: str) -> bool:
    if os.name == "nt":
        return left.casefold() == right.casefold()
    return left == right


def _literal_equal(left: str, right: str, rule: str) -> bool:
    if rule in {"title_name", "serial"}:
        return left.casefold() == right.casefold()
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

    def _matches(self, records: Iterable[Mapping[str, Any]], finding: Mapping[str, Any]) -> bool:
        for record in records:
            if record["rule"] != finding["rule"]:
                continue
            if not _path_equal(record["path"], finding["path"]):
                continue
            if record["line"] != finding["line"]:
                continue
            if not _literal_equal(record["match"], finding["match"], finding["rule"]):
                continue
            if "column" in record and record["column"] != finding["column"]:
                continue
            return True
        return False

    def allows_source(self, finding: Mapping[str, Any]) -> bool:
        return self._matches(self.source, finding)

    def allows_link_map(self, finding: Mapping[str, Any]) -> bool:
        return self._matches(self.link_map, finding)


def _validate_record(record: Any, index: int, *, link_map: bool) -> dict[str, Any]:
    if not isinstance(record, dict):
        raise GateError(f"allowlist {'link_map' if link_map else 'source'}[{index}] must be an object")
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
    if isinstance(result["line"], bool) or not isinstance(result["line"], int) or result["line"] < 1:
        raise GateError(f"allowlist record {index} line must be a positive integer")
    if "column" in result:
        if isinstance(result["column"], bool) or not isinstance(result["column"], int) or result["column"] < 1:
            raise GateError(f"allowlist record {index} column must be a positive integer")
    if not isinstance(result["reason"], str) or not result["reason"].strip():
        raise GateError(f"allowlist record {index} reason must be non-empty")

    if link_map:
        if not isinstance(result["symbol"], str) or result["symbol"] not in _SYMBOL_INFO:
            raise GateError(f"allowlist link_map[{index}] symbol is not a known forbidden symbol")
        result["rule"] = "shipping_symbol"
        result["match"] = result.pop("symbol")
    else:
        if result["rule"] not in SOURCE_RULES:
            raise GateError(f"allowlist source[{index}] has unknown rule {result['rule']!r}")
        if not isinstance(result["match"], str) or not result["match"]:
            raise GateError(f"allowlist source[{index}] match must be non-empty")
    return result


def load_allowlist(path: Path | None) -> Allowlist:
    if path is None:
        return Allowlist()
    data = _read_json(path)
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

    source_records = tuple(_validate_record(item, index, link_map=False) for index, item in enumerate(source))
    link_records = tuple(_validate_record(item, index, link_map=True) for index, item in enumerate(link_map))
    seen: set[str] = set()
    for item in (*source_records, *link_records):
        identity = json.dumps(item, sort_keys=True, separators=(",", ":"))
        if identity in seen:
            raise GateError("allowlist contains duplicate exact exception records")
        seen.add(identity)

    try:
        payload = path.read_bytes()
    except OSError as error:
        raise GateError(f"cannot hash allowlist {path}: {error}") from error
    return Allowlist(source_records, link_records, path, _sha256_bytes(payload))


def _iter_source_files(inputs: Sequence[Path]) -> list[Path]:
    if not inputs:
        raise GateError("at least one source path is required")
    candidates: dict[str, Path] = {}
    for input_path in inputs:
        if not input_path.exists():
            raise GateError(f"source path does not exist: {input_path}")
        if input_path.is_file():
            candidates[os.fspath(input_path.resolve())] = input_path
            continue
        if not input_path.is_dir():
            raise GateError(f"source path is neither a file nor directory: {input_path}")
        for candidate in input_path.rglob("*"):
            if ".git" in candidate.parts or not candidate.is_file():
                continue
            if candidate.suffix.lower() in _SOURCE_SUFFIXES:
                candidates[os.fspath(candidate.resolve())] = candidate
    return sorted(candidates.values(), key=lambda item: os.fspath(item).replace("\\", "/").casefold())


def _address_classification(value: int) -> str | None:
    for classification, lower, upper in _ADDRESS_RANGES:
        if lower <= value <= upper:
            return classification
    return None


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
        for match in _ADDRESS_RE.finditer(line):
            value = int(match.group(1), 16)
            classification = _address_classification(value)
            if classification is not None:
                findings.append(
                    _make_finding(
                        rule="guest_address",
                        rule_id=f"guest_address.{classification}",
                        path=display_path,
                        line=line_number,
                        column=match.start() + 1,
                        match=match.group(0),
                        classification=classification,
                    )
                )
        for column, symbol in _symbol_hits(line, allow_mangled=False):
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


def _symbol_hits(line: str, *, allow_mangled: bool) -> list[tuple[int, str]]:
    """Return stable `(one_based_column, symbol)` hits from one source line.

    Link maps may print Itanium names such as `_Z18psx_unknown_dispatchv`
    or `_ZN3psx12debug_serverEv`.  The forbidden symbol is still
    unambiguously present in that mangled identity, so the link-map mode also
    searches inside `_Z...` tokens.  Source mode remains
    identifier-boundary based.
    """

    hits: list[tuple[int, int, str]] = []
    for match in _SYMBOL_RE.finditer(line):
        hits.append((match.start(), match.end(), match.group("symbol")))

    if allow_mangled:
        for mangled in _MANGLED_TOKEN_RE.finditer(line):
            for _category, _rule_id, symbol in _FORBIDDEN_SYMBOLS:
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
        if any(start < existing_end and end > existing_start for existing_start, existing_end in occupied):
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
        result["path"] = _display_path(allowlist.path, root)
        result["sha256"] = allowlist.sha256
    return result


def _annotate(
    findings: Iterable[dict[str, Any]],
    allowlist: Allowlist,
    *,
    link_map: bool,
) -> tuple[list[dict[str, Any]], int, int]:
    annotated: list[dict[str, Any]] = []
    allowed_count = 0
    for finding in findings:
        allowed = allowlist.allows_link_map(finding) if link_map else allowlist.allows_source(finding)
        item = dict(finding)
        item["allowed"] = allowed
        annotated.append(item)
        allowed_count += int(allowed)
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
    effective_allowlist = allowlist if allowlist is not None else load_allowlist(allowlist_path)
    files = _iter_source_files(paths)
    file_receipts: list[dict[str, Any]] = []
    findings: list[dict[str, Any]] = []
    for path in files:
        try:
            payload = path.read_bytes()
            text = payload.decode("utf-8-sig")
        except (OSError, UnicodeError) as error:
            raise GateError(f"cannot read UTF-8 source {path}: {error}") from error
        display = _display_path(path, root)
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
    annotated, allowed_count, violation_count = _annotate(findings, effective_allowlist, link_map=False)
    return {
        "allowlist": _allowlist_receipt(effective_allowlist, root),
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
    effective_allowlist = allowlist if allowlist is not None else load_allowlist(allowlist_path)
    try:
        payload = path.read_bytes()
        text = payload.decode("utf-8-sig")
    except (OSError, UnicodeError) as error:
        raise GateError(f"cannot read UTF-8 link map {path}: {error}") from error

    display = _display_path(path, root)
    findings: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        for column, symbol in _symbol_hits(line, allow_mangled=True):
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
        "allowlist": _allowlist_receipt(effective_allowlist, root),
        "forbidden_symbols_absent": absent,
        "findings": annotated,
        "format": LINK_MAP_RECEIPT_FORMAT,
        "link_map": {
            "bytes": len(payload),
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
    if path.exists():
        raise GateError(f"refusing to overwrite receipt: {path}")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(compact_json(payload) + "\n", encoding="utf-8", newline="\n")
    except OSError as error:
        raise GateError(f"cannot write receipt {path}: {error}") from error
