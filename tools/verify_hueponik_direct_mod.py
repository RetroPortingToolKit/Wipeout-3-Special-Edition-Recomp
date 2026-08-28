#!/usr/bin/env python3
"""Prove the direct Hueponik package reproduces the pinned patched image.

This verifier is deliberately read-only.  It applies every guarded disc_user
overlay to the verified stock Track 01 user stream and every guarded main_exe
patch to the stock boot executable, then compares both results byte-for-byte
with the official xdelta output supplied by the caller.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import tomllib
from pathlib import Path


SECTOR = 2352
USER_OFFSET = 24
USER_SIZE = 2048
EXE_HEADER = 2048
LOAD_ADDRESS = 0x80010000


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def user_stream(raw: bytes) -> bytes:
    if len(raw) % SECTOR:
        raise SystemExit(f"raw Track 01 size {len(raw)} is not sector aligned")
    return b"".join(
        raw[offset + USER_OFFSET : offset + USER_OFFSET + USER_SIZE]
        for offset in range(0, len(raw), SECTOR)
    )


def manifest_bytes(value: str) -> bytes:
    try:
        return bytes.fromhex(value)
    except ValueError as exc:
        raise SystemExit(f"manifest contains invalid hexadecimal bytes: {exc}") from exc


def verify(args: argparse.Namespace) -> dict[str, object]:
    manifest_path = args.manifest.resolve()
    package_dir = manifest_path.parent
    manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    aot_manifest_path = package_dir / "aot_patches.toml"
    aot_manifest = tomllib.loads(aot_manifest_path.read_text(encoding="utf-8"))
    stock_raw = args.stock.read_bytes()
    patched_raw = args.patched.read_bytes()
    stock_exe = args.stock_exe.read_bytes()
    stock_user = user_stream(stock_raw)
    patched_user = user_stream(patched_raw)

    if len(stock_raw) != len(patched_raw):
        raise SystemExit("stock and patched Track 01 sizes differ")

    targets = manifest.get("target", [])
    if len(targets) != 1:
        raise SystemExit("direct package must declare exactly one target")
    target = targets[0]
    if target.get("disc_sha256") != sha256(stock_raw):
        raise SystemExit("manifest stock disc target digest mismatch")
    if target.get("exe_sha256") not in (None, sha256(stock_exe)):
        raise SystemExit("manifest stock executable target digest mismatch")

    rebuilt_user = bytearray(stock_user)
    overlays = manifest.get("overlay", [])
    if manifest.get("patch"):
        raise SystemExit("runtime PAL100 package must not dirty main_exe pages")
    for overlay in overlays:
        if overlay.get("target") != "disc_user":
            raise SystemExit("PAL100 direct package contains a non-disc_user overlay")
        offset = int(overlay["offset"])
        payload_path = package_dir / overlay["file"]
        payload = payload_path.read_bytes()
        end = offset + len(payload)
        expected = bytes(rebuilt_user[offset:end])
        if sha256(expected) != overlay["expected_sha256"]:
            raise SystemExit(f"overlay guard mismatch at user offset {offset}")
        if sha256(payload) != overlay["sha256"]:
            raise SystemExit(f"overlay payload digest mismatch: {payload_path}")
        rebuilt_user[offset:end] = payload

    if bytes(rebuilt_user) != patched_user:
        raise SystemExit("disc_user overlays do not reproduce the official patched image")

    exe_user_offset = stock_user.find(stock_exe)
    if exe_user_offset < 0:
        raise SystemExit("stock executable not found in stock Track 01 user data")
    expected_patched_exe = patched_user[
        exe_user_offset : exe_user_offset + len(stock_exe)
    ]
    rebuilt_exe = bytearray(stock_exe)
    if aot_manifest.get("stock_exe_sha256") != sha256(stock_exe):
        raise SystemExit("AOT manifest stock executable digest mismatch")
    patches = aot_manifest.get("patch", [])
    for patch in patches:
        if patch.get("target") != "main_exe":
            raise SystemExit("PAL100 direct package contains a non-main_exe patch")
        address = int(patch["address"])
        expected = manifest_bytes(patch["expected"])
        replacement = manifest_bytes(patch["replace"])
        if len(expected) != len(replacement):
            raise SystemExit(f"main_exe patch size mismatch at 0x{address:08X}")
        offset = EXE_HEADER + address - LOAD_ADDRESS
        end = offset + len(expected)
        if bytes(rebuilt_exe[offset:end]) != expected:
            raise SystemExit(f"main_exe guard mismatch at 0x{address:08X}")
        rebuilt_exe[offset:end] = replacement

    if bytes(rebuilt_exe) != expected_patched_exe:
        raise SystemExit("main_exe patches do not reproduce the official patched executable")

    return {
        "status": "ok",
        "package": f"{manifest['id']}@{manifest['version']}",
        "disc_overlays": len(overlays),
        "main_exe_patches": len(patches),
        "stock_track_sha256": sha256(stock_raw),
        "patched_track_sha256": sha256(patched_raw),
        "patched_user_sha256": sha256(patched_user),
        "patched_exe_sha256": sha256(expected_patched_exe),
    }


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--stock-exe", type=Path, required=True)
    parser.add_argument("--patched", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=root
        / "mods"
        / "preloaded"
        / "packages"
        / "wipeout3.enhancement.framerate-pal100"
        / "1.1.0"
        / "manifest.toml",
    )
    args = parser.parse_args()
    print(json.dumps(verify(args), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
