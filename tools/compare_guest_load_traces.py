"""Compare the two focused normal/accelerated load gates.

The gates intentionally test load results, not whole-run identity:
``startup-card`` compares the memory-card load endpoint; ``race-load`` compares
the loaded-race endpoint and its short post-load continuation.
"""
import argparse
import json
from pathlib import Path


def load(path: Path):
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def compare(label, left, right, keys):
    if isinstance(left, dict) and isinstance(right, dict):
        left = left.get("entries", left)
        right = right.get("entries", right)
    if len(left) != len(right):
        return f"{label}: entry count differs ({len(left)} != {len(right)})"
    for i, (a, b) in enumerate(zip(left, right)):
        for key in keys:
            if a.get(key) != b.get(key):
                return f"{label}: entry {i} field {key} differs ({a.get(key)!r} != {b.get(key)!r})"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gate", required=True,
                    choices=("startup-card", "race-load"),
                    help="startup-card: memory-card result; race-load: race result")
    ap.add_argument("--normal", required=True, type=Path)
    ap.add_argument("--accelerated", required=True, type=Path)
    ap.add_argument("--kind", choices=("fingerprint", "card", "cd"), required=True)
    args = ap.parse_args()
    keys = {
        "fingerprint": ("frame", "wr", "pc", "wc", "mmio", "mc", "sp", "sc", "cyc"),
        # Trace sequence numbers are process-local ring counters.  List order
        # remains significant, while the counter itself must not be treated as
        # guest-visible state across separate runs.
        "card": ("slot", "cmd", "sector", "checksum", "data_idx", "data_peek"),
        "cd": ("kind", "addr", "val", "w", "frame", "stat", "request", "sector_pos", "read_cmd"),
    }[args.kind]
    allowed = {
        "startup-card": {"card"},
        "race-load": {"fingerprint", "cd"},
    }
    if args.kind not in allowed[args.gate]:
        raise SystemExit(
            f"{args.gate} gate does not accept --kind {args.kind}; "
            f"use one of: {', '.join(sorted(allowed[args.gate]))}"
        )
    error = compare(f"{args.gate}/{args.kind}",
                    load(args.normal), load(args.accelerated), keys)
    if error:
        raise SystemExit(f"FAIL {error}")
    print(f"PASS {args.gate}: normal and accelerated load result matches ({args.kind})")


if __name__ == "__main__":
    main()
