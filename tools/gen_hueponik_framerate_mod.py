#!/usr/bin/env python3
"""Convert hueponik's WipEout 3 SE NTSC xdelta into the "NTSC Mode" package.

derived_disc VCDIFF is conversion scaffolding only. Every changed Track 01
sector becomes one guarded disc_user overlay. That includes the boot EXE's own
sectors: the BIOS loads SCES_028.45 through CD reads, which the runtime serves
through those overlays, so the EXE edits are encoded exactly ONCE.

Until 1.1.0 the EXE edits were ALSO emitted as main_exe patches guarded by the
stock bytes. The overlay had already changed those bytes by the time the
runtime checked the guards after the BIOS load, so every plan was rejected at
0x80010098 ("expected-byte guard failed; booting unmodified") and the patches
never applied -- the overlay was doing all the work (bead beads-btt8.6).

hueponik publishes six builds (ntsc/pal x stock/8/full8). Since 1.3.0 the
package ships ONE: the plain `ntsc` build (60 Hz, stock 2 MB RAM), which
hueponik's README recommends for real PSX / non-overclocked emulators ("30fps
instead of 25fps"). It is a single hidden, default-on feature ("NTSC Mode")
with no options and no [[requirement]].

1.2.0 shipped the two FULL builds (ntscfull8 / palfull8) behind a Region option
with an unconditional 8 MB [[requirement]]. hueponik targets >=500% overclock
for those; measured 2026-09-26 at stock clock (GP1 display-flip ring, guest
time, 175 s races): stock 25.0 fps, ntscfull8 18.9 / palfull8 19.3 fps (12-25
by scene, flips every 3-4 vblanks), so they were dropped (beads-btt8.6). The
"8" builds also need 8 MB + overclock and are not packaged; plain `pal` is
not packaged either.

Source: https://codeberg.org/hueponik/wip3out-patches
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import urllib.request
from pathlib import Path

SECTOR = 2352
USER = 2048
HEADER = 24
EXE_HEADER = 2048
LOAD_ADDRESS = 0x80010000
DISC_SHA256 = "003bdc41068252e791ea7dd99ba1b94814dc48b967e57a7cdc4bf6e1cf4cb1e9"
EXE_SHA256 = "0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d"
# Pinned: upstream master moved on and, from ac46a5dc ("Collapse number of
# patches"), no longer carries the 8 MB builds. This is the last commit with
# all six xdeltas; the committed assets reproduce from it.
PATCH_COMMIT = "1a630b055c415f62019f5bf3da2a3a0b2a846470"
PATCH_BASE = f"https://codeberg.org/hueponik/wip3out-patches/raw/commit/{PATCH_COMMIT}"
# The one hueponik build packaged: (build, xdelta). The build name survives
# only as the asset directory.
BUILD = "ntsc"
BUILD_PATCH = "ntsc_track01.xdelta3"
# Feature id. Deliberately NOT 1.2.0's "framerate": that feature was visible
# and opt-in, so a state.toml written then can hold `enabled = false`, which
# would override default_enabled forever -- and a hidden feature is not shown
# while disabled, so the player could never switch it back on.
FEATURE_ID = "ntsc"
PKG_VERSION = "1.3.0"
EXE_GAP = 64
# hueponik unlocks every-vblank sim (2→1 skip + dt). Real PSX/DuckStation stay
# GPU-bound near 25/30 fps; recomp's GPU is instant so that unlock chipmunks
# audio at stock CRTC. Restore stock cadence for the 25/30 package.
#
# Retired for package emit (1.0.12+): restore_stock_sim_rate frankensteined
# hueponik NTSC (stock unlock spans + island clamps + FORCE_SKIP2) and froze
# grid AI on recomp while AOT ignored mid-function skip patches (2× pace).
# Kept for reference / a future AOT-visible 30 fps path — do not call from
# convert_variant_60.
FPS_UNLOCK_SPANS = (
    (0x800E99C4, 0x800E99DC),
    (0x801234C0, 0x80123500),
)
# In-game FRAME RATE "60HZ" writes skip=1 every frame. On recomp that
# chipmunks audio; force the same skip=2 as 30HZ / stock. Stock/PAL and
# enhanced builds place the li $t4,1 at different GVAs.
MENU_60HZ_SKIP_SITES = (
    (0x8017BBA0, 0x340C0001, 0x340C0002),  # stock / plain PAL
    (0x8017BB74, 0x340C0001, 0x340C0002),  # enhanced / full
)
# Timing-island countdown: after ~60 frames it force-writes skip=1 even if the
# menu says 30HZ. That is why Start+Select framerate feels like a no-op on recomp.
ISLAND_SKIP1_SITES = (
    (0x801752E4, 0x34030001, 0x34030002),  # li $v1,1 → li $v1,2 before sw skip
)
# Main vblank gate (800EE00C): lw skip / nop → li 2 / sw skip. Hard-locks every-
# other-vblank sim on recomp so island/menu/RAM cannot re-open every-vblank.
# Without this, skip often stays 1 (or 0) and input/sim run at 60 Hz (= double).
FORCE_SKIP2_GATE = (
    (0x800EE00C, bytes.fromhex("2cf0628c00000000"), bytes.fromhex("020002242cf062ac")),
)

PKG_60 = "wipeout3.enhancement.framerate"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def user_stream(raw: bytes) -> bytes:
    nsect = len(raw) // SECTOR
    return b"".join(raw[i * SECTOR + HEADER : i * SECTOR + HEADER + USER] for i in range(nsect))


def changed_lbas(stock_user: bytes, patched_user: bytes) -> list[int]:
    lbas = []
    nsect = min(len(stock_user), len(patched_user)) // USER
    for lba in range(nsect):
        a = stock_user[lba * USER : (lba + 1) * USER]
        b = patched_user[lba * USER : (lba + 1) * USER]
        if a != b:
            lbas.append(lba)
    return lbas


def coalesce_lbas(lbas: list[int]) -> list[tuple[int, int]]:
    if not lbas:
        return []
    runs = []
    start = prev = lbas[0]
    for lba in lbas[1:]:
        if lba == prev + 1:
            prev = lba
            continue
        runs.append((start, prev - start + 1))
        start = prev = lba
    runs.append((start, prev - start + 1))
    return runs


def byte_runs(stock: bytes, patched: bytes, gap: int) -> list[tuple[int, int]]:
    runs: list[tuple[int, int]] = []
    i = 0
    n = min(len(stock), len(patched))
    while i < n:
        if stock[i] == patched[i]:
            i += 1
            continue
        j = i + 1
        while j < n and stock[j] != patched[j]:
            j += 1
        if runs and i - (runs[-1][0] + runs[-1][1]) <= gap:
            start, _ = runs[-1]
            runs[-1] = (start, j - start)
        else:
            runs.append((i, j - i))
        i = j
    return runs


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"download {url}", file=sys.stderr)
    with urllib.request.urlopen(url) as src, dest.open("wb") as out:
        shutil.copyfileobj(src, out)


def apply_xdelta(stock: Path, patch: Path, dest: Path) -> None:
    cmd = ["xdelta3", "-d", "-f", "-s", str(stock), str(patch), str(dest)]
    subprocess.check_call(cmd)


def emit_variant_ops(
    lines: list[str],
    out_dir: Path,
    assets: Path,
    feature: str,
    build: str,
    meta: dict,
) -> None:
    variant_dir = assets / build
    variant_dir.mkdir()
    for lba, count, payload, expected_digest, digest in meta["overlays"]:
        rel = f"assets/{build}/user_{lba:05d}_{count:02d}.overlay"
        (out_dir / rel).write_bytes(payload)
        offset = lba * USER
        lines += [
            "[[overlay]]",
            f'feature = "{feature}"',
            'target = "disc_user"',
            f"offset = {offset}",
            f'file = "{rel}"',
            f'sha256 = "{digest}"',
            f'expected_sha256 = "{expected_digest}"',
            "",
        ]


def emit_package_60(out_dir: Path, meta: dict) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    assets = out_dir / "assets"
    assets.mkdir(parents=True)

    lines: list[str] = [
        "format_version = 7",
        f'id = "{PKG_60}"',
        f'version = "{PKG_VERSION}"',
        'name = "WipEout 3 NTSC Mode"',
        'author = "hueponik"',
        "description = \"Converted from hueponik's SCES-02845 xdelta patches: "
        "his plain NTSC build (60 Hz, stock 2 MB RAM), which he recommends for "
        "an unmodified PlayStation (30 fps instead of 25). Measured here: "
        "races hold 30 fps (stock 25) at normal game speed.\"",
        'license = "See upstream patch author. Conversion is a sparse native overlay of that work."',
        'source_name = "hueponik/wip3out-patches"',
        'source_url = "https://codeberg.org/hueponik/wip3out-patches"',
        'resolver = "declarative"',
        'save_compatibility = "isolated"',
        "",
        "[[author_link]]",
        'name = "hueponik"',
        'url = "https://codeberg.org/hueponik/wip3out-patches"',
        "",
        "[[target]]",
        'game_id = "SCES-02845"',
        f'disc_sha256 = "{DISC_SHA256}"',
        f'exe_sha256 = "{EXE_SHA256}"',
        "",
        "[[feature]]",
        f'id = "{FEATURE_ID}"',
        'name = "NTSC Mode"',
        'description = "Runs the game in NTSC mode (60 Hz) using hueponik\'s '
        'NTSC patch: races hold 30 frames per second instead of the stock 25, '
        'at normal game speed."',
        'group = "Gameplay"',
        "hidden = true",
        "default_enabled = true",
        "",
        "[[plugin]]",
        f'feature = "{FEATURE_ID}"',
        'id = "wipeout3.framerate.skip2-on"',
        "",
        "[[plugin]]",
        f'feature = "{FEATURE_ID}"',
        'id = "wipeout3.framerate.skip2"',
        "",
        "[[plugin]]",
        f'feature = "{FEATURE_ID}"',
        'id = "wipeout3.framerate.skip2-entry"',
        "",
    ]

    emit_variant_ops(lines, out_dir, assets, FEATURE_ID, BUILD, meta)

    # Generated output: LF on every host, so the manifest hash (which the AOT
    # profile pins) does not depend on where the generator ran.
    (out_dir / "manifest.toml").write_text(
        "\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    (out_dir / "NOTICE.md").write_text(
        "Game edits come from hueponik's WipEout 3 Special Edition patches:\n"
        "https://codeberg.org/hueponik/wip3out-patches\n\n"
        "This package does not redistribute the disc. It stores sparse guarded "
        "disc overlays converted from the published xdelta against a verified "
        "SCES-02845 Track 01 dump; the boot EXE's edits arrive through the same "
        "overlays when the BIOS loads it. The wipeout3.framerate.skip2* plugins "
        "force gSimVblankSkip=2 from SimVblankGate entry (pre-CPS) plus a VBlank "
        f"backup. {PKG_VERSION} ships only hueponik's plain NTSC build "
        f"({BUILD}: 60 Hz, stock 2 MB RAM) as one hidden, default-on feature; "
        "the 8 MB full builds and the Region option of 1.2.0 are gone.\n",
        encoding="utf-8",
        newline="\n",
    )


def restore_stock_sim_rate(
    patched_user: bytes,
    exe: bytes,
    exe_off: int,
    *,
    stock_user: bytes | None = None,
    force_ntsc_video: bool = False,
) -> bytes:
    """Retired helper (do not call from convert_variant_60).

    Undo every-vblank unlock / clamp skip=2. On recomp this hybrid froze grid
    AI; AOT ignored mid-function patches so pace stayed 2×. Kept for reference.
    """
    del stock_user  # accepted for API compat with convert_variant_60
    buf = bytearray(patched_user)
    for addr, end in FPS_UNLOCK_SPANS:
        file_off = (addr - LOAD_ADDRESS) + EXE_HEADER
        size = end - addr
        src = exe_off + file_off
        buf[src : src + size] = exe[file_off : file_off + size]
    for addr, expect, replace in (*MENU_60HZ_SKIP_SITES, *ISLAND_SKIP1_SITES):
        off = exe_off + (addr - LOAD_ADDRESS) + EXE_HEADER
        if off + 4 > len(buf):
            continue
        cur = struct.unpack_from("<I", buf, off)[0]
        if cur == expect:
            struct.pack_into("<I", buf, off, replace)
    for addr, expect, replace in FORCE_SKIP2_GATE:
        off = exe_off + (addr - LOAD_ADDRESS) + EXE_HEADER
        if off + len(expect) > len(buf):
            continue
        cur = bytes(buf[off : off + len(expect)])
        if cur == expect:
            buf[off : off + len(replace)] = replace
        else:
            print(
                f"warning: FORCE_SKIP2_GATE 0x{addr:08X}: expected {expect.hex()}, "
                f"found {cur.hex()}",
                file=sys.stderr,
            )
    if force_ntsc_video:
        vm = exe_off + (0x800EDA1C - LOAD_ADDRESS) + EXE_HEADER
        cur = struct.unpack_from("<I", buf, vm)[0]
        if (cur & 0xFFFF0000) in (0x24040000, 0x34040000):
            struct.pack_into("<I", buf, vm, cur & 0xFFFF0000)
    return bytes(buf)


def materialize_variant(
    stock_raw: bytes,
    stock_user: bytes,
    exe: bytes,
    patched_user: bytes,
) -> dict:
    if len(patched_user) != len(stock_user):
        raise SystemExit("patched user stream size does not match stock")
    exe_off = stock_user.find(exe)
    if exe_off < 0:
        raise SystemExit("SCES_028.45 was not found in stock Track 01 user data")
    overlays = []
    for lba, count in coalesce_lbas(changed_lbas(stock_user, patched_user)):
        start = lba * USER
        end = start + count * USER
        payload = patched_user[start:end]
        expected = stock_user[start:end]
        overlays.append(
            (lba, count, payload, sha256_bytes(expected), sha256_bytes(payload))
        )
    # The boot EXE's edits are delivered by the overlays over its sectors (the
    # BIOS loads it with CD reads). Prove that instead of re-encoding them as
    # main_exe patches, which is what made every pre-1.1.0 plan fail its
    # expected-byte guards (beads-btt8.6).
    loaded = bytearray(stock_user)
    for lba, count, payload, _expected, _digest in overlays:
        loaded[lba * USER : lba * USER + count * USER] = payload
    patched_exe = patched_user[exe_off : exe_off + len(exe)]
    if bytes(loaded[exe_off : exe_off + len(exe)]) != patched_exe:
        raise SystemExit("disc overlays do not reproduce the patched boot EXE")
    exe_edits = byte_runs(exe, patched_exe, EXE_GAP)
    return {"overlays": overlays, "exe_edit_runs": len(exe_edits)}


def convert_variant_60(
    stock_raw: bytes,
    stock_user: bytes,
    exe: bytes,
    patched_raw: bytes,
    *,
    force_ntsc_video: bool,
) -> dict:
    """Materialize plain hueponik Track 01 (no skip/cadence frankenstein).

    1.0.7–1.0.11 called restore_stock_sim_rate() to undo every-vblank unlock.
    On recomp that hybrid froze grid AI while AOT still ran unlocked (2× pace).
    Cadence (~30 fps) needs a later AOT-visible approach, not mid-function patches.
    """
    del force_ntsc_video  # hueponik NTSC xdeltas already SetVideoMode(NTSC)
    if len(patched_raw) != len(stock_raw):
        raise SystemExit("patched image size does not match stock Track 01")
    return materialize_variant(
        stock_raw, stock_user, exe, user_stream(patched_raw)
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--stock",
        type=Path,
        default=root
        / "disc"
        / "WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It) (Track 01).bin",
    )
    parser.add_argument("--exe", type=Path, default=root / "disc" / "SCES_028.45")
    parser.add_argument("--cache", type=Path, default=Path("/tmp/wip3out-patches"))
    parser.add_argument(
        "--out-60",
        type=Path,
        default=root / "mods" / "preloaded" / "packages" / PKG_60 / PKG_VERSION,
    )
    args = parser.parse_args()
    if not args.stock.is_file() or not args.exe.is_file():
        print("stock Track 01 / SCES_028.45 missing", file=sys.stderr)
        return 1
    if sha256_file(args.stock) != DISC_SHA256:
        print("stock Track 01 digest is not the supported SCES-02845 image", file=sys.stderr)
        return 1
    if sha256_file(args.exe) != EXE_SHA256:
        print("SCES_028.45 digest does not match the supported dump", file=sys.stderr)
        return 1
    stock_raw = args.stock.read_bytes()
    stock_user = user_stream(stock_raw)
    exe = args.exe.read_bytes()
    args.cache.mkdir(parents=True, exist_ok=True)

    # Apply the NTSC xdelta once.
    patch = args.cache / BUILD_PATCH
    if not patch.is_file():
        download(f"{PATCH_BASE}/{BUILD_PATCH}", patch)
    patched = args.cache / f"{BUILD}_track01.bin"
    if not patched.is_file():
        apply_xdelta(args.stock, patch, patched)

    converted = convert_variant_60(
        stock_raw,
        stock_user,
        exe,
        patched.read_bytes(),
        force_ntsc_video=True,
    )
    print(
        f"{BUILD}: overlays={len(converted['overlays'])} "
        f"boot-EXE edit runs carried by those overlays="
        f"{converted['exe_edit_runs']}",
        file=sys.stderr,
    )
    emit_package_60(args.out_60, converted)
    print(args.out_60)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
