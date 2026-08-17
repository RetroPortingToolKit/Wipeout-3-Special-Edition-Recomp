"""Generate high-resolution F500 replacements for every palette variant.

The game draws the same glyph bitmap under several CLUTs (white HUD text, grey
menu text, and so on), and a replacement is keyed by <texhash>-<palhash>, so
each variant needs its own file. The GEOMETRY is identical across them — only
the ink colour differs — so render the glyphs once per atlas and recolour.

Each variant's colour is taken from the pack's own existing replacement for
that key, which already has it right. That also means we only emit variants the
pack ships, so we never invent a key the game will not ask for.

Usage:
    python build_all.py <dump-dir> <pack-dir> <ttf> [--dry-run]
"""
import collections
import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# Rows per atlas. A string covers the leading glyphs; anything after it is
# upscaled from the source, since a text face has no arrow or bracket glyphs.
# None means the whole row is symbols. Derived with segment_atlas.py and
# cross-checked against the runtime's UV census — the narrow glyphs land where
# the typeface says they should (i and l 2-4 px, 1 narrow, : and . 2 px).
ATLASES = {
    "75f5e38b": {
        "band_h": 18,
        "rows": ["abcdefghijklmnopqrstu", "vwxyz0123456789:./+-%", None],
    },
    "4cb2ec4e": {
        "band_h": 16,
        "rows": ["abcdefghijklmnopqrstuvwx", "yz0123456789:./+-%", None],
    },
}


def ink_colour(path):
    """Most common non-transparent pixel — the CLUT's ink for this variant."""
    im = Image.open(path).convert("RGBA")
    px = im.load()
    w, h = im.size
    step = max(1, w // 300)
    counts = collections.Counter()
    for y in range(0, h, step):
        for x in range(0, w, step):
            p = px[x, y]
            if p[3] > 8:
                counts[p] += 1
    return counts.most_common(1)[0][0] if counts else None


def main():
    dump_dir, pack_dir, ttf = sys.argv[1], sys.argv[2], sys.argv[3]
    dry = "--dry-run" in sys.argv

    for tex, cfg in ATLASES.items():
        # Any dumped palette variant will do as the geometry source: the glyph
        # bitmap is what differs between textures, not between palettes.
        srcs = [f for f in os.listdir(dump_dir) if f.startswith(tex + "-")]
        if not srcs:
            print("%s: no dump to derive geometry from — skipped" % tex)
            continue
        src = os.path.join(dump_dir, srcs[0])

        base = os.path.join(HERE, "_gen_%s.png" % tex)
        cfg_path = os.path.join(HERE, "_gen_%s.json" % tex)
        import json
        json.dump({"src": src, "out": base, "ttf": ttf,
                   "band_h": cfg["band_h"], "rows": cfg["rows"]},
                  open(cfg_path, "w"))
        print("=== %s (geometry from %s)" % (tex, srcs[0]))
        subprocess.run([sys.executable,
                        os.path.join(HERE, "build_font_atlas.py"), cfg_path],
                       check=True)

        glyphs = Image.open(base).convert("RGBA")
        alpha = glyphs.getchannel("A")

        targets = sorted(f for f in os.listdir(pack_dir)
                         if f.startswith(tex + "-") and f.endswith(".png"))
        for t in targets:
            tpath = os.path.join(pack_dir, t)
            col = ink_colour(tpath)
            if not col:
                print("   %s: no ink found, skipped" % t)
                continue
            tinted = Image.new("RGBA", glyphs.size, col[:3] + (0,))
            # Scale coverage by this variant's own alpha, so the CLUT's
            # semi-transparency bit survives exactly as the dumper encoded it.
            tinted.putalpha(alpha.point(lambda v: v * col[3] // 255))
            if dry:
                print("   would write %s  ink=%s" % (t, col))
            else:
                tinted.save(tpath)
                print("   wrote %s  ink=%s  %dx%d"
                      % (t, col, tinted.width, tinted.height))
        os.remove(cfg_path)


if __name__ == "__main__":
    main()
