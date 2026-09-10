import importlib.util
import json
import os
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify_runtime_repro", ROOT / "tools/verify_runtime_repro.py")
verify = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = verify
spec.loader.exec_module(verify)


def _paths(tmp_path):
    root = tmp_path / "run"
    build = root / "build"
    saves = root / "saves"
    build.mkdir(parents=True)
    saves.mkdir()
    exe = build / "Wipeout.exe"
    bios = root / "SCPH1001.BIN"
    exe.write_bytes(b"exe")
    bios.write_bytes(b"bios")
    return root, exe, bios, saves


def _row(exe, bios, saves, pid=13736, creation=None):
    return verify.ProcessRow(
        pid,
        str(exe),
        f'"{exe}" --no-launcher --bios "{bios}" --memcard-dir "{saves}"',
        creation,
    )


def test_exact_executable_and_one_candidate(tmp_path):
    _, exe, bios, saves = _paths(tmp_path)
    rows = [_row(exe, bios, saves), _row(tmp_path / "other" / exe.name, bios, saves, pid=2)]
    result = verify.audit_processes(rows, exe, bios, saves)
    assert result.ok
    assert result.details["candidate_pids"] == [13736]


def test_duplicate_or_stale_candidate_is_rejected(tmp_path):
    _, exe, bios, saves = _paths(tmp_path)
    rows = [_row(exe, bios, saves, pid=1, creation=10), _row(exe, bios, saves, pid=2, creation=20)]
    result = verify.audit_processes(rows, exe, bios, saves, run_start_epoch=15)
    assert not result.ok
    assert any("found 2" in error for error in result.errors)
    assert any("predates this run" in error for error in result.errors)


def test_path_audit_rejects_broad_or_escaping_state_root(tmp_path):
    root, exe, bios, saves = _paths(tmp_path)
    result = verify.audit_paths(exe, bios, root, root, root)
    assert not result.ok
    assert any("dedicated child" in error for error in result.errors)
    outside = tmp_path / "outside"
    outside.mkdir()
    result = verify.audit_paths(exe, bios, saves, root, outside)
    assert any("state directory escapes" in error for error in result.errors)


def test_marker_audit_distinguishes_stale_report_and_fatal_heartbeat(tmp_path):
    now = time.time()
    (tmp_path / "psx_freeze_heartbeat.json").write_text(
        json.dumps({"wall_clock_epoch": now, "fatal": "seh"}), encoding="utf-8"
    )
    (tmp_path / "psx_last_run_report.json").write_text(
        json.dumps({"timestamp": now - 100, "reason": "seh", "exit_origin": "unknown"}), encoding="utf-8"
    )
    result = verify.audit_markers(tmp_path, now - 1)
    assert not result.ok
    assert any("fatal state" in error for error in result.errors)
    assert any("stale for this run" in warning for warning in result.warnings)


def test_marker_audit_detects_current_freeze_dump(tmp_path):
    now = time.time()
    (tmp_path / "psx_freeze_dump_psx-runtime_1.json").write_text(
        json.dumps({"wall_clock_epoch": now, "wedge_kind": 5, "wedge_kind_name": "spin_freeze"}),
        encoding="utf-8",
    )
    result = verify.audit_markers(tmp_path, now - 1)
    assert not result.ok
    assert any("spin_freeze" in error for error in result.errors)


def test_marker_audit_ignores_malformed_dump_that_predates_run(tmp_path):
    now = time.time()
    stale = tmp_path / "psx_freeze_dump_psx-runtime_stale.json"
    stale.write_text("{not-json", encoding="utf-8")
    os.utime(stale, (now - 100, now - 100))
    result = verify.audit_markers(tmp_path, now - 1)
    assert result.ok


def test_marker_audit_rejects_malformed_dump_from_current_run(tmp_path):
    now = time.time()
    current = tmp_path / "psx_freeze_dump_psx-runtime_current.json"
    current.write_text("{not-json", encoding="utf-8")
    result = verify.audit_markers(tmp_path, now - 1)
    assert not result.ok
    assert any("invalid freeze dump" in error for error in result.errors)


def test_marker_audit_requires_run_boundary(tmp_path):
    result = verify.audit_markers(tmp_path, None)
    assert not result.ok
    assert "run-start-epoch" in result.errors[0]


def test_cim_creation_date_preserves_timezone_offset():
    assert verify._epoch("20260824042844.000000-420") == verify._epoch("2026-08-24T11:28:44Z")
    assert verify._epoch("/Date(1787570924841)/") == 1787570924.841
