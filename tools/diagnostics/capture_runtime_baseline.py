#!/usr/bin/env python3
"""Capture a compact legacy-runner race baseline from the debug socket.

The quarantined runner exposes very large diagnostic payloads.  This command
keeps only acceptance-relevant counters and the hottest interpreted PCs so a
race run has a durable, reviewable receipt without preserving trace rings.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEBUG_CLIENT = ROOT / "psxrecomp" / "tools" / "debug_client.py"


def _load_debug_client():
    spec = importlib.util.spec_from_file_location(
        "psxrecomp_debug_client", DEBUG_CLIENT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {DEBUG_CLIENT}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _require_ok(name: str, response: dict) -> dict:
    if not isinstance(response, dict) or response.get("ok") is not True:
        raise RuntimeError(f"{name} failed: {response!r}")
    return response


def _pick(source: dict, names: tuple[str, ...]) -> dict:
    return {name: source[name] for name in names if name in source}


def compact_responses(responses: dict[str, dict], top_pc_count: int = 20) -> dict:
    frame = _require_ok("frame", responses["frame"])
    dirty = _require_ok("dirty_ram_stats", responses["dirty_ram_stats"])
    overlay = _require_ok(
        "overlay_loader_status", responses["overlay_loader_status"])
    perf = _require_ok("frame_perf", responses["frame_perf"])
    audio = _require_ok("audio_stats", responses["audio_stats"])
    spu = _require_ok("spu_status", responses["spu_status"])
    cdrom = _require_ok("cdrom_timing", responses["cdrom_timing"])

    per_pc = sorted(
        dirty.get("per_pc", []),
        key=lambda item: (-int(item.get("insns", 0)), item.get("pc", "")),
    )[:top_pc_count]

    return {
        "schema": "wo3se-legacy-race-baseline-v1",
        "classification": "quarantined route evidence; not a correctness oracle",
        "frame": frame.get("frame"),
        "dirty_execution": {
            **_pick(dirty, (
                "blocks_run", "insns_run", "native_handoffs", "guard_yields",
                "aborts", "text_diverged_pages", "text_exact_mismatches",
                "text_native_blocked",
            )),
            "top_pcs": [
                _pick(item, ("pc", "entries", "hits", "insns"))
                for item in per_pc
            ],
        },
        "overlay_loader": _pick(overlay, (
            "active", "registered", "regions", "loads", "dispatch_native",
            "dispatch_interp_fallback", "file_found", "stale_blocked",
            "candidate_overflow", "range_index_overflow",
        )),
        "frame_perf": {
            "samples": perf.get("samples"),
            "all": perf.get("all", {}),
            "native_4_3": perf.get("a4_3", {}),
        },
        "audio": {
            "output": audio.get("out", {}),
            **_pick(audio, ("underruns", "pump_calls", "pump_skips",
                            "queue_hiwater", "queue_lowater")),
        },
        "spu": _pick(spu, (
            "render_frames", "nonzero_frames", "cd_push_frames",
            "cd_underflow_frames", "cd_overflow_frames", "active_mask",
            "peak",
        )),
        "cdrom": _pick(cdrom, (
            "records", "data_sectors", "schedule_exact", "schedule_early",
            "schedule_late", "lost", "dropped", "exposure_over_1x_sector",
        )),
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4370)
    parser.add_argument("--top-pcs", type=int, default=20)
    parser.add_argument("--route-receipt", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--output", "-o", type=Path)
    args = parser.parse_args()
    if args.top_pcs < 0 or args.top_pcs > 256:
        parser.error("--top-pcs must be in [0, 256]")

    debug = _load_debug_client()
    commands = (
        "frame", "dirty_ram_stats", "overlay_loader_status", "frame_perf",
        "audio_stats", "spu_status", "cdrom_timing",
    )
    responses = {
        name: debug.query(args.host, args.port, {"cmd": name})
        for name in commands
    }
    receipt = compact_responses(responses, args.top_pcs)

    if args.route_receipt:
        route = json.loads(args.route_receipt.read_text(encoding="utf-8"))
        receipt["route"] = _pick(route, (
            "route_format", "route_digest", "step_count", "frame_count",
            "completion", "start", "end",
        ))
    if args.screenshot:
        receipt["screenshot"] = {
            "path": str(args.screenshot),
            "sha256": _sha256(args.screenshot),
        }

    encoded = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8", newline="\n")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
