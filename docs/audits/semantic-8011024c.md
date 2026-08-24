# Semantic audit: `0x8011024C` / `MirrorQuadHorizontalCoordinates`

## Result and ABI

`0x8011024C` mirrors a quad's four coordinate pairs horizontally by swapping
the two X values in each row:

```c
void MirrorQuadHorizontalCoordinates(
    int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1,
    int16_t *x2, int16_t *y2, int16_t *x3, int16_t *y3);
```

The first four pointers use `a0..a3`; the remaining four are incoming stack
arguments `+0x10..+0x1C`. The function establishes no scalar return value.

## Exact owner and permutation

The generated interval `[0x8011024C,0x801102B8)` is exact, length `0x6C`. It
uses an eight-byte leaf frame to preserve `s0/s1`, calls no helper, and returns
at `0x801102B0` with frame restoration in the delay slot. The next independent
permutation begins at `0x801102B8` with its own frame.

All eight signed halfwords are loaded before any store, so the exact snapshot
permutation is:

```text
(x0,y0,x1,y1,x2,y2,x3,y3)
 -> (x1,y0,x0,y1,x3,y2,x2,y3)
```

Although loads sign-extend through `lh`, stores truncate with `sh`, so every
16-bit coordinate pattern is preserved exactly. The Y pointers are read and
rewritten unchanged. There is no null/alignment validation, global access,
GPU access, or other side effect.

## Callers, targets, and neighboring variants

Exactly five executable calls target the entry:

```text
8010B27C 8010B2E0 8010B338 8010BE64 8010BEC4
```

Each caller passes eight pointers holding the four coordinate pairs and
selects this routine from an orientation/mode range. The adjacent equal-sized
owners at `0x801102B8`, `0x80110324`, and `0x80110390` perform different
permutations of the same eight-pointer set; callers combine them for other
orientation cases. This establishes a quad-coordinate mirror rather than a
generic eight-value swap.

No external direct jump or JAL targets an interior instruction. An exact-body
scan finds only `0x8011024C`, so there is no overlay duplicate or duplicated
logical caller.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` header, for exact loads/stores,
  caller arguments, target census, and duplicate-body scan;
- `generated/SCES_028.45_full.ranges` and generated owner for the exact extent;
- adjacent permutation bodies and their shared orientation-dispatch callers.
