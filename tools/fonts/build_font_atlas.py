"""Render a high-resolution replacement for one of WipEout 3's bitmap font
atlases, from the vector F500 Ang-ular typeface.

The replacement has to land on the SAME cells the game samples, or every string
in the game shifts. Those cells were read off the running game
(`tex_pack sub=uvs`, which records the UV rect of every textured draw) and
cross-checked against the dumped bitmap: within a row band each glyph is
separated by exactly one blank column, and a cell is (ink_start-1, ink_end).

So per row we: segment the original ink runs, derive scale and baseline from a
reference glyph's ink box, then draw each character with the vector font so its
rendered ink box lands on the original's, multiplied by SCALE. Rows of custom
symbols (arrows, brackets) have no counterpart in the typeface and are
upscaled from the original pixels instead.

The output is a pure COVERAGE mask: RGB is the source ink colour but alpha runs
the full 0-255, carrying only how much of the pixel the glyph covers. It is NOT
the CLUT's alpha. build_all.py scales this coverage by each palette variant's
own alpha exactly once, when it tints. Baking the CLUT alpha in here too would
apply it twice — and since build_all re-derives the colour from the file it is
about to overwrite, a second run would halve it again, and a third would erase
the font. Alpha is meaning here, not just opacity (the shader reads >191 as
STP-clear, less as STP-set), so a halved alpha silently flips glyphs into the
semi-transparency blend path.
"""
import json
import sys
from PIL import Image, ImageDraw, ImageFont

SCALE = 16          # 256x56 -> 4096x896, matching the HD pack's own upscale
SUPER = 4           # extra oversampling for the glyph raster, downsampled after


def ink_runs(px, w, y0, y1):
    runs, x = [], 0
    while x < w:
        if any(px[x, y][3] > 8 for y in range(y0, y1)):
            xs = x
            while x < w and any(px[x, y][3] > 8 for y in range(y0, y1)):
                x += 1
            runs.append((xs, x - 1))
        else:
            x += 1
    return runs


def ink_box(px, x0, x1, y0, y1):
    """Tight ink bounds of one glyph, in source pixels."""
    xs, ys, xe, ye = None, None, None, None
    for x in range(x0, x1 + 1):
        for y in range(y0, y1):
            if px[x, y][3] > 8:
                xs = x if xs is None else min(xs, x)
                xe = x if xe is None else max(xe, x)
                ys = y if ys is None else min(ys, y)
                ye = y if ye is None else max(ye, y)
    return xs, ys, xe, ye


def glyph_ink_box(font, ch):
    """Tight ink bounds of a rendered character, and where it was drawn."""
    pad = 64
    img = Image.new("L", (pad * 4, pad * 4), 0)
    ImageDraw.Draw(img).text((pad, pad), ch, font=font, fill=255)
    bb = img.getbbox()
    return bb, pad


def to_coverage(img, ink_alpha):
    """Rescale a crop of the source so its ink reads as full coverage.

    Source ink sits at the CLUT's alpha (127 on these atlases). Symbol bands are
    lifted straight from those pixels, so without this they would land in the
    output at 127 while the rendered glyphs land at 255 — a mask that is half
    coverage and half CLUT alpha. Normalising here keeps the whole output on one
    meaning, so build_all's single tint applies cleanly to all of it.
    """
    a = img.getchannel("A")
    img.putalpha(a.point(lambda v: min(255, v * 255 // max(1, ink_alpha))))
    return img


def render_atlas(src_path, out_path, ttf_path, rows, band_h):
    src = Image.open(src_path).convert("RGBA")
    w, h = src.size
    px = src.load()

    # Ink colour + alpha, taken from the source so each palette variant keeps
    # its own colour and the CLUT's semi-transparency bit.
    colour = None
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            if p[3] > 8:
                colour = p
                break
        if colour:
            break
    if colour is None:
        raise SystemExit("no ink found in %s" % src_path)
    print("  ink colour %s" % (colour,))

    out = Image.new("RGBA", (w * SCALE, h * SCALE), (0, 0, 0, 0))

    for bi, chars in enumerate(rows):
        y0, y1 = bi * band_h, min(h, (bi + 1) * band_h)
        runs = ink_runs(px, w, y0, y1)
        if chars is None:
            # Custom symbols: no counterpart in the typeface. Upscale the band.
            band = src.crop((0, y0, w, y1)).resize(
                (w * SCALE, (y1 - y0) * SCALE), Image.NEAREST)
            out.alpha_composite(to_coverage(band, colour[3]), (0, y0 * SCALE))
            print("  band %d: %d symbol glyphs upscaled" % (bi, len(runs)))
            continue
        # A row may be part text, part symbols: the string covers the leading
        # runs and anything past it (arrows, brackets) is upscaled from source,
        # since a text typeface has no glyph for it.
        if len(chars) > len(runs):
            raise SystemExit(
                "band %d: %d characters but only %d ink runs (%r)"
                % (bi, len(chars), len(runs), chars))
        if len(chars) < len(runs):
            tail_x0 = ink_box(px, *runs[len(chars)], y0, y1)[0]
            if tail_x0 is not None:
                tail = src.crop((tail_x0, y0, w, y1)).resize(
                    ((w - tail_x0) * SCALE, (y1 - y0) * SCALE), Image.NEAREST)
                out.alpha_composite(to_coverage(tail, colour[3]),
                                    (tail_x0 * SCALE, y0 * SCALE))
            print("  band %d: %d trailing symbol glyphs upscaled"
                  % (bi, len(runs) - len(chars)))
            runs = runs[:len(chars)]

        # Scale + baseline from a reference glyph: pick the character whose
        # original ink height is the row's modal height, so a descender or a
        # short glyph cannot skew the fit.
        boxes = [ink_box(px, a, b, y0, y1) for a, b in runs]
        heights = [bx[3] - bx[1] + 1 for bx in boxes]
        modal_h = max(set(heights), key=heights.count)
        ref = heights.index(modal_h)
        ref_ch = chars[ref]

        size, best = 8, None
        for trial in range(8, 40 * SUPER):
            f = ImageFont.truetype(ttf_path, trial)
            bb, _ = glyph_ink_box(f, ref_ch)
            if not bb:
                continue
            got = bb[3] - bb[1]
            want = modal_h * SCALE
            if best is None or abs(got - want) < abs(best[1] - want):
                best, size = (trial, got), trial
            if got > want * 1.4:
                break
        font = ImageFont.truetype(ttf_path, size)
        print("  band %d: %d glyphs, ref '%s' h=%d -> font size %d"
              % (bi, len(chars), ref_ch, modal_h, size))

        for (a, b), bx, ch in zip(runs, boxes, chars):
            sx, sy, ex, ey = bx
            bb, pad = glyph_ink_box(font, ch)
            if not bb:
                continue
            g = Image.new("L", (pad * 4, pad * 4), 0)
            ImageDraw.Draw(g).text((pad, pad), ch, font=font, fill=255)
            g = g.crop(bb)
            # Land the rendered ink exactly on the original's ink box.
            tw = (ex - sx + 1) * SCALE
            th = (ey - sy + 1) * SCALE
            g = g.resize((max(1, tw), max(1, th)), Image.LANCZOS)
            tinted = Image.new("RGBA", g.size, colour[:3] + (0,))
            tinted.putalpha(g)   # coverage, not CLUT alpha — see module docstring
            out.alpha_composite(tinted, (sx * SCALE, sy * SCALE))

    out.save(out_path)
    print("  wrote %s  %dx%d" % (out_path, out.width, out.height))


if __name__ == "__main__":
    cfg = json.load(open(sys.argv[1], encoding="utf-8-sig"))
    render_atlas(cfg["src"], cfg["out"], cfg["ttf"],
                 [None if r is None else r for r in cfg["rows"]],
                 cfg["band_h"])
