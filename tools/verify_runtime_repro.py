"""Audit one running WipEout runtime and its isolated diagnostic paths.

This is deliberately a read-only guard.  It never starts, stops, or deletes a
process.  The live mode only asks Windows for the current process table; the
snapshot mode makes the same checks deterministic in CI and in audit tests.
"""

from __future__ import annotations

import argparse
import json
import ntpath
import re
import subprocess
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

_OPTION_RE = re.compile(
    r"(?<!\S)(--(?:bios|memcard-dir))(?:=|\s+)"
    r"(?:\"([^\"]*)\"|'([^']*)'|([^\s]+))",
    re.IGNORECASE,
)
_NORMAL_EXIT_ORIGINS = {"sdl_window_close", "tcp_quit", "normal"}


@dataclass(frozen=True)
class ProcessRow:
    pid: int
    executable: str
    command_line: str
    creation_epoch: float | None = None


@dataclass
class AuditResult:
    errors: list[str]
    warnings: list[str]
    details: dict[str, Any]

    @property
    def ok(self) -> bool:
        return not self.errors


def _win_path(value: str | Path) -> str:
    """Normalize Windows paths even when tests run on a non-Windows host."""

    return ntpath.normcase(ntpath.normpath(str(value).replace("/", "\\")))


def _inside(path: str, root: str) -> bool:
    path_n = _win_path(path)
    root_n = _win_path(root).rstrip("\\")
    return path_n == root_n or path_n.startswith(root_n + "\\")


def _physical(path: Path) -> str:
    """Use the target path so junctions/symlinks cannot evade scope checks."""

    return str(path.resolve(strict=False))


def _value_from_option(command_line: str, option: str) -> str | None:
    for match in _OPTION_RE.finditer(command_line or ""):
        if match.group(1).lower() == option.lower():
            return next((value for value in match.groups()[1:] if value is not None), "")
    return None


def _epoch(value: Any) -> float | None:
    if value is None or value == "":
        return None
    if isinstance(value, (int, float)):
        return float(value)
    text = str(value).strip()
    try:
        return float(text)
    except ValueError:
        pass
    dotnet = re.fullmatch(r"/Date\((-?\d+)(?:[+-]\d+)?\)/", text)
    if dotnet:
        return int(dotnet.group(1)) / 1000.0
    # CIM_DATETIME: YYYYMMDDHHMMSS.ffffff(+/-)UUU
    cim = re.match(r"^(\d{14})(?:\.\d+)?([+-]\d{3})?$", text)
    if cim:
        offset = int(cim.group(2) or "+000")
        stamp = datetime.strptime(cim.group(1), "%Y%m%d%H%M%S").replace(
            tzinfo=timezone(timedelta(minutes=offset))
        )
        return stamp.timestamp()
    try:
        return datetime.fromisoformat(text.replace("Z", "+00:00")).timestamp()
    except ValueError:
        return None


def process_rows(records: Iterable[Mapping[str, Any]]) -> list[ProcessRow]:
    rows: list[ProcessRow] = []
    for record in records:
        executable = str(record.get("ExecutablePath", record.get("executable", "")) or "")
        command_line = str(record.get("CommandLine", record.get("command_line", "")) or "")
        try:
            pid = int(record.get("ProcessId", record.get("pid", 0)))
        except (TypeError, ValueError):
            pid = 0
        rows.append(ProcessRow(pid, executable, command_line, _epoch(record.get("CreationDate", record.get("creation_epoch")))))
    return rows


def load_snapshot(path: Path) -> list[ProcessRow]:
    with path.open(encoding="utf-8") as stream:
        payload = json.load(stream)
    records = payload.get("processes", payload) if isinstance(payload, dict) else payload
    if not isinstance(records, list):
        raise TypeError("process snapshot must be a list or an object with a processes list")
    return process_rows(records)


def collect_windows_processes() -> list[ProcessRow]:
    """Read the process table with CIM; this command has no process mutation."""

    command = (
        "$ErrorActionPreference='Stop'; "
        "Get-CimInstance Win32_Process | "
        "Select-Object ProcessId,ParentProcessId,ExecutablePath,CommandLine,CreationDate | "
        "ConvertTo-Json -Compress"
    )
    completed = subprocess.run(
        ["powershell", "-NoProfile", "-NonInteractive", "-Command", command],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    )
    payload = json.loads(completed.stdout or "[]")
    if isinstance(payload, dict):
        payload = [payload]
    return process_rows(payload)


def audit_processes(
    rows: Iterable[ProcessRow],
    executable: str | Path,
    bios: str | Path,
    memcard_dir: str | Path,
    *,
    expected_count: int = 1,
    run_start_epoch: float | None = None,
) -> AuditResult:
    errors: list[str] = []
    warnings: list[str] = []
    expected_exe = _win_path(executable)
    candidates = [row for row in rows if _win_path(row.executable) == expected_exe]
    details: dict[str, Any] = {
        "candidate_count": len(candidates),
        "expected_count": expected_count,
        "candidate_pids": [row.pid for row in candidates],
        "executable": str(executable),
    }
    if len(candidates) != expected_count:
        errors.append(f"expected exactly {expected_count} candidate process(es) for {executable}; found {len(candidates)}")
    expected_bios = _win_path(bios)
    expected_memcard = _win_path(memcard_dir)
    for row in candidates:
        if run_start_epoch is not None and (row.creation_epoch is None or row.creation_epoch < run_start_epoch):
            errors.append(f"candidate PID {row.pid} predates this run; stale process cleanup is not permitted")
        actual_bios = _value_from_option(row.command_line, "--bios")
        actual_memcard = _value_from_option(row.command_line, "--memcard-dir")
        if actual_bios is None:
            errors.append(f"candidate PID {row.pid} is missing an explicit --bios path")
        elif _win_path(actual_bios) != expected_bios:
            errors.append(f"candidate PID {row.pid} uses BIOS {actual_bios!r}, expected {bios!s}")
        if actual_memcard is None:
            errors.append(f"candidate PID {row.pid} is missing an explicit --memcard-dir path")
        elif _win_path(actual_memcard) != expected_memcard:
            errors.append(f"candidate PID {row.pid} uses memcard dir {actual_memcard!r}, expected {memcard_dir!s}")
    return AuditResult(errors, warnings, details)


def audit_paths(
    executable: str | Path,
    bios: str | Path,
    memcard_dir: str | Path,
    session_root: str | Path,
    state_dir: str | Path | None = None,
) -> AuditResult:
    errors: list[str] = []
    warnings: list[str] = []
    exe = Path(executable)
    bios_path = Path(bios)
    memcards = Path(memcard_dir)
    root = Path(session_root)
    state = Path(state_dir) if state_dir is not None else memcards
    for label, path in (("executable", exe), ("BIOS", bios_path)):
        if not path.is_file():
            errors.append(f"{label} does not exist as a file: {path}")
    if not memcards.is_dir():
        errors.append(f"memcard directory does not exist as a directory: {memcards}")
    root_real = _physical(root)
    memcards_real = _physical(memcards)
    state_real = _physical(state)
    if not _inside(memcards_real, root_real):
        errors.append(f"memcard directory escapes session root: {memcards}")
    if _win_path(memcards_real) == _win_path(root_real):
        errors.append("memcard directory must be a dedicated child of session root, not the root itself")
    if not _inside(state_real, memcards_real):
        errors.append(f"state directory escapes memcard directory: {state}")
    for label, path in (("executable", exe), ("BIOS", bios_path), ("state", state)):
        if not _inside(_physical(path), root_real):
            errors.append(f"{label} escapes session root: {path}")
    return AuditResult(errors, warnings, {"session_root": str(root), "state_dir": str(state)})


def audit_markers(marker_root: str | Path, run_start_epoch: float | None) -> AuditResult:
    root = Path(marker_root)
    errors: list[str] = []
    warnings: list[str] = []
    details: dict[str, Any] = {"marker_root": str(root)}
    if run_start_epoch is None:
        return AuditResult(["marker audit requires --run-start-epoch; stale reports cannot be attributed safely"], warnings, details)

    heartbeat_path = root / "psx_freeze_heartbeat.json"
    if heartbeat_path.exists():
        try:
            heartbeat = json.loads(heartbeat_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid heartbeat marker {heartbeat_path}: {exc}")
        else:
            heartbeat_epoch = _epoch(heartbeat.get("wall_clock_epoch"))
            details["heartbeat_epoch"] = heartbeat_epoch
            if heartbeat_epoch is None or heartbeat_epoch < run_start_epoch:
                errors.append("heartbeat marker is stale for this run")
            if heartbeat.get("fatal") is not None:
                errors.append(f"heartbeat reports fatal state: {heartbeat.get('fatal')}")
    else:
        warnings.append(f"heartbeat marker is absent: {heartbeat_path}")

    # A freeze dump is a watchdog finding even when the process never reaches
    # the crash-report path.  Only consider dumps from this run; old dumps are
    # evidence for a previous run and must not poison a new audit.
    for dump_path in sorted(root.glob("psx_freeze_dump_*.json")):
        try:
            dump = json.loads(dump_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid freeze dump {dump_path}: {exc}")
            continue
        dump_epoch = _epoch(dump.get("wall_clock_epoch"))
        if dump_epoch is None or dump_epoch < run_start_epoch:
            continue
        wedge_name = dump.get("wedge_kind_name")
        if wedge_name or dump.get("wedge_kind") not in (None, 0):
            errors.append(f"freeze dump reports {wedge_name or dump.get('wedge_kind')}: {dump_path.name}")
            details["latest_freeze_dump"] = dump_path.name

    report_path = root / "psx_last_run_report.json"
    if report_path.exists():
        try:
            report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid crash report {report_path}: {exc}")
        else:
            report_epoch = _epoch(report.get("timestamp"))
            details["crash_report_epoch"] = report_epoch
            if report_epoch is None or report_epoch < run_start_epoch:
                warnings.append("crash report is stale for this run")
            elif report.get("reason") != "atexit" or report.get("exit_origin") not in _NORMAL_EXIT_ORIGINS:
                errors.append(
                    f"crash report indicates abnormal exit: reason={report.get('reason')!r}, "
                    f"exit_origin={report.get('exit_origin')!r}"
                )
    return AuditResult(errors, warnings, details)


def combine(*results: AuditResult) -> AuditResult:
    errors = [error for result in results for error in result.errors]
    warnings = [warning for result in results for warning in result.warnings]
    details = {key: value for result in results for key, value in result.details.items()}
    return AuditResult(errors, warnings, details)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--bios", type=Path, required=True)
    parser.add_argument("--memcard-dir", type=Path, required=True)
    parser.add_argument("--session-root", type=Path, required=True)
    parser.add_argument("--state-dir", type=Path)
    parser.add_argument("--process-snapshot", type=Path, help="JSON snapshot; omit to query Windows read-only")
    parser.add_argument("--marker-root", type=Path)
    parser.add_argument("--run-start-epoch", type=float)
    args = parser.parse_args(argv)
    try:
        rows = load_snapshot(args.process_snapshot) if args.process_snapshot else collect_windows_processes()
        result = combine(
            audit_processes(rows, args.executable, args.bios, args.memcard_dir, run_start_epoch=args.run_start_epoch),
            audit_paths(args.executable, args.bios, args.memcard_dir, args.session_root, args.state_dir),
            *( [audit_markers(args.marker_root, args.run_start_epoch)] if args.marker_root else [] ),
        )
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as exc:
        print(f"FAIL runtime reproducibility audit: {exc}")
        return 1
    payload = {"ok": result.ok, "errors": result.errors, "warnings": result.warnings, **result.details}
    print(json.dumps(payload, indent=2, sort_keys=True))
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
