#!/usr/bin/env python3
"""Reduce psxrecomp freeze dumps to a compact, reviewable JSON receipt.

Freeze dumps intentionally contain large trace rings.  This tool reads only the
small scalar header from every input and fully parses the newest dump so a run
can be diagnosed before the generated multi-gigabyte captures are removed.
"""

from __future__ import annotations

import argparse
import glob
import json
from pathlib import Path
import sys


HEADER_SENTINEL = b'  "caps":'
DETAIL_TAILS = {
    "heartbeat_ring": 16,
    "frame_history": 16,
    "dirty_block": 32,
    "main_stack_samples": 1,
}


def read_scalar_header(path: Path) -> dict[str, object]:
    with path.open("rb") as stream:
        prefix = stream.read(256 * 1024)
    stop = prefix.find(HEADER_SENTINEL)
    if stop < 0:
        raise ValueError(f"{path}: scalar header sentinel not found")
    text = prefix[:stop].decode("utf-8")
    text = text.rstrip().removesuffix(",") + "\n}"
    header = json.loads(text)
    header["path"] = str(path.resolve())
    header["bytes"] = path.stat().st_size
    return header


def newest_detail(path: Path) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as stream:
        dump = json.load(stream)
    detail: dict[str, object] = {
        "path": str(path.resolve()),
        "cpu": dump.get("cpu"),
        "ram_peeks": dump.get("ram_peeks"),
        "caps": dump.get("caps"),
    }
    for key, count in DETAIL_TAILS.items():
        value = dump.get(key)
        detail[f"{key}_tail"] = value[-count:] if isinstance(value, list) else []
    return detail


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "dumps",
        nargs="+",
        help="dump paths or non-recursive glob patterns",
    )
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument(
        "--delete-after",
        action="store_true",
        help="delete the exact captured inputs after a durable --output receipt",
    )
    return parser.parse_args(argv)


def resolve_inputs(values: list[str]) -> list[Path]:
    expanded: list[Path] = []
    for value in values:
        matches = [Path(item) for item in glob.glob(value)]
        expanded.extend(matches if matches else [Path(value)])
    return sorted(
        {path.resolve() for path in expanded if path.is_file()},
        key=lambda path: (path.stat().st_mtime_ns, path.name),
    )


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    paths = resolve_inputs(args.dumps)
    if not paths:
        print("no freeze dumps found", file=sys.stderr)
        return 2

    headers = [read_scalar_header(path) for path in paths]
    receipt = {
        "schema": 1,
        "files": len(paths),
        "total_bytes": sum(path.stat().st_size for path in paths),
        "headers": headers,
        "newest": newest_detail(paths[-1]),
    }
    rendered = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.output:
        output = args.output.resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8", newline="\n")
        # Read the receipt back before destructive cleanup.  Inputs must be
        # direct siblings with the expected generated basename, never a broad
        # directory or a recursively discovered path.
        if args.delete_after:
            durable = json.loads(output.read_text(encoding="utf-8"))
            expected_bytes = sum(path.stat().st_size for path in paths)
            if durable.get("files") != len(paths) or durable.get("total_bytes") != expected_bytes:
                raise RuntimeError("refusing cleanup: receipt does not match inputs")
            parent = paths[0].parent
            if any(
                path.parent != parent
                or not path.name.startswith("psx_freeze_dump_")
                or path.suffix != ".json"
                or path == output
                for path in paths
            ):
                raise RuntimeError("refusing cleanup: unsafe dump path set")
            for path in paths:
                path.unlink()
            remaining = [path for path in paths if path.exists()]
            if remaining:
                raise RuntimeError(f"cleanup incomplete: {len(remaining)} inputs remain")
            print(json.dumps({"deleted_files": len(paths), "deleted_bytes": expected_bytes}))
    else:
        if args.delete_after:
            raise ValueError("--delete-after requires --output")
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
