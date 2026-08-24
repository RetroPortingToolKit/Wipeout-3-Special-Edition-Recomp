#!/usr/bin/env python3
"""Merge immutable overlay-capture contributions by exact RAM image.

Runtime history can contain several observations of the same overlay bytes,
each with different dispatch roots.  Unioning only records with identical
``bytes_b64`` keeps code generations separate while making the compiler see
the complete bounded root set for that generation.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


LIST_FIELDS = ("executed_pcs", "dispatch_entry_pcs", "function_entry_pcs", "seeds")


def merge(paths: list[Path]) -> list[dict]:
    merged: dict[tuple[str, int, str], dict] = {}
    for path in paths:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(payload, dict):
            payload = [payload]
        for cap in payload:
            key = (str(cap.get("load_addr", "")), int(cap.get("size", 0)),
                   str(cap.get("bytes_b64", "")))
            if not key[0] or not key[2]:
                continue
            dst = merged.setdefault(key, dict(cap))
            for field in LIST_FIELDS:
                values = {str(v) for v in dst.get(field, [])}
                values.update(str(v) for v in cap.get(field, []))
                dst[field] = sorted(values, key=lambda v: int(v, 0))
    return sorted(merged.values(), key=lambda cap: (int(cap["load_addr"], 0),
                                                     cap["size"],
                                                     cap["bytes_b64"]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", action="append", required=True,
                        help="capture JSON file or directory; repeatable")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    paths: list[Path] = []
    for raw in args.input:
        path = Path(raw)
        if path.is_dir():
            paths.extend(sorted(path.glob("*.json")))
        else:
            paths.append(path)
    result = merge(paths)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"merged_captures={len(result)} source_files={len(paths)} output={output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
