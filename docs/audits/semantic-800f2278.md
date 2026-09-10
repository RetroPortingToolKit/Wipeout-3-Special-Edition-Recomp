# Semantic audit: `0x800F2278` / `ComputeThreePointTrigonometricOffset`

## Result

`0x800F2278` is a fixed-point three-point construction now named
`ComputeThreePointTrigonometricOffset`. It measures AB, BC, and AC, feeds a
retained signed Q12 combination of those lengths through arccos and
quarter-angle cosine, and writes a point extending from B away from C.

## Boundary and ABI

The exact generated owner is `[0x800F2278,0x800F2534)`, length `0x2BC`. It
creates an 80-byte frame, saves `ra` and `s0..s2`, and returns at `0x800F252C`
with frame restoration in the delay slot. `0x800F2534` begins a separate
owner. Generated interior entries are division guards and call continuations,
not callable aliases.

The effective ABI is a flattened coordinate interface:

```c
void ComputeThreePointTrigonometricOffset(
    int32_t out[3],
    int32_t ax, int32_t ay, int32_t az,
    int32_t unused_stack_word,
    int32_t bx, int32_t by, int32_t bz,
    int32_t cx, int32_t cy, int32_t cz);
```

`out` is `a0`; A is `a1..a3`; the incoming stack word at `+0x10` is ignored;
B is at `+0x14..+0x1C`, and C at `+0x20..+0x28`. Callers populate the unused
word as part of a wider geometry calling convention.

## Exact geometry

The routine forms `AB=A-B`, `BC=C-B`, and `AC=A-C`, then calls
`ApproximateVectorLength3i` for each. If `|AB|` or `|BC|` is zero, it returns
without writing `out`.

Otherwise it evaluates this exact retained expression with signed integer
division (not the ordinary positive cosine-law numerator):

```text
ratio_q12 = (-(|BC|^2) - (|AB|^2 + |AC|^2)) << 12
            / (2 * |AB| * |BC|)
angle     = ApproximateArccosQ12(ratio_q12)
distance  = (CosQ12FromSignedAngle(angle >> 2) * |AB|) >> 12

out.x = B.x - (C.x - B.x) * distance / |BC|
out.y = B.y - (C.y - B.y) * distance / |BC|
out.z = B.z - (C.z - B.z) * distance / |BC|
```

Angles use the game's convention in which pi is `0x4000`; shifting by two is
an exact quarter-angle operation. Squares, sums, and the left shift have MIPS
32-bit wrap behavior. The unusual negated-sum ratio is instruction-proven; the
name deliberately does not relabel it as a conventional triangle angle.

## Callers, targets, and overlay accounting

Exactly four executable JALs target the true entry:

```text
800E3B90 800E43E8 800E8F58 800F1734
```

All supply an output vector and three object/geometry-derived points; callers
consume the written vector after return. No external direct jump or call
targets any interior instruction. An aligned exact-body scan finds only
`0x800F2278`, so overlay overlap adds neither a duplicate owner nor duplicated
logical call sites.

The name states the proven construction rather than assigning an unproven
track, collision, or craft-specific purpose.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` header, for exact arithmetic,
  caller ABI, target census, and duplicate-body scan;
- `generated/SCES_028.45_full.ranges` for the exact owner;
- audits of `ApproximateVectorLength3i`, `ApproximateArccosQ12`, and the Q12
  cosine leaf for independently established math conventions.
