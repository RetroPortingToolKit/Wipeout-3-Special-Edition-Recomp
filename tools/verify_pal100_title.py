#!/usr/bin/env python3
"""Verify the authenticated PAL 100 Hz title-side timing surface.

This is intentionally a verifier, not a patcher.  The title transformation is
user-media-derived; the checks below pin the exact local PAL executable and the
MIPS instructions recovered from its generated/recompiler data.  The runtime
PAL-x2 hook is auto-selected for this authenticated target, but activation
remains gated until the PAL game display mode is programmed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import sys
import tomllib


BASE_ADDRESS = 0x80010000
EXE_HEADER_SIZE = 2048
TRACK_SHA256 = "397b8a30f401bbd9d12904d6054946474ce4746bdf19565a8db534d65ba179e1"
EXE_SHA256 = "7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac"

# Exact words from the local PAL executable / generated dispatch.  Together
# they implement the title's PAL 25/50/100 selector path:
#   selector 0 -> gSimVblankSkip=2
#   selector 1 -> gSimVblankSkip=1, PAL-x2 state=0
#   selector 2 -> gSimVblankSkip=1, PAL-x2 state=1
TITLE_WORDS = {
    0x8017BB8C: 0x8E0D0008,  # load selector from 0x8017B8EC + 8
    0x8017BB90: 0x3C098018,
    0x8017BB94: 0x2DAC0001,
    0x8017BB98: 0x258C0001,
    0x8017BB9C: 0xAD2CF02C,  # store gSimVblankSkip at 0x8017F02C
    0x8017BBA0: 0x2DAC0002,
    0x8017BBA4: 0x398C0001,
    0x8017BBA8: 0x3C0A8017,
    0x8017BBAC: 0xAD4C5064,  # store PAL-x2 state at 0x80175064
}

TITLE_STRINGS = {
    0x80174DEC: b"100HZ",
    0x80174FB0: b"50HZ",
}


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_offset(address: int) -> int:
    return EXE_HEADER_SIZE + address - BASE_ADDRESS


def selector_semantics(selector: int) -> tuple[int, int]:
    skip = 2 if selector < 1 else 1
    pal_x2_state = 0 if selector < 2 else 1
    return skip, pal_x2_state


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()
    root = args.root.resolve()

    manifest_path = root / "mods/preloaded/packages/wipeout3.enhancement.framerate-pal100/1.0.0/manifest.toml"
    exe_path = root / "disc/SCES_028.45"
    track_path = root / "disc/WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It) (Track 01).bin"
    failures: list[str] = []

    if not manifest_path.is_file():
        failures.append(f"missing package manifest: {manifest_path}")
        manifest = {}
    else:
        manifest = tomllib.loads(manifest_path.read_text(encoding="utf-8"))

    targets = manifest.get("target", [])
    target = targets[0] if targets else {}
    if target.get("game_id") != "SCES-02845":
        failures.append("PAL100 package target is not SCES-02845")
    if target.get("disc_sha256") != TRACK_SHA256:
        failures.append("PAL100 package track hash does not match the authenticated local target")
    if target.get("exe_sha256") != EXE_SHA256:
        failures.append("PAL100 package executable hash does not match the authenticated local target")
    feature = next((item for item in manifest.get("feature", []) if item.get("id") == "framerate-pal100"), {})
    if feature.get("default_enabled") is not True:
        failures.append("PAL100 feature is not auto-enabled")
    plugins = {item.get("id") for item in manifest.get("plugin", [])}
    if "wipeout3.framerate.pal-x2" not in plugins:
        failures.append("PAL100 package does not select the PAL-x2 activation hook")

    if not track_path.is_file():
        failures.append(f"missing PAL Track 01: {track_path}")
    elif sha256(track_path) != TRACK_SHA256:
        failures.append("local PAL Track 01 hash differs from the authenticated target")

    exe = b""
    if not exe_path.is_file():
        failures.append(f"missing PAL executable: {exe_path}")
    else:
        exe = exe_path.read_bytes()
        if sha256(exe_path) != EXE_SHA256:
            failures.append("local PAL executable hash differs from the authenticated target")

    for address, expected in TITLE_WORDS.items():
        offset = file_offset(address)
        actual = struct.unpack_from("<I", exe, offset)[0] if offset + 4 <= len(exe) else None
        if actual != expected:
            failures.append(f"0x{address:08X}: expected 0x{expected:08X}, got {actual!r}")

    for address, expected in TITLE_STRINGS.items():
        offset = file_offset(address)
        actual = exe[offset : offset + len(expected)]
        if actual != expected:
            failures.append(f"0x{address:08X}: expected {expected!r}, got {actual!r}")

    result = {
        "ok": not failures,
        "track_sha256": sha256(track_path) if track_path.is_file() else None,
        "exe_sha256": sha256(exe_path) if exe_path.is_file() else None,
        "title_words": len(TITLE_WORDS),
        "title_strings": len(TITLE_STRINGS),
        "selector_semantics": {
            selector: {"gSimVblankSkip": skip, "pal_x2_state": state}
            for selector, (skip, state) in ((0, selector_semantics(0)), (1, selector_semantics(1)), (2, selector_semantics(2)))
        },
        "failures": failures,
    }
    if args.as_json:
        print(json.dumps(result, sort_keys=True))
    elif failures:
        print("PAL100 title verification: FAIL")
        for failure in failures:
            print(f"- {failure}")
    else:
        print("PAL100 title verification: PASS")
        print("- authenticated Track 01 and executable hashes")
        print(f"- {len(TITLE_WORDS)} exact selector/state instruction words")
        print(f"- {len(TITLE_STRINGS)} exact PAL title-rate strings")
        print("- selector 2 => gSimVblankSkip=1, pal_x2_state=1")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
