import sys
from PIL import Image
path, band_h, nbands = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
im = Image.open(path).convert("RGBA"); w, h = im.size; px = im.load()
def ink(x, y): return px[x, y][3] > 8
for b in range(nbands):
    y0, y1 = b * band_h, min(h, (b + 1) * band_h)
    runs = []
    x = 0
    while x < w:
        if any(ink(x, y) for y in range(y0, y1)):
            x0 = x
            while x < w and any(ink(x, y) for y in range(y0, y1)): x += 1
            runs.append((x0, x - 1))
        else:
            x += 1
    print("band %d  y=[%d,%d)  ink runs=%d" % (b, y0, y1, len(runs)))
    print("   " + " ".join("%d-%d(%d)" % (a, bb, bb - a + 1) for a, bb in runs))
