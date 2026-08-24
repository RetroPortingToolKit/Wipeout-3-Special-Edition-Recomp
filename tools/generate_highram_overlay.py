#!/usr/bin/env python3
"""Reproducibly generate the bounded WipEout 3 high-RAM static overlay."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CAPTURE = ROOT / "build-release" / "overlay_captures-8078.json"
DEFAULT_OUTPUT = ROOT / "generated" / "overlays_highram.c"
DEFAULT_RECOMPILER = (
    ROOT / "psxrecomp" / "recompiler" / "build" / "psxrecomp-game.exe"
)
COMPILER = ROOT / "psxrecomp" / "tools" / "compile_overlays.py"
GAME_TOML = ROOT / "game.toml"
RUNTIME_INCLUDE = ROOT / "psxrecomp" / "runtime" / "include"

PINNED_SHA256 = "0d5fa49ab033c759f18395a6e39c1c24aeb228dd8da6426dadb84c8b853e6808"
CAPTURE_SCHEMA = "psxrecomp overlay capture v2"
REGION = 0x80780000
TARGET = 0x807814D0
PINNED_ROWS = 644
PINNED_TARGET_EXECUTED = 103
PINNED_TARGET_DISPATCH = 96


class GenerationError(RuntimeError):
    pass


def _addresses(row: dict, field: str) -> set[int]:
    try:
        return {int(value, 0) for value in row.get(field, [])}
    except (TypeError, ValueError) as exc:
        raise GenerationError(f"invalid {field} address: {exc}") from exc


def validate_capture(path: Path) -> dict[str, int | str]:
    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise GenerationError(f"cannot read capture corpus {path}: {exc}") from exc
    digest = hashlib.sha256(raw).hexdigest()
    if digest != PINNED_SHA256:
        raise GenerationError(
            f"pinned capture SHA-256 mismatch: expected {PINNED_SHA256}, got {digest}"
        )
    try:
        rows = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GenerationError(f"capture corpus is not valid JSON: {exc}") from exc
    if not isinstance(rows, list) or not rows:
        raise GenerationError("capture corpus must be a non-empty JSON array")

    executed = dispatch = 0
    required = {
        "schema", "load_addr", "size", "bytes_b64", "executed_pcs",
        "dispatch_entry_pcs", "function_entry_pcs", "seeds",
    }
    for index, row in enumerate(rows):
        if not isinstance(row, dict) or not required <= row.keys():
            missing = sorted(required - set(row) if isinstance(row, dict) else required)
            raise GenerationError(f"capture row {index} is malformed; missing {missing}")
        if row["schema"] != CAPTURE_SCHEMA:
            raise GenerationError(
                f"capture row {index} has unsupported schema {row['schema']!r}"
            )
        try:
            load_addr = int(row["load_addr"], 0)
            decoded_size = len(base64.b64decode(row["bytes_b64"], validate=True))
            declared_size = int(row["size"])
        except (TypeError, ValueError) as exc:
            raise GenerationError(f"capture row {index} has invalid scalar data: {exc}") from exc
        if load_addr != REGION:
            raise GenerationError(
                f"capture row {index} is outside bounded region 0x{REGION:08X}"
            )
        if declared_size != decoded_size:
            raise GenerationError(
                f"capture row {index} size mismatch: {declared_size} != {decoded_size}"
            )
        executed += TARGET in _addresses(row, "executed_pcs")
        dispatch += TARGET in _addresses(row, "dispatch_entry_pcs")

    if not executed or not dispatch:
        raise GenerationError(
            f"capture lacks target proof for 0x{TARGET:08X}: "
            f"executed rows={executed}, dispatch rows={dispatch}"
        )
    if (len(rows), executed, dispatch) != (
        PINNED_ROWS, PINNED_TARGET_EXECUTED, PINNED_TARGET_DISPATCH
    ):
        raise GenerationError(
            "pinned capture statistics mismatch: "
            f"rows/executed/dispatch={len(rows)}/{executed}/{dispatch}, expected "
            f"{PINNED_ROWS}/{PINNED_TARGET_EXECUTED}/{PINNED_TARGET_DISPATCH}"
        )
    return {"sha256": digest, "rows": len(rows), "executed": executed,
            "dispatch": dispatch}


def command(capture: Path, recompiler: Path, out_dir: Path) -> list[str]:
    return [
        sys.executable, str(COMPILER), "--captures", str(capture),
        "--game-toml", str(GAME_TOML), "--recompiler", str(recompiler),
        "--runtime-include", str(RUNTIME_INCLUDE), "--project-root",
        str(ROOT / "psxrecomp"), "--out-dir", str(out_dir),
        "--only-region", f"0x{REGION:08X}", "--static", "--cps",
        "--static-symbol-prefix", "highram",
    ]


def verify_generated(path: Path) -> bytes:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise GenerationError(f"compiler did not produce {path}: {exc}") from exc
    text = data.decode("utf-8", errors="replace")
    required = ("psx_overlay_dispatch_highram", f"case 0x{TARGET:08X}")
    missing = [symbol for symbol in required if symbol not in text]
    if not re.search(rf"\bov_highram_[A-Za-z0-9_]*func_{TARGET:08X}\b", text):
        missing.append(f"ov_highram_*func_{TARGET:08X}")
    if missing:
        raise GenerationError(f"generated overlay is missing {', '.join(missing)}")
    if "psx_overlay_dispatch(CPUState" in text:
        raise GenerationError("generated overlay leaked the unprefixed dispatcher")
    return data


def _log_tail(path: Path, lines: int = 60) -> str:
    """Return a bounded diagnostic tail from the very verbose compiler log."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return f"<unable to read compiler log: {exc}>"
    return "\n".join(text.splitlines()[-lines:])


def generate(args: argparse.Namespace) -> int:
    capture = args.capture.resolve()
    output = args.output.resolve()
    recompiler = args.recompiler.resolve()
    stats = validate_capture(capture)
    print("capture verified: " + ", ".join(f"{k}={v}" for k, v in stats.items()))
    if not COMPILER.is_file():
        raise GenerationError(f"overlay compiler not found: {COMPILER}")
    if not recompiler.is_file():
        raise GenerationError(f"recompiler not found: {recompiler}")

    if args.dry_run:
        print(subprocess.list2cmdline(command(capture, recompiler, Path("<temporary>"))))
        return 0

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="highram-overlay-", dir=output.parent) as temp:
        temp_dir = Path(temp)
        cmd = command(capture, recompiler, temp_dir)
        print(subprocess.list2cmdline(cmd))
        compiler_log = temp_dir / "compile_overlays.log"
        with compiler_log.open("w", encoding="utf-8", errors="replace") as log:
            result = subprocess.run(cmd, cwd=ROOT, env={
                key: value for key, value in os.environ.items()
                if key not in {"PSX_OVERLAY_CAPTURES", "PSX_OVERLAY_CACHE_DIR"}
            }, stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            raise GenerationError(
                f"compile_overlays.py failed with exit {result.returncode}:\n"
                f"{_log_tail(compiler_log)}")
        print("overlay compiler completed; diagnostics retained in temporary storage")
        generated = verify_generated(temp_dir / "overlays_static.c")
        if args.check:
            try:
                current = output.read_bytes()
            except OSError:
                current = None
            if current != generated:
                raise GenerationError(f"generated output is stale or missing: {output}")
            print(f"check passed: {output} is reproducible and current")
            return 0
        staging = temp_dir / output.name
        staging.write_bytes(generated)
        os.replace(staging, output)
        print(f"published atomically: {output}")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, default=None,
                        help="path to the pinned capture corpus (useful when it is stored outside the build tree)")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--recompiler", type=Path, default=DEFAULT_RECOMPILER)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--check", action="store_true",
                       help="regenerate in temporary storage and fail if output differs")
    modes.add_argument("--dry-run", action="store_true",
                       help="validate inputs and print the exact compiler command")
    args = parser.parse_args(argv)
    if args.capture is None:
        args.capture = DEFAULT_CAPTURE
    return args


def main(argv: list[str] | None = None) -> int:
    try:
        return generate(parse_args(argv))
    except GenerationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
