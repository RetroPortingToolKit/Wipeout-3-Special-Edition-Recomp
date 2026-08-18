"""Clear pack ink that lands in a font atlas's cell gaps.

The game addresses a glyph by a UV rect whose bounds come from the bitmap's own
cell structure: within a row band, glyphs are separated by columns that are
entirely colour-index 0. Those gap columns are load-bearing — a quad's sampling
bounds sit on them, and anything drawn there shows up outside the glyph the game
asked for.

princepeter64's pack redraws the glyphs at 16x, and in a few places its ink
crosses into a gap column. On the original that column is empty, so the game
shows background; with the replacement it shows a slice of the neighbouring
letter — the stray marks that appear after specific glyphs (O, E, o) and
nowhere else.

This masks each replacement against its own dumped original: for every source
column that is blank across a row band, the corresponding 16x block is cleared.
It can only ever REMOVE ink the game's texture does not have, never add or move
anything, so it cannot change a glyph the pack drew correctly.

Usage:
    python clean_pack_gaps.py <dump-dir> <pack-dir> <texhash> [<texhash>...]
"""
import os
import sys

from PIL import Image


def ink_bands(px, w, h):
    """Contiguous y ranges containing ink — the atlas's glyph rows."""
    rows = [any(px[x, y] > 8 for x in range(w)) for y in range(h)]
    out, y = [], 0
    while y < h:
        if rows[y]:
            s = y
            while y < h and rows[y]:
                y += 1
            out.append((s, y))
        else:
            y += 1
    return out


def gap_columns(px, w, bands):
    """(y0, y1, [x, ...]) per band: source columns blank for that whole band."""
    for (y0, y1) in bands:
        cols = [x for x in range(w)
                if not any(px[x, y] > 8 for y in range(y0, y1))]
        yield y0, y1, cols


def clean(dump_dir, pack_dir, tex):
    srcs = [f for f in os.listdir(dump_dir) if f.startswith(tex + "-")]
    if not srcs:
        print("%s: no dump to derive cell structure from — skipped" % tex)
        return
    # Geometry is identical across palette variants, so any dump is authority.
    src = Image.open(os.path.join(dump_dir, srcs[0])).convert("RGBA")
    w, h = src.size
    sp = src.getchannel("A").load()
    bands = ink_bands(sp, w, h)
    gaps = list(gap_columns(sp, w, bands))

    targets = sorted(f for f in os.listdir(pack_dir)
                     if f.startswith(tex + "-") and f.endswith(".png"))
    for t in targets:
        tpath = os.path.join(pack_dir, t)
        rep = Image.open(tpath).convert("RGBA")
        if rep.width % w or rep.height % h:
            print("   %s: %dx%d is not an integer upscale of %dx%d — skipped"
                  % (t, rep.width, rep.height, w, h))
            continue
        S = rep.width // w
        alpha = rep.getchannel("A")
        ap = alpha.load()
        cleared = 0
        for (y0, y1, cols) in gaps:
            for x in cols:
                for yy in range(y0 * S, min(y1 * S, rep.height)):
                    for xx in range(x * S, min((x + 1) * S, rep.width)):
                        if ap[xx, yy] > 8:
                            ap[xx, yy] = 0
                            cleared += 1
        # Rows work the same way and for the same reason: a source row that is
        # blank right across separates one band from the next, and pack ink
        # there bleeds a slice of the row above or below into the glyph the
        # game asked for. Same safety argument, other axis.
        blank_rows = [y for y in range(h)
                      if not any(sp[x, y] > 8 for x in range(w))]
        for y in blank_rows:
            for yy in range(y * S, min((y + 1) * S, rep.height)):
                for xx in range(rep.width):
                    if ap[xx, yy] > 8:
                        ap[xx, yy] = 0
                        cleared += 1
        if cleared:
            rep.putalpha(alpha)
            rep.save(tpath)
        print("   %s: cleared %d stray px" % (t, cleared))


if __name__ == "__main__":
    dump_dir, pack_dir = sys.argv[1], sys.argv[2]
    for tex in sys.argv[3:]:
        print("=== %s" % tex)
        clean(dump_dir, pack_dir, tex)
