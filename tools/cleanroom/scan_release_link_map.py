#!/usr/bin/env python3
"""Prove that a release link map contains no forbidden shipping symbols."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

if __package__ in {None, ""}:  # Allow the documented direct-script form.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.cleanroom.gate_common import GateError, compact_json, scan_link_map, write_receipt
else:
    from .gate_common import GateError, compact_json, scan_link_map, write_receipt


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--link-map", type=Path, required=True)
    parser.add_argument("--root", type=Path, help="root used for stable relative receipt paths")
    parser.add_argument("--allowlist", type=Path, help="exact-match v1 allowlist JSON for audit annotation")
    parser.add_argument("--json-out", type=Path, help="write a new compact receipt; existing files are refused")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        receipt = scan_link_map(args.link_map, root=args.root, allowlist_path=args.allowlist)
        if args.json_out:
            write_receipt(args.json_out, receipt)
        sys.stdout.write(compact_json(receipt) + "\n")
        return 0 if receipt["status"] == "pass" else 1
    except GateError as error:
        sys.stderr.write(f"release link-map gate failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
