"""Emit a focused PGXP/runtime effectiveness receipt.

This is deliberately conservative: config and manifest values are reported as
configured, while runtime values are only reported as effective when supplied
by a runtime JSON receipt (``--runtime``).  It therefore cannot turn a TOML
static check into a runtime claim.
"""
import argparse
import json
from pathlib import Path
import tomllib


FIELDS = {
    "geometry": ("video", "geometry_correction"),
    "perspective_texture": ("video", "perspective_texturing"),
    "depth": ("video", "pgxp_depth"),
    "cpu_mode": ("video", "pgxp_cpu_mode"),
    "backend": ("video", "renderer"),
}


def read_toml(path):
    with path.open("rb") as stream:
        return tomllib.load(stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path("game.toml"))
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("psxrecomp/mods/builtin/packages/psx.enhancement.pgxp/1.0.0/manifest.toml"),
    )
    parser.add_argument("--runtime", type=Path,
                        help="runtime JSON receipt containing effective values")
    parser.add_argument("--output", type=Path,
                        help="write the JSON receipt here as well as stdout")
    args = parser.parse_args()

    config = read_toml(args.config)
    video = config.get("video", {})
    cull = config.get("widescreen", {}).get("cull", {})
    receipt = {
        "schema": "wipeout3.pgxp-runtime/v1",
        "config": str(args.config),
        "configured": {name: video.get(key) for name, (_, key) in FIELDS.items()},
        "culling": {
            "configured": bool(cull),
            "auto_screen_x": cull.get("auto_screen_x"),
            "keep_rules": len(cull.get("keep", [])),
            "plane_nx_sites": cull.get("plane_nx_sites", []),
        },
        "cpu_data_mode": {
            "cpu_overclock_percent": video.get("cpu_overclock"),
            "recompiler_strict": config.get("recompiler", {}).get("strict"),
            "overlay_cache": config.get("runtime", {}).get("overlay_cache"),
        },
        "vertex_cache_reset": {"status": "unknown", "evidence": None},
        "manifest": {"path": str(args.manifest) if args.manifest else None,
                      "present": bool(args.manifest and args.manifest.exists())},
        "runtime": None,
        "blockers": [],
    }
    if args.manifest and args.manifest.exists():
        manifest = read_toml(args.manifest)
        receipt["manifest"]["default_enabled"] = manifest.get("default_enabled")
    else:
        receipt["blockers"].append("PGXP manifest was not supplied/present")

    if args.runtime:
        runtime = json.loads(args.runtime.read_text(encoding="utf-8"))
        receipt["runtime"] = runtime
        for key in (*FIELDS, "culling", "vertex_cache_reset", "cpu_data_mode"):
            if key in runtime:
                if key in FIELDS:
                    receipt.setdefault("effective", {})[key] = runtime[key]
                elif key == "vertex_cache_reset":
                    receipt["vertex_cache_reset"] = runtime[key]
        receipt["blockers"].append("runtime receipt is externally supplied; verifier does not authenticate its producer")
    else:
        receipt["blockers"].extend([
            "no runtime receipt supplied; configured values are not runtime proof",
            "vertex-cache/reset effectiveness has no exposed config or runtime evidence",
        ])
        if not (Path("psxrecomp/runtime").is_dir() and
                Path("psxrecomp/recompiler").is_dir()):
            receipt["blockers"].append(
                "engine submodules are absent in this checkout, so config-loader/OpenGL call sites cannot be audited here"
            )
    receipt["runtime_proven"] = bool(args.runtime) and not any(
        "not authenticate" in item for item in receipt["blockers"])

    output = json.dumps(receipt, indent=2, sort_keys=True)
    print(output)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
