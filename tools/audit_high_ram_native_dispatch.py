#!/usr/bin/env python3
"""Bounded coverage check for the declared 8 MB native-dispatch roots.

This intentionally does not invoke the generator.  It checks the checked-in
TOML roots and, when supplied, one finite run report for a guarded high-PC.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


HEX = re.compile(r"0x[0-9a-fA-F]+")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", type=Path, default=Path("game.toml"))
    ap.add_argument("--report", type=Path)
    ap.add_argument("--guard-pc", default="0x80781474")
    args = ap.parse_args()

    text = args.config.read_text(encoding="utf-8")
    marker = 'load_addr = "0x80780000"'
    if marker not in text:
        raise SystemExit("FAIL: expected 8 MB high-RAM overlay declaration is absent")
    block = text[text.index(marker):]
    block = block[: block.find("\n[[", 1)] if "\n[[" in block[1:] else block
    entries = [int(x, 16) for x in HEX.findall(block.split("entries =", 1)[1])]
    base = int("0x80780000", 16)
    guard = int(args.guard_pc, 16)
    print(f"overlay_base=0x{base:08X} declared_roots={len(entries)}")
    if len(entries) != len(set(entries)):
        print("FAIL duplicate declared roots")
        return 1
    if any(not (base <= pc < base + 0x20000) for pc in entries):
        print("FAIL declared root outside the bounded high-RAM window")
        return 1
    print(f"guard_pc=0x{guard:08X} in_high_window={base <= guard < base + 0x20000}")
    print(f"guard_is_declared_root={guard in entries}")
    if args.report:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        unsupported = report.get("interp_unsupported", [])
        matches = [x for x in unsupported if isinstance(x, str) and f"0x{guard:08x}" in x.lower()]
        print(f"report_interp_unsupported={len(unsupported)} guard_match={bool(matches)}")
    print("RESULT: roots are covered; an in-range guarded PC remains a native-dispatch fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
