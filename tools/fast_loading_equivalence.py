#!/usr/bin/env python3
"""Deterministic normal-vs-fast-loading equivalence harness.

The runtime exposes all of the evidence needed for the focused loading test
over its TCP debug server.  This tool restores the same savestate anchor in
two already-running instances, waits for the restore receipt, clears the
post-restore trace channels, advances both instances by the same number of
guest frames, and compares the resulting evidence.

It deliberately compares guest-visible order and timing, not wall-clock load
duration.  A host-pacing optimization is acceptable only when it preserves
the CD sector/IRQ stream, card traffic, guest frame fingerprints, and audio
sample/event cadence.

Examples:

  python tools/fast_loading_equivalence.py pair \
      --normal-port 4377 --accelerated-port 4378 --slot 2 --frames 300 \
      --out-dir artifacts/fast-loading/startup

  python tools/fast_loading_equivalence.py compare \
      --normal artifacts/fast-loading/startup/normal.json \
      --accelerated artifacts/fast-loading/startup/accelerated.json

The two processes must already be running with the same BIOS/disc and an
identical savestate in the requested slot.  Keep their writable state roots
separate so card writes cannot cross-contaminate the pair.  The normal process
has fast loading disabled; the accelerated process has it enabled.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path
from typing import Any, Iterable


class HarnessError(RuntimeError):
    """A deterministic harness failure, suitable for a concise CLI error."""


class DebugClient:
    """One-request-per-connection client for the runtime debug protocol."""

    def __init__(self, host: str, port: int, timeout: float = 15.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    def request(self, payload: dict[str, Any]) -> dict[str, Any]:
        wire = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
        with socket.create_connection((self.host, self.port), self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(wire)
            return _read_json_object(sock)


def _read_json_object(sock: socket.socket) -> dict[str, Any]:
    """Read one complete JSON object, including large trace responses."""

    data = bytearray()
    pos = 0
    depth = 0
    in_string = False
    escaped = False
    started = False
    while True:
        chunk = sock.recv(1 << 20)
        if not chunk:
            break
        data.extend(chunk)
        while pos < len(data):
            char = data[pos]
            if in_string:
                if escaped:
                    escaped = False
                elif char == ord("\\"):
                    escaped = True
                elif char == ord('"'):
                    in_string = False
                pos += 1
                continue
            if char == ord('"'):
                in_string = True
            elif char == ord("{"):
                depth += 1
                started = True
            elif char == ord("}"):
                depth -= 1
                if started and depth == 0:
                    return json.loads(bytes(data[: pos + 1]).decode("utf-8"))
            pos += 1
    text = bytes(data).decode("utf-8", errors="replace").strip()
    if not text:
        raise HarnessError("debug server closed the connection without a response")
    try:
        value = json.loads(text)
    except json.JSONDecodeError as exc:
        raise HarnessError(f"invalid debug-server response: {text[:240]!r}") from exc
    if not isinstance(value, dict):
        raise HarnessError("debug-server response was not a JSON object")
    return value


def request(client: DebugClient, command: str, **kwargs: Any) -> dict[str, Any]:
    response = client.request({"cmd": command, **kwargs})
    if response.get("ok") is False:
        raise HarnessError(f"{client.port}:{command} failed: {response}")
    return response


def wait_for_savestate_load(client: DebugClient, slot: int, timeout: float) -> dict[str, Any]:
    before = request(client, "savestate_status")
    generation = int(before.get("generation", 0))
    request(client, "savestate", op="load", slot=slot)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = request(client, "savestate_status")
        if (int(status.get("generation", 0)) > generation and
                not int(status.get("pending", 0))):
            if not int(status.get("last_ok", 0)) or status.get("last_op") != "load":
                raise HarnessError(
                    f"{client.port}: savestate slot {slot} was rejected: {status}"
                )
            if int(status.get("last_slot", -1)) != slot:
                raise HarnessError(
                    f"{client.port}: completion receipt named the wrong slot: {status}"
                )
            return status
        time.sleep(0.025)
    raise HarnessError(
        f"{client.port}: timed out waiting for savestate load receipt "
        f"(slot {slot}, generation {generation})"
    )


def reset_capture_channels(client: DebugClient) -> None:
    """Clear only trace rings; guest state is untouched."""

    for command in (
        "cdrom_sector_history_clear",
        "cdrom_command_history_clear",
        "cdrom_trace_clear",
        "card_read_summary_reset",
        "card_data_writes_reset",
    ):
        request(client, command)
    request(client, "devtrace_ctl", reset=1, arm=1)


def current_frame(client: DebugClient) -> int:
    return int(request(client, "frame").get("frame", -1))


def wait_for_frame(client: DebugClient, target: int, timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frame = current_frame(client)
        if frame >= target:
            return frame
        time.sleep(0.025)
    raise HarnessError(f"{client.port}: timed out at frame {current_frame(client)}; target {target}")


def _audio_counter_snapshot(stats: dict[str, Any]) -> dict[str, int]:
    counters: dict[str, int] = {}
    for index, tap in enumerate(stats.get("taps", [])):
        for field in ("frames", "nonzero", "audible"):
            counters[f"tap{index}.{field}"] = int(tap.get(field, 0))
    for field in ("pump_calls", "pump_skips", "underruns", "events_total"):
        counters[field] = int(stats.get(field, 0))
    output = stats.get("out", {})
    for field in ("underruns", "overflow_drops"):
        counters[f"out.{field}"] = int(output.get(field, 0))
    return counters


def _counter_delta(before: dict[str, int], after: dict[str, int]) -> dict[str, int]:
    keys = sorted(set(before) | set(after))
    return {key: int(after.get(key, 0)) - int(before.get(key, 0)) for key in keys}


def _audio_events_since(bundle: dict[str, Any]) -> tuple[list[dict[str, Any]], bool]:
    """Return only events generated after the restored anchor.

    The audio event ring is always-on and has no reset verb.  Its sequence
    numbers are process-local, but they are monotonic within one bundle, so a
    pre-window total gives us an exact post-anchor slice as long as the ring
    did not wrap.  If a caller supplies a hand-authored bundle without totals,
    retain its entire `audio_events_after` list for comparator compatibility.
    """

    before = bundle.get("audio_events_before", {})
    after = bundle.get("audio_events_after", {})
    before_total = before.get("total")
    events = after.get("events", []) if isinstance(after, dict) else []
    if not isinstance(events, list):
        return [], False
    if before_total is None:
        return [e for e in events if isinstance(e, dict)], True
    start = int(before_total)
    selected = [
        e for e in events
        if isinstance(e, dict) and int(e.get("seq", -1)) >= start
    ]
    end_total = int(after.get("total", start)) if isinstance(after, dict) else start
    # audio_events returns the newest retained entries.  If more events were
    # produced than the response can retain, the post-anchor stream is not a
    # complete proof and must fail closed instead of comparing a suffix.
    return selected, end_total - start <= len(selected)


def capture(client: DebugClient, slot: int, frames: int, timeout: float) -> dict[str, Any]:
    if frames < 1:
        raise HarnessError("--frames must be positive")
    receipt = wait_for_savestate_load(client, slot, timeout)
    # card_txn_dump has no reset verb.  Keep its pre-window total so the
    # compare phase can discard transactions inherited from the anchor.
    card_transactions_before = request(client, "card_txn_dump", count=128)
    reset_capture_channels(client)
    anchor_frame = current_frame(client)
    audio_before = request(client, "audio_stats")
    audio_events_before = request(client, "audio_events", count=8192)
    target_frame = anchor_frame + frames
    end_frame = wait_for_frame(client, target_frame, timeout)

    # Keep the frame filter bounded to this anchor window.  The runtime frame
    # ring is global and may contain a prefix from before the restored state.
    frame_filter = {
        "frame_lo": anchor_frame + 1,
        "frame_hi": end_frame,
    }
    audio_after = request(client, "audio_stats")
    audio_events_after = request(client, "audio_events", count=8192)

    traces = {
        "card_reads": request(client, "card_read_summary"),
        "card_writes": request(client, "card_data_writes", count=4096),
        "card_transactions": request(client, "card_txn_dump", count=128),
        "cd_sectors": request(client, "cdrom_sector_history", count=4096),
        "cd_trace": request(client, "cdrom_trace_dump", count=8192, **frame_filter),
        "cd_irqs": request(client, "devtrace_dump", count=65536, src=2),
        "frame_fingerprints": request(
            client, "frame_fingerprint", count=65536, **frame_filter
        ),
    }
    return {
        "schema": 1,
        "port": client.port,
        "slot": slot,
        "requested_frames": frames,
        "savestate_receipt": receipt,
        "anchor_frame": anchor_frame,
        "end_frame": end_frame,
        "audio_before": audio_before,
        "audio_after": audio_after,
        "audio_events_before": audio_events_before,
        "audio_events_after": audio_events_after,
        "card_transactions_before": card_transactions_before,
        "traces": traces,
    }


def _entries(bundle: dict[str, Any], name: str) -> list[dict[str, Any]]:
    value = bundle.get("traces", {}).get(name, {})
    if not isinstance(value, dict):
        return []
    # Most debug verbs call the array `entries`; devtrace_dump calls it
    # `events`.  Treat both as the same ordered evidence stream.
    entries = value.get("entries", value.get("events", []))
    return entries if isinstance(entries, list) else []


def _window_entries(bundle: dict[str, Any], name: str) -> list[dict[str, Any]]:
    entries = _entries(bundle, name)
    if name != "card_transactions":
        return entries
    before = bundle.get("card_transactions_before", {})
    baseline = int(before.get("total_closed", 0)) if isinstance(before, dict) else 0
    # txn_seq is local to a process but monotonic within that process.  The
    # baseline count is the first sequence that belongs to this capture.
    return [e for e in entries if int(e.get("txn_seq", -1)) >= baseline]


def _without(entry: dict[str, Any], *fields: str) -> dict[str, Any]:
    ignored = set(fields)
    return {key: value for key, value in entry.items() if key not in ignored}


def _first_difference(left: list[Any], right: list[Any]) -> str | None:
    if len(left) != len(right):
        return f"count {len(left)} != {len(right)}"
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return f"entry {index}: {a!r} != {b!r}"
    return None


def _check(name: str, left: list[Any], right: list[Any], note: str = "") -> dict[str, Any]:
    difference = _first_difference(left, right)
    result = {
        "name": name,
        "pass": difference is None,
        "left_count": len(left),
        "right_count": len(right),
    }
    if difference:
        result["difference"] = difference
    if note:
        result["note"] = note
    return result


def compare(normal: dict[str, Any], accelerated: dict[str, Any]) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    checks.append(_check(
        "anchor", [normal.get("anchor_frame")], [accelerated.get("anchor_frame")],
        "the two restores did not resume at the same guest frame",
    ))
    checks.append(_check(
        "endpoint",
        [normal.get("end_frame"), normal.get("requested_frames")],
        [accelerated.get("end_frame"), accelerated.get("requested_frames")],
        "the two captures did not cover the same guest-frame window",
    ))

    # Sequence counters are local to each process.  Every other field is
    # guest-visible evidence and is intentionally strict.
    for name, ignored in (
        ("card_reads", ("seq",)),
        ("card_writes", ("seq",)),
        ("card_transactions", ("txn_seq", "start_byte_seq", "end_byte_seq")),
        ("cd_sectors", ("seq",)),
        ("cd_trace", ("seq",)),
        ("cd_irqs", ("seq",)),
        ("frame_fingerprints", ()),
    ):
        left = [_without(e, *ignored) for e in _window_entries(normal, name)]
        right = [_without(e, *ignored) for e in _window_entries(accelerated, name)]
        note = "empty in both runs" if not left and not right else ""
        checks.append(_check(name, left, right, note))

    before_n = _audio_counter_snapshot(normal.get("audio_before", {}))
    after_n = _audio_counter_snapshot(normal.get("audio_after", {}))
    before_a = _audio_counter_snapshot(accelerated.get("audio_before", {}))
    after_a = _audio_counter_snapshot(accelerated.get("audio_after", {}))
    delta_n = _counter_delta(before_n, after_n)
    delta_a = _counter_delta(before_a, after_a)
    checks.append(_check("audio_timing", [delta_n], [delta_a]))

    raw_events_n, complete_n = _audio_events_since(normal)
    raw_events_a, complete_a = _audio_events_since(accelerated)
    events_n = [_without(e, "seq") for e in raw_events_n]
    events_a = [_without(e, "seq") for e in raw_events_a]
    audio_check = _check("audio_events", events_n, events_a,
                          "audio event ring is compared over its retained window")
    if not complete_n or not complete_a:
        audio_check["pass"] = False
        audio_check["difference"] = "post-anchor audio event ring wrapped before capture"
    checks.append(audio_check)

    passed = all(bool(item["pass"]) for item in checks)
    return {
        "schema": 1,
        "pass": passed,
        "checks": checks,
        "normal": {
            "port": normal.get("port"),
            "anchor_frame": normal.get("anchor_frame"),
            "end_frame": normal.get("end_frame"),
            "audio_delta": delta_n,
        },
        "accelerated": {
            "port": accelerated.get("port"),
            "anchor_frame": accelerated.get("anchor_frame"),
            "end_frame": accelerated.get("end_frame"),
            "audio_delta": delta_a,
        },
    }


def print_report(report: dict[str, Any]) -> None:
    overall = "PASS" if report.get("pass") else "FAIL"
    print(f"{overall} fast-loading equivalence")
    for check in report.get("checks", []):
        status = "PASS" if check.get("pass") else "FAIL"
        counts = f"{check.get('left_count', 0)} vs {check.get('right_count', 0)}"
        suffix = f" — {check['difference']}" if check.get("difference") else ""
        if check.get("note"):
            suffix += f" ({check['note']})"
        print(f"  {status:<4} {check['name']:<20} {counts}{suffix}")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HarnessError(f"cannot read {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise HarnessError(f"{path} is not a JSON object")
    return value


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    capture_parser = sub.add_parser("capture", help="restore one port and capture an evidence bundle")
    capture_parser.add_argument("--port", type=int, required=True)
    capture_parser.add_argument("--host", default="127.0.0.1")
    capture_parser.add_argument("--slot", type=int, required=True)
    capture_parser.add_argument("--frames", type=int, required=True)
    capture_parser.add_argument("--timeout", type=float, default=120.0)
    capture_parser.add_argument("--out", type=Path, required=True)

    pair_parser = sub.add_parser("pair", help="capture both ports and compare them")
    pair_parser.add_argument("--normal-port", type=int, required=True)
    pair_parser.add_argument("--accelerated-port", type=int, required=True)
    pair_parser.add_argument("--host", default="127.0.0.1")
    pair_parser.add_argument("--slot", type=int, required=True)
    pair_parser.add_argument("--frames", type=int, required=True)
    pair_parser.add_argument("--timeout", type=float, default=120.0)
    pair_parser.add_argument("--out-dir", type=Path, required=True)

    compare_parser = sub.add_parser("compare", help="compare two previously captured bundles")
    compare_parser.add_argument("--normal", type=Path, required=True)
    compare_parser.add_argument("--accelerated", type=Path, required=True)
    compare_parser.add_argument("--report", type=Path)

    args = parser.parse_args(list(argv) if argv is not None else None)
    try:
        if args.command == "capture":
            bundle = capture(DebugClient(args.host, args.port), args.slot, args.frames, args.timeout)
            write_json(args.out, bundle)
            print(f"CAPTURED port {args.port}: {args.out}")
            return 0
        if args.command == "pair":
            normal_client = DebugClient(args.host, args.normal_port)
            accelerated_client = DebugClient(args.host, args.accelerated_port)
            # Restore both before starting either capture window.  The order is
            # fixed so the test is reproducible and neither side waits on the
            # other process's wall-clock progress.
            normal = capture(normal_client, args.slot, args.frames, args.timeout)
            accelerated = capture(accelerated_client, args.slot, args.frames, args.timeout)
            normal_path = args.out_dir / "normal.json"
            accelerated_path = args.out_dir / "accelerated.json"
            report_path = args.out_dir / "report.json"
            report = compare(normal, accelerated)
            write_json(normal_path, normal)
            write_json(accelerated_path, accelerated)
            write_json(report_path, report)
            print_report(report)
            print(f"bundles: {normal_path}, {accelerated_path}")
            print(f"report:   {report_path}")
            return 0 if report["pass"] else 1
        normal = load_json(args.normal)
        accelerated = load_json(args.accelerated)
        report = compare(normal, accelerated)
        if args.report:
            write_json(args.report, report)
        print_report(report)
        if args.report:
            print(f"report:   {args.report}")
        return 0 if report["pass"] else 1
    except (HarnessError, OSError, ValueError) as exc:
        print(f"ERROR {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
