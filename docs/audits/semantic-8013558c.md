# Semantic audit: `0x8013558C`

## Result

`0x8013558C` is the confirmed transform Euler builder
`BuildTransformRxRyRzMatrix`. It takes a transform pointer and three signed
angles, calculates a row-major 3x3 Q12 rotation matrix using the recovered
sine/cosine helpers, and marks the containing transform state at `+0x40`.

Its recovered ABI is:

```c
void BuildTransformRxRyRzMatrix(
    void *destination, int16_t angle_x, int16_t angle_y, int16_t angle_z);
```

The name describes the proven algebraic composition. It does not claim an
original source identifier or a world/view convention that the instructions
alone cannot establish.

## Boundary and ownership

The entry at `0x8013558C` opens a fresh 48-byte frame, saves `ra` and
`s0..s6`, and has a single epilogue ending with `jr ra` at `0x80135784` and
the stack restore in its delay slot at `0x80135788`. The exact owner is
therefore `[0x8013558C,0x8013578C)`, or `0x200` bytes, exactly matching the
generated range.

The adjacency does not merge it with either neighbor:

- `SetTransformTranslation` ends at `0x8013558B` after its own `jr ra` and
  delay-slot state write;
- `BuildTransformRotationMatrix` begins at `0x8013578C` with a distinct
  56-byte frame and implements a different Euler composition.

Generated entries at `0x801355D4`, `0x801355E4`, `0x801355F4`,
`0x80135604`, `0x80135614`, and `0x80135624` are post-call continuation
blocks retaining the owner's live saved registers and frame. An exhaustive
aligned JAL scan finds no call to any address inside the owner.

## Caller census and role

The same scan finds exactly 13 direct callers:

```text
800E911C 800E98A8 800E9B8C 800E9EF0 800EA0A8 800EA390 800EA54C
80124868 801248A8 80133AA0 80133AC8 80133E9C 80133EC4
```

Six main-image initialization calls pair this function with
`SetTransformTranslation`, passing zero for all three angles to produce an
identity rotation without modifying the translation just installed. The
seven expansion/overlay callers provide live orientation values. Six of those
derive `a1` from `Atan2Q12`, load a signed halfword into `a2`, pass zero in
`a3`, and target a transform reached through an owner link at `+0x44`. The
remaining caller supplies two negated signed angles. These uses independently
confirm that the three scalars are angles and that the destination is the same
transform layout used by the adjacent translation setter.

No caller consumes a scalar result; the routine establishes none.

## Angle and arithmetic conventions

Each of `a1`, `a2`, and `a3` is explicitly truncated and sign-extended from
16 bits. The routine calls `SinQ12FromSignedAngle` for the three angles in
argument order, then `CosQ12FromSignedAngle` in the same order. Their separate
audit proves that one turn is `0x1000` angle units and each result is signed
Q12 (`0x1000 == 1.0`).

Let:

```text
A = (int16_t)a1, B = (int16_t)a2, C = (int16_t)a3
sA = sin(A), cA = cos(A)
sB = sin(B), cB = cos(B)
sC = sin(C), cC = cos(C)
Q(x*y) = arithmetic_shift_right(x*y, 12)
```

Nested `Q` operations below are real staged shifts in the executable, not
algebraic simplifications. They preserve the title's integer rounding.

## Destination layout and exact formulas

The first 18 destination bytes are nine signed halfwords in row-major order:

| Offset | Field | Exact fixed-point formula |
|---:|---:|---|
| `+0x00` | `m00` | `Q(cB*cC)` |
| `+0x02` | `m01` | `Q((-cB)*sC)` |
| `+0x04` | `m02` | `-sB` |
| `+0x06` | `m10` | `Q(cA*sC) - Q(Q(sA*sB)*cC)` |
| `+0x08` | `m11` | `Q(cA*cC) + Q(Q(sA*sB)*sC)` |
| `+0x0A` | `m12` | `Q((-sA)*cB)` |
| `+0x0C` | `m20` | `Q(sA*sC) + Q(Q(cA*sB)*cC)` |
| `+0x0E` | `m21` | `Q(sA*cC) - Q(Q(cA*sB)*sC)` |
| `+0x10` | `m22` | `Q(cA*cB)` |

Ignoring staged fixed-point rounding, the matrix is exactly:

```text
M = Rx(A) * Ry(-B) * Rz(C)
```

The isolated-axis cases make the signs and order directly testable:

- only `a1` nonzero yields `Rx(a1)`;
- only `a2` nonzero yields `Ry(-a2)`;
- only `a3` nonzero yields `Rz(a3)`.

This differs from the adjacent `0x8013578C` builder, whose independently
audited composition is `Ry(-a1) * Rx(a2) * Rz(-a3)`. The two are real
callable alternatives rather than duplicate generated splits.

## Mutation limits

After the calculations, the routine writes halfword `1` to destination
offset `+0x40`. This is the same containing-transform state field written by
`SetTransformTranslation`; its broader enum meaning is not proven. The
routine does not write the 32-bit translation fields at `+0x14/+0x18/+0x1C`,
the padding between the matrix and translation, any link field, or globals.
Final matrix stores truncate to signed 16-bit representation.

## Evidence

- `disc/SCES_028.45`: authoritative instructions and exhaustive direct-JAL
  and interior-target scans;
- `generated/SCES_028.45_full.ranges`: exact `0x200` owner range;
- `generated/SCES_028.45_full_39.c`: complete call sequence, arithmetic,
  stores, and epilogue;
- `docs/audits/semantic-80135574.md`: adjacent translation layout;
- `docs/audits/semantic-8013578c.md`: independently distinct neighboring
  Euler builder;
- `docs/audits/semantic-8015c318.md`: Q12 trigonometric conventions.
