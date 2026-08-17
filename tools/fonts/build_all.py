"""Generate high-resolution F500 replacements for every palette variant.

The game draws the same glyph bitmap under several CLUTs (white HUD text, grey
menu text, and so on), and a replacement is keyed by <texhash>-<palhash>, so
each variant needs its own file. The GEOMETRY is identical across them — only
the ink colour differs — so render the glyphs once per atlas and recolour.

Each variant's colour is taken from the pack's own existing replacement for
that key, which already has it right. That also means we only emit variants the
pack ships, so we never invent a key the game will not ask for.

Those colours are cached in ink_colours.json and reused on every later run.
That cache is not an optimisation, it is a correctness requirement: we OVERWRITE
the file we would otherwise read the colour back from, so a second run would
sample our own output. Alpha is meaning, not opacity — the shader reads >191 as
STP-clear and anything less as STP-set — so re-deriving would apply the variant
alpha a second time, flip 255 to 127 (opaque glyphs into the semi-transparency
blend path), and halve again on every run until the font disappeared.

Usage:
    python build_all.py <dump-dir> <pack-dir> <ttf> [--dry-run]
"""
import collections
import json
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


INK_CACHE = os.path.join(HERE, "ink_colours.json")


def load_ink_cache():
    if os.path.exists(INK_CACHE):
        return {k: tuple(v) for k, v in json.load(open(INK_CACHE)).items()}
    return {}


def cached_ink(cache, name, path):
    """This variant's CLUT colour, sampled from the pack once and remembered.

    Sampling `path` again after we have written to it would read our own output;
    see the module docstring for why that is destructive rather than merely
    redundant.
    """
    if name not in cache:
        col = ink_colour(path)
        if col is None:
            return None
        cache[name] = col
        json.dump({k: list(v) for k, v in cache.items()},
                  open(INK_CACHE, "w"), indent=2, sort_keys=True)
        print("   %s: ink %s recorded in ink_colours.json" % (name, col))
    return cache[name]


def main():
    dump_dir, pack_dir, ttf = sys.argv[1], sys.argv[2], sys.argv[3]
    dry = "--dry-run" in sys.argv
    cache = load_ink_cache()

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
            col = cached_ink(cache, t, tpath)
            if not col:
                print("   %s: no ink found, skipped" % t)
                continue
            tinted = Image.new("RGBA", glyphs.size, col[:3] + (0,))
            # Scale coverage by this variant's own alpha, so the CLUT's
            # semi-transparency bit survives exactly as the dumper encoded it.
            # `alpha` is pure coverage (build_font_atlas leaves it that way), so
            # this is the ONE place the variant alpha is applied.
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
