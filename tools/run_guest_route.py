#!/usr/bin/env python3
"""Run a WipEout input route on psxrecomp's guest-frame input queue."""

from __future__ import annotations

import argparse
import json
import socket
import time
from pathlib import Path


def request(port: int, payload: dict, timeout: float = 5.0) -> dict:
    raw = (json.dumps(payload, separators=(",", ":")) + "\n").encode()
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall(raw)
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(1 << 16)
            if not chunk:
                break
            chunks.append(chunk)
            if b"\n" in chunk:
                break
    response = json.loads(b"".join(chunks).splitlines()[0])
    if not response.get("ok"):
        raise RuntimeError(response.get("error", "debug command failed"))
    return response


def load_route(path: Path) -> list[dict[str, int]]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("format") != "wipeout3se-guest-route-v1":
        raise ValueError("unsupported route format")
    raw_steps = document.get("steps")
    if not isinstance(raw_steps, list) or not raw_steps:
        raise ValueError("route requires a non-empty steps array")
    steps: list[dict[str, int]] = []
    for index, raw in enumerate(raw_steps):
        buttons = int(str(raw["buttons"]), 0)
        frames = int(raw["frames"])
        if not 0 <= buttons <= 0xFFFF or frames <= 0:
            raise ValueError(f"invalid step {index}")
        steps.append({"buttons": buttons, "frames": frames})
    return steps


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("route", type=Path)
    parser.add_argument("--port", type=int, default=4370)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()
    steps = load_route(args.route)

    request(args.port, {"cmd": "input_route_clear"})
    for step in steps:
        request(args.port, {"cmd": "input_route_append", **step})
    started = request(args.port, {"cmd": "input_route_start"})

    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        status = request(args.port, {"cmd": "input_route_status"})
        if not status["active"]:
            if status["index"] != status["steps"] or status["remaining"] != 0:
                raise RuntimeError(f"route stopped incompletely: {status}")
            frame = request(args.port, {"cmd": "frame"})
            print(json.dumps({
                "ok": True,
                "start_frame": started["start_frame"],
                "end_frame": frame["frame"],
                "route_frames": sum(step["frames"] for step in steps),
                "steps": len(steps),
            }, sort_keys=True))
            return 0
        time.sleep(0.01)
    request(args.port, {"cmd": "input_route_stop"})
    raise TimeoutError("guest route did not complete before deadline")


if __name__ == "__main__":
    raise SystemExit(main())
