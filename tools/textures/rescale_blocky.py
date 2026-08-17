"""Redraw pack images that are only nearest-neighbour upscales of the original.

Some entries in princepeter64's pack were never hand-redrawn -- they are the
dumped texture blown up with nearest sampling, which is pixel-for-pixel what the
game would have drawn anyway. They cost a substitution and buy nothing, and at
high internal resolution their staircased diagonals read as "blocky" next to the
genuinely redrawn entries beside them.

Scale2x (AdvMAME2x) is the right tool rather than a smooth resampler, because
alpha here is MEANING, not coverage: the shader discards at a >= 1 and splits
STP at a > 191, so a partial-alpha edge would not antialias, it would flip
texels into the wrong transparency tier. Scale2x only ever COPIES source pixels
-- every output pixel is some input pixel verbatim -- so the exact colour set
and the exact 0/127/255 alpha tiers survive, while staircases on diagonals and
curves get filled in. It is also entirely local and deterministic, so a rerun
reproduces the same bytes.

Applied log2(S) times to reach the pack's own scale, so the result drops into
the existing filename with no change to sizes, hashes, or the pack format.

Usage:
    python rescale_blocky.py <dump-dir> <pack-dir> [--apply]
Without --apply it only reports what it would redraw.
"""
import os
import sys

from PIL import Image


FLAT_RATIO = 0.98   # threshold, not equality -- see flat_block_ratio


def flat_block_ratio(src, rep):
    """Fraction of SxS blocks in `rep` that are one flat colour.

    A hand-redrawn image sits far below the threshold (the pack's genuine
    redraws measure 20-70%), so this separates cleanly. It is a ratio rather
    than "every block", because the two nearest-upscaled entries in this pack
    still measure 99.1% and 99.6% -- a handful of blocks differ, which is not
    detail worth keeping and would otherwise let them pass as redrawn.
    """
    sw, sh = src.size
    if rep.width % sw or rep.height % sh:
        return 0.0, 0
    S = rep.width // sw
    if S < 2 or rep.height // sh != S:
        return 0.0, 0
    rp = rep.load()
    flat = 0
    for by in range(sh):
        for bx in range(sw):
            first = rp[bx * S, by * S]
            if all(rp[bx * S + i, by * S + j] == first
                   for j in range(S) for i in range(S)):
                flat += 1
    return flat / float(sw * sh), S


def scale2x(im):
    """AdvMAME2x. Output pixels are copies of input pixels, never blends."""
    w, h = im.size
    src = im.load()
    out = Image.new("RGBA", (w * 2, h * 2))
    dst = out.load()

    def at(x, y):
        return src[min(max(x, 0), w - 1), min(max(y, 0), h - 1)]

    for y in range(h):
        for x in range(w):
            E = at(x, y)
            B, H = at(x, y - 1), at(x, y + 1)
            D, F = at(x - 1, y), at(x + 1, y)
            if B != H and D != F:
                e0 = D if D == B else E
                e1 = F if B == F else E
                e2 = D if D == H else E
                e3 = F if H == F else E
            else:
                e0 = e1 = e2 = e3 = E
            dst[x * 2,     y * 2]     = e0
            dst[x * 2 + 1, y * 2]     = e1
            dst[x * 2,     y * 2 + 1] = e2
            dst[x * 2 + 1, y * 2 + 1] = e3
    return out


def main():
    dump_dir, pack_dir = sys.argv[1], sys.argv[2]
    apply = "--apply" in sys.argv

    dumps = {}
    for f in os.listdir(dump_dir):
        if f.endswith(".png") and "-" in f:
            dumps.setdefault(f.split("-")[0], f)

    redrawn = 0
    for f in sorted(os.listdir(pack_dir)):
        if not f.endswith(".png") or "-" not in f:
            continue
        tex = f.split("-")[0]
        if tex not in dumps:
            continue
        src = Image.open(os.path.join(dump_dir, dumps[tex])).convert("RGBA")
        rep = Image.open(os.path.join(pack_dir, f)).convert("RGBA")
        ratio, S = flat_block_ratio(src, rep)
        if ratio < FLAT_RATIO:
            continue
        if S & (S - 1):
            print("%s: %dx upscale is not a power of two -- skipped" % (f, S))
            continue
        # Redraw from the ORIGINAL, not from the blown-up copy: scaling the
        # nearest version would just re-smooth its own staircases.
        out = src
        n = S
        while n > 1:
            out = scale2x(out)
            n //= 2
        assert out.size == rep.size, (out.size, rep.size)
        print("%s: %.1f%% flat blocks at %dx -> Scale2x redraw %dx%d"
              % (f, 100.0 * ratio, S, out.width, out.height))
        if apply:
            out.save(os.path.join(pack_dir, f))
        redrawn += 1

    print("%s %d image(s)" % ("redrew" if apply else "would redraw", redrawn))


if __name__ == "__main__":
    main()
