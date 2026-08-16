#!/usr/bin/env python3
"""Convert hueponik WipEout 3 SE xdelta patches into declarative .psxmod packages.

derived_disc VCDIFF is conversion scaffolding only. This writes native
main_exe patches (so the recompiled boot EXE is dirtied) and disc_user
overlays (so later CD reads see the same edits).

Also derives an NTSC x2 (120 Hz) package from the unlocked NTSC builds by
retargeting hueponik's 50→60 timing scales to 50→120 and keeping every-vblank
sim. Requires the runtime CRTC refresh multiplier (psx_mod_set_crtc_refresh_multiplier).

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
PATCH_BASE = "https://codeberg.org/hueponik/wip3out-patches/raw/branch/master"
VARIANTS = (
    ("ntsc", "ntsc_track01.xdelta3", "NTSC 30 fps (stock 2 MB)"),
    ("ntsc8", "ntsc8_track01.xdelta3", "NTSC 30 fps enhanced (needs 8 MB RAM)"),
    ("ntscfull8", "ntscfull8_track01.xdelta3", "NTSC 30 fps full (needs 8 MB RAM)"),
    ("pal", "pal_track01.xdelta3", "PAL 25 fps (stock 2 MB)"),
    ("pal8", "pal8_track01.xdelta3", "PAL 25 fps enhanced (needs 8 MB RAM)"),
    ("palfull8", "palfull8_track01.xdelta3", "PAL 25 fps full (needs 8 MB RAM)"),
)
# NTSC sources for 120 Hz derivation (unlocked every-vblank kept).
NTSC120_SOURCES = (
    ("ntsc120", "ntsc", "NTSC x2 120 Hz (stock 2 MB)"),
    ("ntsc1208", "ntsc8", "NTSC x2 120 Hz enhanced (needs 8 MB RAM)"),
    ("ntsc120full8", "ntscfull8", "NTSC x2 120 Hz full (needs 8 MB RAM)"),
)
EXE_GAP = 64
# hueponik unlocks every-vblank sim (2→1 skip + dt). Real PSX/DuckStation stay
# GPU-bound near 25/30 fps; recomp's GPU is instant so that unlock chipmunks
# audio at stock CRTC. Restore stock cadence for the 25/30 package; keep the
# unlock for the NTSC x2 120 Hz package (paired with CRTC multiplier 2).
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

# Retarget unlocked-NTSC timing immediates from 60 Hz world-dt to 120 Hz.
# Fixed-point scales were 50→60 (×5/6); 120 Hz needs ×5/12 from PAL 1.0, i.e.
# half the NTSC value. Frame-count timeouts that grew 50→60 grow again ×2.
NTSC120_WORD_SCALES = (
    # ori $at, dt_scale  (0x6AAB → 0x3555)
    (0x80174FC8, 0x34016AAB, 0x34013555),
    (0x80174FDC, 0x34010DB6, 0x340106DB),
    (0x80174FE8, 0x34010D03, 0x34010681),
    # addiu $a0, frame timeout 1800 → 3600
    (0x801235F4, 0x24040708, 0x24040E10),
)
# In-game menu labels (4-byte slots). Replace the high label first so a
# 30HZ→60HZ rewrite cannot steal the original 60HZ site on enhanced builds.
NTSC120_LABELS = (
    (0x801751B0, b"60HZ", b"120H"),
    (0x80174FB0, b"30HZ", b"60HZ"),
)

PKG_60 = "wipeout3.enhancement.framerate"
PKG_120 = "wipeout3.enhancement.framerate-120"


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


def hex_block(data: bytes) -> str:
    rows = []
    for off in range(0, len(data), 32):
        chunk = data[off : off + 32]
        rows.append(" ".join(f"{b:02x}" for b in chunk))
    return '"""\n' + "\n".join(rows) + '\n"""'


def download(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"download {url}", file=sys.stderr)
    with urllib.request.urlopen(url) as src, dest.open("wb") as out:
        shutil.copyfileobj(src, out)


def apply_xdelta(stock: Path, patch: Path, dest: Path) -> None:
    cmd = ["xdelta3", "-d", "-f", "-s", str(stock), str(patch), str(dest)]
    subprocess.check_call(cmd)


def toml_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def gva_to_exe_off(addr: int) -> int:
    return EXE_HEADER + (addr - LOAD_ADDRESS)


def emit_variant_ops(
    lines: list[str],
    out_dir: Path,
    assets: Path,
    feature: str,
    value: str,
    meta: dict,
) -> None:
    variant_dir = assets / value
    variant_dir.mkdir()
    for lba, count, payload, expected_digest, digest in meta["overlays"]:
        rel = f"assets/{value}/user_{lba:05d}_{count:02d}.overlay"
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
            f'when = {{ build = "{value}" }}',
            "",
        ]
    for addr, expected, replacement in meta["exe_patches"]:
        lines += [
            "[[patch]]",
            f'feature = "{feature}"',
            'target = "main_exe"',
            f"address = 0x{addr:08X}",
            f"expected = {hex_block(expected)}",
            f"replace = {hex_block(replacement)}",
            f'when = {{ build = "{value}" }}',
            "",
        ]


def emit_package_60(out_dir: Path, variants: dict[str, dict]) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    assets = out_dir / "assets"
    assets.mkdir(parents=True)

    lines: list[str] = [
        "format_version = 5",
        f'id = "{PKG_60}"',
        'version = "1.0.14"',
        'name = "WipEout 3 NTSC / Framerate"',
        'author = "hueponik"',
        "description = \"Converted from hueponik SCES-02845 xdelta patches "
        "(plain unlock + AOT skip=2 at SimVblankGate). NTSC keeps "
        "SetVideoMode(NTSC) / 60 Hz CRTC and hueponik's 50→60 timing scales. "
        "Gate entry + VBlank plugins force gSimVblankSkip=2 so AOT runs "
        "every-other-vblank (~30 fps); title Game 60 fps is CRTC, not sim. "
        "PAL ~25 fps. Mutually exclusive with NTSC x2 120 Hz. Enhanced/Full "
        "need 8 MB (not with stock 2 MB builds).\"",
        'license = "See upstream patch author. Conversion is a sparse native overlay of that work."',
        'source_name = "hueponik/wip3out-patches"',
        'source_url = "https://codeberg.org/hueponik/wip3out-patches"',
        'resolver = "declarative"',
        'save_compatibility = "isolated"',
        f'conflicts = ["{PKG_120}"]',
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
        'id = "framerate"',
        'name = "NTSC / Framerate"',
        'description = """Plain hueponik NTSC/PAL plus AOT skip=2 at '
        'SimVblankGate (~30/25 fps). Enable 8 MB only for Enhanced/Full. '
        'Conflicts with NTSC x2 120 Hz."""',
        'group = "Gameplay"',
        "default_enabled = false",
        "",
        "[[plugin]]",
        'feature = "framerate"',
        'id = "wipeout3.framerate.skip2-on"',
        "",
        "[[plugin]]",
        'feature = "framerate"',
        'id = "wipeout3.framerate.skip2"',
        "",
        "[[plugin]]",
        'feature = "framerate"',
        'id = "wipeout3.framerate.skip2-entry"',
        "",
        "[[option]]",
        'feature = "framerate"',
        'id = "build"',
        'label = "Build"',
        'description = "NTSC 30 fps or PAL 25 fps. Enhanced/Full need 8 MB RAM."',
        'group = "Gameplay"',
        'type = "choice"',
        'default = "ntsc"',
        "",
    ]
    for value, _, label in VARIANTS:
        lines += [
            "[[option.choice]]",
            f'value = "{value}"',
            f'label = "{toml_escape(label)}"',
            "",
        ]

    for value, meta in variants.items():
        emit_variant_ops(lines, out_dir, assets, "framerate", value, meta)

    (out_dir / "manifest.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out_dir / "NOTICE.md").write_text(
        "Game edits come from hueponik's WipEout 3 Special Edition patches:\n"
        "https://codeberg.org/hueponik/wip3out-patches\n\n"
        "This package does not redistribute the disc. It stores sparse guarded "
        "writes converted from the published xdelta against a verified SCES-02845 "
        "Track 01 dump. 1.0.14 keeps plain hueponik and forces gSimVblankSkip=2 "
        "from SimVblankGate entry (pre-CPS) plus VBlank backup so AOT cannot "
        "lose a skip=1 stomp race. Title Game 60 fps is CRTC vblanks.\n"
        f"Mutually exclusive with `{PKG_120}`.\n",
        encoding="utf-8",
    )


def emit_package_120(out_dir: Path, variants: dict[str, dict]) -> None:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    assets = out_dir / "assets"
    assets.mkdir(parents=True)

    lines: list[str] = [
        "format_version = 5",
        f'id = "{PKG_120}"',
        'version = "1.0.0"',
        'name = "WipEout 3 NTSC x2 / 120 Hz"',
        'author = "hueponik + retcomm"',
        "description = \"Derived from hueponik's unlocked NTSC builds: every-vblank "
        "sim with timing scales retargeted for 120 Hz, plus the runtime NTSC x2 "
        "CRTC (half guest VBlank period) and 120 Hz host pacing. Experimental. "
        "Mutually exclusive with the stock 25/30 fps framerate package. "
        "Enhanced/Full need 8 MB Main RAM.\"",
        'license = "See upstream patch author. 120 Hz retarget and CRTC x2 are '
        'local conversion/runtime work."',
        'source_name = "hueponik/wip3out-patches"',
        'source_url = "https://codeberg.org/hueponik/wip3out-patches"',
        'resolver = "declarative"',
        'save_compatibility = "isolated"',
        f'conflicts = ["{PKG_60}"]',
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
        'id = "framerate-120"',
        'name = "NTSC x2 120 Hz (Experimental)"',
        'description = """120 guest updates/sec on an NTSC x2 CRTC. Enable 8 MB '
        'Main RAM before Enhanced or Full. Conflicts with the 25/30 fps '
        'framerate package — only one may be enabled."""',
        'group = "Gameplay"',
        "default_enabled = false",
        "",
        "[[option]]",
        'feature = "framerate-120"',
        'id = "build"',
        'label = "Build"',
        'description = "NTSC x2 120 Hz. Enhanced/Full need 8 MB RAM."',
        'group = "Gameplay"',
        'type = "choice"',
        'default = "ntsc120"',
        "",
    ]
    for value, _, label in NTSC120_SOURCES:
        lines += [
            "[[option.choice]]",
            f'value = "{value}"',
            f'label = "{toml_escape(label)}"',
            "",
        ]

    lines += [
        "[[plugin]]",
        'feature = "framerate-120"',
        'id = "wipeout3.framerate.ntsc-x2"',
        "",
    ]

    for value, meta in variants.items():
        emit_variant_ops(lines, out_dir, assets, "framerate-120", value, meta)

    (out_dir / "manifest.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out_dir / "NOTICE.md").write_text(
        "Game edits are derived from hueponik's WipEout 3 Special Edition patches:\n"
        "https://codeberg.org/hueponik/wip3out-patches\n\n"
        "The NTSC x2 / 120 Hz package keeps hueponik's every-vblank unlock, "
        "retargets 60 Hz timing scales to 120 Hz, and relies on psxrecomp's "
        "CRTC refresh multiplier (NTSC x2) plus 120 Hz host pacing.\n"
        f"Mutually exclusive with `{PKG_60}`.\n",
        encoding="utf-8",
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


def apply_ntsc120_retarget(patched_user: bytes, exe_off: int, exe_len: int) -> bytes:
    """Keep unlocked NTSC; retarget dt / timeouts / menu labels for 120 Hz."""
    buf = bytearray(patched_user)
    for addr, expect, replace in NTSC120_WORD_SCALES:
        off = exe_off + gva_to_exe_off(addr)
        cur = struct.unpack_from("<I", buf, off)[0]
        if cur != expect:
            raise SystemExit(
                f"NTSC120 scale site 0x{addr:08X}: expected 0x{expect:08X}, "
                f"found 0x{cur:08X}"
            )
        struct.pack_into("<I", buf, off, replace)
    # Menu code moves between stock/enhanced builds; prefer the known GVA, else
    # rewrite the first matching label inside the boot EXE image.
    exe_bytes = bytes(buf[exe_off : exe_off + exe_len])
    for addr, expect, replace in NTSC120_LABELS:
        off = exe_off + gva_to_exe_off(addr)
        cur = bytes(buf[off : off + len(expect)])
        if cur == expect:
            buf[off : off + len(replace)] = replace
            continue
        rel = exe_bytes.find(expect)
        if rel < 0:
            print(
                f"warning: NTSC120 label {expect!r} not found; leaving as-is",
                file=sys.stderr,
            )
            continue
        abs_off = exe_off + rel
        buf[abs_off : abs_off + len(replace)] = replace
        # Refresh search base after mutation so the next label sees updates.
        exe_bytes = bytes(buf[exe_off : exe_off + exe_len])
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
    patched_exe = patched_user[exe_off : exe_off + len(exe)]
    exe_patches = []
    for off, size in byte_runs(exe, patched_exe, EXE_GAP):
        if off + size <= EXE_HEADER:
            continue
        start = max(off, EXE_HEADER)
        end = off + size
        addr = LOAD_ADDRESS + (start - EXE_HEADER)
        exe_patches.append((addr, exe[start:end], patched_exe[start:end]))
    return {"overlays": overlays, "exe_patches": exe_patches}


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
    NTSC x2 works as pure unlocked + scale retarget; x1 matches that base now.
    Cadence (~30 fps) needs a later AOT-visible approach, not mid-function patches.
    """
    del force_ntsc_video  # hueponik NTSC xdeltas already SetVideoMode(NTSC)
    if len(patched_raw) != len(stock_raw):
        raise SystemExit("patched image size does not match stock Track 01")
    return materialize_variant(
        stock_raw, stock_user, exe, user_stream(patched_raw)
    )


def convert_variant_120(
    stock_raw: bytes,
    stock_user: bytes,
    exe: bytes,
    patched_raw: bytes,
) -> dict:
    if len(patched_raw) != len(stock_raw):
        raise SystemExit("patched image size does not match stock Track 01")
    exe_off = stock_user.find(exe)
    unlocked = user_stream(patched_raw)
    retargeted = apply_ntsc120_retarget(unlocked, exe_off, len(exe))
    return materialize_variant(stock_raw, stock_user, exe, retargeted)


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
        default=root / "mods" / "preloaded" / "packages" / PKG_60 / "1.0.14",
    )
    parser.add_argument(
        "--out-120",
        type=Path,
        default=root / "mods" / "preloaded" / "packages" / PKG_120 / "1.0.0",
    )
    parser.add_argument(
        "--only",
        choices=("all", "60", "120"),
        default="all",
        help="Which packages to regenerate",
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

    # Ensure NTSC (and optionally PAL) xdeltas are applied once.
    patched_paths: dict[str, Path] = {}
    needed = set()
    if args.only in ("all", "60"):
        needed.update(v[0] for v in VARIANTS)
    if args.only in ("all", "120"):
        needed.update(src for _, src, _ in NTSC120_SOURCES)
    for value, patch_name, _label in VARIANTS:
        if value not in needed:
            continue
        patch = args.cache / patch_name
        if not patch.is_file():
            download(f"{PATCH_BASE}/{patch_name}", patch)
        patched = args.cache / f"{value}_track01.bin"
        if not patched.is_file():
            apply_xdelta(args.stock, patch, patched)
        patched_paths[value] = patched

    if args.only in ("all", "60"):
        converted_60 = {}
        for value, _patch_name, _label in VARIANTS:
            converted_60[value] = convert_variant_60(
                stock_raw,
                stock_user,
                exe,
                patched_paths[value].read_bytes(),
                force_ntsc_video=value.startswith("ntsc"),
            )
            print(
                f"{value}: overlays={len(converted_60[value]['overlays'])} "
                f"exe_patches={len(converted_60[value]['exe_patches'])}",
                file=sys.stderr,
            )
        emit_package_60(args.out_60, converted_60)
        print(args.out_60)

    if args.only in ("all", "120"):
        converted_120 = {}
        for value, src, _label in NTSC120_SOURCES:
            converted_120[value] = convert_variant_120(
                stock_raw, stock_user, exe, patched_paths[src].read_bytes()
            )
            print(
                f"{value}: overlays={len(converted_120[value]['overlays'])} "
                f"exe_patches={len(converted_120[value]['exe_patches'])}",
                file=sys.stderr,
            )
        emit_package_120(args.out_120, converted_120)
        print(args.out_120)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
