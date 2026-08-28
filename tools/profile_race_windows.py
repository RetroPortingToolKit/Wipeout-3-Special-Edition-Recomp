#!/usr/bin/env python3
"""Profile an opening race window and a later steady-state window.

This is deliberately keyed to the title-owned race clock rather than launch
time, menu progress, FPS, or fixed sleeps. It is a bounded diagnostic: the
process exits if a race does not start before --timeout.
"""

import argparse
import json
import time

from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "psxrecomp" / "tools"))
from load_probe import Client, delta_top, perpc_map, phot_map  # noqa: E402


def race_cs(client, address):
    reply = client.cmd("read_ram", addr=f"0x{address:08X}", len=4)
    raw = bytes.fromhex(reply["hex"])
    return int.from_bytes(raw, "little")


def wait_until(client, address, predicate, deadline):
    while time.monotonic() < deadline:
        value = race_cs(client, address)
        if predicate(value):
            return value
        time.sleep(0.1)
    raise TimeoutError("race-clock condition was not reached before timeout")


def profile_window(client, seconds):
    native0 = phot_map(client.cmd("phase_hot", top=64))
    static0 = phot_map(client.cmd("phase_hot", set="static", top=64))
    interp0 = perpc_map(client.cmd("dirty_ram_stats"))
    clock0 = time.monotonic()
    time.sleep(seconds)
    wall = time.monotonic() - clock0
    native1 = phot_map(client.cmd("phase_hot", top=64))
    static1 = phot_map(client.cmd("phase_hot", set="static", top=64))
    interp1 = perpc_map(client.cmd("dirty_ram_stats"))
    return {
        "wall_seconds": wall,
        "phase_profile": client.cmd("phase_profile", window=max(1, round(seconds))),
        "static_delta": delta_top(static0, static1, 20),
        "native_delta": delta_top(native0, native1, 20),
        "interp_pc_delta": delta_top(interp0, interp1, 20),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--race-object", type=lambda x: int(x, 0),
                        default=0x801F5DF4)
    parser.add_argument("--window-seconds", type=float, default=6.0)
    parser.add_argument("--plateau-centiseconds", type=int, default=1000)
    parser.add_argument("--timeout", type=float, default=55.0)
    args = parser.parse_args()

    client = Client("127.0.0.1", args.port)
    clock_addr = args.race_object + 0x114
    deadline = time.monotonic() + args.timeout
    opening_cs = wait_until(client, clock_addr, lambda v: 0 < v < 200, deadline)
    opening = profile_window(client, args.window_seconds)
    plateau_cs = wait_until(client, clock_addr,
                            lambda v: v >= args.plateau_centiseconds, deadline)
    plateau = profile_window(client, args.window_seconds)
    report = {
        "schema": 1,
        "race_clock_address": f"0x{clock_addr:08X}",
        "opening_start_cs": opening_cs,
        "plateau_start_cs": plateau_cs,
        "opening": opening,
        "plateau": plateau,
    }
    Path(args.out).write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
