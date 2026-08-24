#!/usr/bin/env python3
"""Rank uncataloged PSX function targets by generated call frequency."""

from __future__ import annotations

import argparse
import re
import tomllib
from collections import Counter
from pathlib import Path


CALL_RE = re.compile(rb"CPS jal -> 0x([0-9A-Fa-f]{8})")
SEED_RE = re.compile(r"0x([0-9A-Fa-f]{8})")


def cataloged_addresses(symbols_path: Path) -> set[int]:
    with symbols_path.open("rb") as stream:
        document = tomllib.load(stream)
    return {
        int(entry["pc"])
        for section in ("func", "block")
        for entry in document.get(section, [])
    }


def seed_addresses(seed_path: Path) -> set[int]:
    addresses: set[int] = set()
    for line in seed_path.read_text(encoding="utf-8").splitlines():
        match = SEED_RE.search(line)
        if match:
            addresses.add(int(match.group(1), 16))
    return addresses


def generated_call_counts(generated_dir: Path) -> Counter[int]:
    counts: Counter[int] = Counter()
    for path in sorted(generated_dir.glob("*.c")):
        for match in CALL_RE.finditer(path.read_bytes()):
            counts[int(match.group(1), 16)] += 1
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbols", type=Path, default=Path("symbols.toml"))
    parser.add_argument("--seeds", type=Path, default=Path("seeds/ghidra_funcs.txt"))
    parser.add_argument("--generated", type=Path, default=Path("generated"))
    parser.add_argument("--limit", type=int, default=30)
    args = parser.parse_args()

    known = cataloged_addresses(args.symbols)
    seeds = seed_addresses(args.seeds)
    counts = generated_call_counts(args.generated)
    candidates = [
        (count, address)
        for address, count in counts.items()
        if address not in known
    ]
    candidates.sort(key=lambda item: (-item[0], item[1]))

    print("address,generated_calls,ghidra_seed")
    for count, address in candidates[: max(args.limit, 0)]:
        print(f"0x{address:08X},{count},{str(address in seeds).lower()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
