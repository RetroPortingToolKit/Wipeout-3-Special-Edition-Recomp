#!/usr/bin/env python3
"""Bake a mod package's disc_user overlays into a copy of the disc.

Produces a patched Track 01 bin (Mode2 Form1 user-data writes) plus a cue,
reusing the untouched audio tracks by copying them alongside. The point:
regenerate the AOT build FROM the baked disc so the mod's patched text is
first-class native code — the runtime package's disc overlays then match the
disc bytes exactly (zero divergent text pages, zero overlay dispatch).

Usage:
  python tools/bake_disc_variant.py \
      --manifest mods/preloaded/packages/<id>/<ver>/manifest.toml \
      --build ntsc120full8 \
      --src-cue "<original cue>" \
      --out-dir D:/WipEout3-ntsc120full8
"""
import argparse, os, re, shutil, sys

SECTOR_RAW = 2352
USER_OFF = 24          # Mode2 Form1: sync(12)+header(4)+subheader(8)
USER_LEN = 2048

def parse_overlays(manifest_path, build):
    s = open(manifest_path, encoding="utf-8").read()
    base = os.path.dirname(manifest_path)
    out = []
    for block in re.findall(r"\[\[overlay\]\](.*?)(?=\[\[|\Z)", s, re.S):
        if f'when = {{ build = "{build}" }}' not in block:
            continue
        if 'target = "disc_user"' not in block:
            continue
        off = re.search(r"offset\s*=\s*(\d+)", block)
        f = re.search(r'file\s*=\s*"([^"]+)"', block)
        if not off or not f:
            continue
        out.append((int(off.group(1)), os.path.join(base, f.group(1))))
    return out

def user_write(img: bytearray, user_offset: int, data: bytes):
    """Write data at a user-data byte offset, spanning sectors."""
    pos = 0
    while pos < len(data):
        sector = (user_offset + pos) // USER_LEN
        insec = (user_offset + pos) % USER_LEN
        n = min(USER_LEN - insec, len(data) - pos)
        raw = sector * SECTOR_RAW + USER_OFF + insec
        img[raw:raw + n] = data[pos:pos + n]
        pos += n

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--build", required=True)
    ap.add_argument("--src-cue", required=True)
    ap.add_argument("--out-dir", required=True)
    args = ap.parse_args()

    overlays = parse_overlays(args.manifest, args.build)
    if not overlays:
        sys.exit(f"no disc_user overlays for build {args.build}")
    print(f"{len(overlays)} overlays for build {args.build}")

    src_dir = os.path.dirname(os.path.abspath(args.src_cue))
    cue = open(args.src_cue, encoding="utf-8", errors="replace").read()
    tracks = re.findall(r'FILE\s+"([^"]+)"', cue)
    if not tracks:
        sys.exit("no FILE entries in cue")
    os.makedirs(args.out_dir, exist_ok=True)

    # Track 01 gets patched; the rest are copied verbatim.
    t1 = os.path.join(src_dir, tracks[0])
    print(f"reading {t1}")
    img = bytearray(open(t1, "rb").read())
    total = 0
    for user_offset, path in overlays:
        data = open(path, "rb").read()
        user_write(img, user_offset, data)
        total += len(data)
    print(f"patched {total} bytes across {len(overlays)} overlays")
    out_t1 = os.path.join(args.out_dir, tracks[0])
    open(out_t1, "wb").write(img)
    for t in tracks[1:]:
        dst = os.path.join(args.out_dir, t)
        if not os.path.exists(dst):
            shutil.copyfile(os.path.join(src_dir, t), dst)
            print(f"copied {t}")
    out_cue = os.path.join(args.out_dir, os.path.basename(args.src_cue))
    open(out_cue, "w", encoding="utf-8").write(cue)
    print(f"baked cue: {out_cue}")

if __name__ == "__main__":
    main()
