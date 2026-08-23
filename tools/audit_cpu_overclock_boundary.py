#!/usr/bin/env python3
"""Static regression audit for the 900% CPU/peripheral timing boundary.

This intentionally checks source shape, not runtime behavior.  A CPU
overclock may scale instruction retirement, but device deadlines must remain
on the raw guest-cycle clock.
"""
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1] / "psxrecomp"
HEADER = ROOT / "runtime" / "include" / "psx_cycles.h"
CYCLES = ROOT / "runtime" / "src" / "psx_cycles.c"


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    raise SystemExit(1)


def main() -> int:
    if not HEADER.exists() or not CYCLES.exists():
        fail(f"psxrecomp source tree not available under {ROOT}")
    header = HEADER.read_text(encoding="utf-8")
    cycles = CYCLES.read_text(encoding="utf-8")

    if header.count("psx_oc_apply(") != 3:
        fail("overclock helper is referenced outside its declaration and CPU charge path")
    if "cycles = psx_oc_apply(cycles);" not in header:
        fail("CPU retirement no longer applies the overclock scale")
    if "psx_cycle_count += (uint64_t)cycles;" not in header:
        fail("raw device-service cycle path is missing")
    if "psx_cycle_count += (uint64_t)psx_oc_apply(b);" not in header:
        fail("batched CPU charges no longer apply the overclock scale")

    # The device fan-out must consume raw guest-cycle deltas.
    advance = re.search(r"static void advance_devices\(uint32_t c\) \{(.*?)\n\}", cycles, re.S)
    if not advance:
        fail("advance_devices() not found")
    body = advance.group(1)
    expected = ["psx_cycle_count += (uint64_t)c;", "cdrom_advance(c);",
                "dma_advance(c);", "timers_advance(c);"]
    for text in expected:
        if text not in body:
            fail(f"device service boundary missing raw call: {text}")
    if "psx_oc_apply" in body:
        fail("device fan-out applies the CPU overclock")

    print("PASS: 900% scales CPU retirement only; VBlank/CDROM/DMA/timers remain raw-cycle scheduled")
    print(f"  source: {HEADER.relative_to(ROOT.parent)}; {CYCLES.relative_to(ROOT.parent)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
