#!/usr/bin/env python3
"""Bounded Release-runtime timing and moving-soak verifier.

The target must be a Release build with PSX_DEBUG_TOOLS enabled.  Measurements
are counter deltas so boot and menu transients cannot contaminate race results.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes
import json
import socket
import time
from pathlib import Path


def call(port: int, command: str, **fields: object) -> dict:
    request = {"id": 1, "cmd": command, **fields}
    with socket.create_connection(("127.0.0.1", port), timeout=10.0) as sock:
        sock.sendall((json.dumps(request) + "\n").encode())
        data = bytearray()
        while True:
            chunk = sock.recv(1 << 20)
            if not chunk:
                break
            data.extend(chunk)
            try:
                response = json.loads(data)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue
            if not response.get("ok"):
                raise RuntimeError(f"{command}: {response}")
            return response
    raise RuntimeError(f"{command}: incomplete response")


def find_window(pid: int) -> int:
    found: list[int] = []
    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    def visit(hwnd: int, _lparam: int) -> bool:
        window_pid = ctypes.c_ulong()
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(window_pid))
        if window_pid.value == pid and ctypes.windll.user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    ctypes.windll.user32.EnumWindows(enum_proc(visit), 0)
    if not found:
        raise RuntimeError(f"no visible window for PID {pid}")
    return found[0]


def set_client_size(pid: int, width: int, height: int) -> dict:
    hwnd = find_window(pid)
    rect = ctypes.wintypes.RECT()
    client = ctypes.wintypes.RECT()
    ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
    ctypes.windll.user32.GetClientRect(hwnd, ctypes.byref(client))
    border_w = (rect.right - rect.left) - (client.right - client.left)
    border_h = (rect.bottom - rect.top) - (client.bottom - client.top)
    ok = ctypes.windll.user32.SetWindowPos(
        hwnd, 0, rect.left, rect.top, width + border_w, height + border_h, 0x0004
    )
    if not ok:
        raise ctypes.WinError()
    time.sleep(1.0)
    ctypes.windll.user32.GetClientRect(hwnd, ctypes.byref(client))
    actual = [client.right - client.left, client.bottom - client.top]
    if actual != [width, height]:
        raise RuntimeError(f"client resize requested {[width, height]}, got {actual}")
    return {"hwnd": hwnd, "client": actual}


def snapshot(port: int) -> dict:
    return {
        "vblank": call(port, "vblank_rate"),
        "audio": call(port, "audio_stats"),
        "spu": call(port, "spu_status"),
        "cdrom": call(port, "cdrom_state"),
        "gpu": call(port, "gpu_state"),
    }


def tap(stats: dict, name: str) -> dict:
    return next(item for item in stats["taps"] if item["name"] == name)


def delta_report(before: dict, after: dict, elapsed: float) -> dict:
    vb = after["vblank"]["delivered"] - before["vblank"]["delivered"]
    raise_count = (
        after["vblank"]["cycle_paced_raise"]
        - before["vblank"]["cycle_paced_raise"]
    )
    cd_frames = (
        tap(after["audio"], "cd_in")["frames"]
        - tap(before["audio"], "cd_in")["frames"]
    )
    cd_rate = cd_frames / elapsed
    errors = {
        "trace_underruns": after["audio"]["underruns"] - before["audio"]["underruns"],
        "output_underruns": after["audio"]["out"]["underruns"] - before["audio"]["out"]["underruns"],
        "output_overflow_drops": after["audio"]["out"]["overflow_drops"] - before["audio"]["out"]["overflow_drops"],
        "spu_cd_underflow_frames": after["spu"]["cd_underflow_frames"] - before["spu"]["cd_underflow_frames"],
        "spu_cd_overflow_frames": after["spu"]["cd_overflow_frames"] - before["spu"]["cd_overflow_frames"],
    }
    ws = after["gpu"]["ws"]
    report = {
        "elapsed_seconds": round(elapsed, 3),
        "vblank": {
            "delivered": vb,
            "delivered_hz": round(vb / elapsed, 4),
            "cycle_paced_raise": raise_count,
            "pollhack_delta": after["vblank"]["pollhack_raise"] - before["vblank"]["pollhack_raise"],
        },
        "cdda": {
            "frames": cd_frames,
            "frames_per_second": round(cd_rate, 3),
            "drift_percent_from_44100": round(abs(cd_rate - 44100.0) / 44100.0 * 100.0, 4),
            "nonzero_delta": tap(after["audio"], "cd_in")["nonzero"] - tap(before["audio"], "cd_in")["nonzero"],
        },
        "errors": errors,
        "final_widescreen": {
            "configured": ws["configured"],
            "active": ws["active"],
            "game_mode": ws["game_mode"],
            "x_margin": ws["x_margin"],
        },
        "final_cdrom_seq": after["cdrom"]["seq"],
    }
    report["pass"] = bool(
        report["vblank"]["pollhack_delta"] == 0
        and report["cdda"]["drift_percent_from_44100"] <= 1.0
        and report["cdda"]["nonzero_delta"] > 0
        and all(value == 0 for value in errors.values())
        and ws["game_mode"] == 1
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--size", choices=("4x3", "16x9", "21x9"))
    parser.add_argument("--moving", action="store_true")
    parser.add_argument("--input-period", type=float, default=10.0)
    parser.add_argument("--buttons")
    parser.add_argument(
        "--throttle-burst",
        type=float,
        default=0.0,
        help="seconds of acceleration at the start of each alternating steering period",
    )
    parser.add_argument("--expected-hz", type=float, default=100.0)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.input_period <= 0.0:
        parser.error("--input-period must be positive")
    if args.throttle_burst < 0.0 or args.throttle_burst > args.input_period:
        parser.error("--throttle-burst must be between zero and --input-period")
    if args.throttle_burst and (not args.moving or args.buttons is not None):
        parser.error("--throttle-burst requires --moving without --buttons")

    window = None
    if args.size:
        if args.pid is None:
            parser.error("--size requires --pid")
        sizes = {"4x3": (960, 720), "16x9": (1280, 720), "21x9": (1400, 600)}
        window = set_client_size(args.pid, *sizes[args.size])

    before = snapshot(args.port)
    started = time.monotonic()
    checkpoints = []
    next_check = started + min(30.0, args.seconds)
    turn_right = True
    while True:
        now = time.monotonic()
        if now - started >= args.seconds:
            break
        burst_elapsed = 0.0
        if args.moving:
            steering = "0xFFDF" if turn_right else "0xFF7F"
            buttons = args.buttons or steering
            if args.buttons is None and args.throttle_burst > 0.0:
                accelerated = "0xBFDF" if turn_right else "0xBF7F"
                call(args.port, "set_input", buttons=accelerated)
                burst = min(args.throttle_burst, args.seconds - (now - started))
                time.sleep(max(0.0, burst))
                burst_elapsed = burst
            call(args.port, "set_input", buttons=buttons)
            turn_right = not turn_right
        wait = min(
            max(0.0, args.input_period - burst_elapsed),
            args.seconds - (time.monotonic() - started),
        )
        time.sleep(max(0.0, wait))
        if time.monotonic() >= next_check:
            gpu = call(args.port, "gpu_state")["ws"]
            checkpoints.append({
                "second": round(time.monotonic() - started, 1),
                "guest_frame": gpu["cur_frame"],
                "game_mode": gpu["game_mode"],
                "active": gpu["active"],
                "margin": gpu["x_margin"],
            })
            next_check += 30.0
    after = snapshot(args.port)
    elapsed = time.monotonic() - started
    report = delta_report(before, after, elapsed)
    actual_hz = report["vblank"]["delivered_hz"]
    report["pass"] = bool(
        report["pass"]
        and abs(actual_hz - args.expected_hz) <= 1.0
        and all(item["game_mode"] == 1 for item in checkpoints)
        and all(
            later > earlier
            for earlier, later in zip(
                [before["gpu"]["ws"]["cur_frame"]]
                + [item["guest_frame"] for item in checkpoints],
                [item["guest_frame"] for item in checkpoints]
                + [after["gpu"]["ws"]["cur_frame"]],
            )
        )
    )
    report.update({
        "size": args.size,
        "window": window,
        "movement": {
            "enabled": args.moving,
            "period_seconds": args.input_period,
            "throttle_burst_seconds": args.throttle_burst,
            "fixed_buttons": args.buttons,
        },
        "checkpoints": checkpoints,
    })
    if args.moving:
        call(args.port, "clear_input")
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
