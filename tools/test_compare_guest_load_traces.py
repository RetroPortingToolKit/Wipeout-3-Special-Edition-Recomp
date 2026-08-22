"""Regression tests for the two focused guest-load gates."""

import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "compare_guest_load_traces.py"


def write_trace(path, entries):
    path.write_text(json.dumps(entries), encoding="utf-8")


def run(*args):
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )


def test_startup_card_ignores_process_local_sequence(tmp_path):
    normal = tmp_path / "normal.json"
    accelerated = tmp_path / "accelerated.json"
    entry = {
        "seq": 3,
        "slot": 0,
        "cmd": 2,
        "sector": 7,
        "checksum": 123,
        "data_idx": 128,
        "data_peek": "00112233",
    }
    write_trace(normal, [entry])
    entry["seq"] = 91
    write_trace(accelerated, [entry])
    result = run(
        "--gate", "startup-card", "--kind", "card",
        "--normal", str(normal), "--accelerated", str(accelerated),
    )
    assert result.returncode == 0, result.stderr


def test_gate_rejects_wrong_trace_kind(tmp_path):
    normal = tmp_path / "normal.json"
    accelerated = tmp_path / "accelerated.json"
    write_trace(normal, [])
    write_trace(accelerated, [])
    result = run(
        "--gate", "startup-card", "--kind", "fingerprint",
        "--normal", str(normal), "--accelerated", str(accelerated),
    )
    assert result.returncode != 0
    assert "does not accept" in result.stderr
