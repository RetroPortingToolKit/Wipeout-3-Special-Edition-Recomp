"""Focused tests for the deterministic fast-loading equivalence harness."""

from __future__ import annotations

import importlib.util
import json
import socket
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "fast_loading_equivalence.py"
SPEC = importlib.util.spec_from_file_location("fast_loading_equivalence", TOOL)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)


def entry_bundle(**overrides):
    traces = {
        "card_reads": {"entries": [{"seq": 1, "slot": 0, "sector": 7, "cyc": 10}]},
        "card_writes": {"entries": [{"seq": 2, "addr": "0x10", "value": "0x20", "cyc": 11}]},
        "card_transactions": {"entries": [{"txn_seq": 3, "start_byte_seq": 4, "end_byte_seq": 5, "tx": ["0x81"]}]},
        "cd_sectors": {"entries": [{"seq": 6, "lba": 123, "frame": 4}]},
        "cd_trace": {"entries": [{"seq": 7, "kind": "sector", "frame": 4}]},
        "cd_irqs": {"events": [{"seq": 8, "cycle": 99, "srcn": 2, "detail": 1}]},
        "frame_fingerprints": {"entries": [{"frame": 4, "wr": "0xabc", "cyc": 100}]},
    }
    value = {
        "anchor_frame": 3,
        "end_frame": 4,
        "traces": traces,
        "audio_before": {
            "taps": [{"frames": 0, "nonzero": 0, "audible": 0}],
            "pump_calls": 0, "pump_skips": 0, "underruns": 0,
            "events_total": 0, "out": {"underruns": 0, "overflow_drops": 0},
        },
        "audio_after": {
            "taps": [{"frames": 100, "nonzero": 50, "audible": 40}],
            "pump_calls": 2, "pump_skips": 1, "underruns": 0,
            "events_total": 1, "out": {"underruns": 0, "overflow_drops": 0},
        },
        "audio_events_after": {"events": [{"seq": 9, "smp": 100, "frame": 4, "kind": "RENDER"}]},
    }
    value.update(overrides)
    return value


def test_compare_ignores_process_local_sequence_numbers():
    normal = entry_bundle()
    accelerated = entry_bundle()
    accelerated["traces"]["card_reads"]["entries"][0]["seq"] = 91
    accelerated["traces"]["cd_irqs"]["events"][0]["seq"] = 92
    accelerated["audio_events_after"]["events"][0]["seq"] = 93
    report = MODULE.compare(normal, accelerated)
    assert report["pass"], report


def test_compare_flags_guest_visible_order_change():
    normal = entry_bundle()
    accelerated = entry_bundle()
    accelerated["traces"]["cd_irqs"]["events"][0]["cycle"] = 100
    report = MODULE.compare(normal, accelerated)
    assert not report["pass"]
    failed = {item["name"] for item in report["checks"] if not item["pass"]}
    assert "cd_irqs" in failed


def test_compare_flags_audio_timing_change():
    normal = entry_bundle()
    accelerated = entry_bundle()
    accelerated["audio_after"]["taps"][0]["frames"] = 99
    report = MODULE.compare(normal, accelerated)
    assert not report["pass"]
    assert any(item["name"] == "audio_timing" and not item["pass"]
               for item in report["checks"])


def test_compare_flags_endpoint_mismatch():
    normal = entry_bundle()
    accelerated = entry_bundle(end_frame=5)
    report = MODULE.compare(normal, accelerated)
    assert not report["pass"]
    assert any(item["name"] == "endpoint" and not item["pass"]
               for item in report["checks"])


def test_compare_fails_closed_when_audio_ring_wraps():
    normal = entry_bundle(
        audio_events_before={"total": 0},
        audio_events_after={"total": 9000, "events": [
            {"seq": i, "smp": i, "frame": 4, "kind": "RENDER"}
            for i in range(808, 9000)
        ]},
    )
    accelerated = entry_bundle(
        audio_events_before={"total": 0},
        audio_events_after={"total": 9000, "events": [
            {"seq": i, "smp": i, "frame": 4, "kind": "RENDER"}
            for i in range(808, 9000)
        ]},
    )
    report = MODULE.compare(normal, accelerated)
    assert not report["pass"]
    assert any(item["name"] == "audio_events" and not item["pass"]
               and "wrapped" in item["difference"]
               for item in report["checks"])


def test_cli_compare_writes_report_and_exit_status(tmp_path):
    normal = tmp_path / "normal.json"
    accelerated = tmp_path / "accelerated.json"
    report = tmp_path / "report.json"
    normal.write_text(json.dumps(entry_bundle()), encoding="utf-8")
    accelerated.write_text(json.dumps(entry_bundle()), encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(TOOL), "compare", "--normal", str(normal),
         "--accelerated", str(accelerated), "--report", str(report)],
        cwd=ROOT, text=True, capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    assert "PASS fast-loading equivalence" in result.stdout
    assert json.loads(report.read_text(encoding="utf-8"))["pass"] is True


def test_json_reader_handles_nested_large_response():
    payload = {"id": 1, "ok": True, "entries": [{"hex": "{}\\\""}]}
    left, right = socket.socketpair()
    try:
        right.sendall((json.dumps(payload) + "\n").encode())
        right.close()
        assert MODULE._read_json_object(left) == payload
    finally:
        left.close()
