# Semantic audit: `0x801102B8` / `MirrorQuadVerticalCoordinates`

## Result and ABI

`0x801102B8` is the vertical companion to
`MirrorQuadHorizontalCoordinates`:

```c
void MirrorQuadVerticalCoordinates(
    int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1,
    int16_t *x2, int16_t *y2, int16_t *x3, int16_t *y3);
```

The first four pointers arrive in `a0..a3`; the remaining four are incoming
stack arguments `+0x10..+0x1C` before the callee creates its eight-byte frame.
It establishes no scalar return value.

## Boundary and exact permutation

The generated range table proves `[0x801102B8, 0x80110324)`, exactly `0x6C`
bytes. It is a single-block leaf, preserves `s0/s1` in an eight-byte frame,
and returns at `0x8011031C` with frame restoration in the delay slot at
`0x80110320`. The adjacent owner begins at `0x80110324`.

All eight signed halfwords are loaded before the first store. In pointer order
the snapshot permutation is:

```text
(x0,y0,x1,y1,x2,y2,x3,y3)
 -> (x0,y2,x1,y3,x2,y0,x3,y1)
```

Thus all X coordinates remain fixed while the two Y rows exchange places:
`y0 <-> y2` and `y1 <-> y3`. Although loads sign-extend, halfword stores
preserve every 16-bit bit pattern. There are no global, GPU, validation, or
other effects.

## Callers and target proof

Exactly four calls target the entry:

```text
0x80108A54  0x80108B38  0x80108F3C  0x80109020
```

Every caller supplies the same eight coordinate-component pointers used by
the neighboring orientation permutations. This body differs from
`MirrorQuadHorizontalCoordinates` at `0x8011024C`, which exchanges X values
within each row; `0x801102B8` instead exchanges the Y rows and therefore earns
the vertical name. No call or jump targets an interior instruction, and the
body has no overlay duplicate.
