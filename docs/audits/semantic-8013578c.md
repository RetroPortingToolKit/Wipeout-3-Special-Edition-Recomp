# Semantic audit: `0x8013578C`

## Result

`0x8013578C` is the confirmed transform rotation builder
`BuildTransformRotationMatrix`. It sign-extends three input angles, evaluates
the recovered Q12 sine/cosine functions, writes a row-major 3x3 Q12 rotation
matrix to the destination's first nine halfwords, and writes one to destination
halfword `+0x40`.

Its recovered ABI is:

```c
void BuildTransformRotationMatrix(
    void *destination,
    int16_t angle_y_negated,
    int16_t angle_x,
    int16_t angle_z_negated);
```

The parameter names describe the matrix convention, not a conversion applied
to the scalar arguments before lookup: all three values go to sine and cosine
with their original signs.

## Boundary and callers

The function opens a 56-byte frame at `0x8013578C`, saves `ra` and `s0..s7`,
has one epilogue, and returns at `0x80135988` with stack restoration at
`0x8013598C`. The next routine opens an independent 48-byte frame at
`0x80135990`. Its exact interval is therefore
`[0x8013578C,0x80135990)`, or `0x204` bytes, matching the generated range.

An aligned JAL scan finds exactly 27 direct callers:

```text
800EBEA4 800EBED8 800F0338 800FA8B0 800FAECC 800FAFB8 800FBF04
800FC3B4 800FC4A0 8012F408 8012F4A0 8012F720 8012F900 8012FA0C
8012FB10 8012FE8C 8012FF64 80130070 8013015C 80138CFC 8013C000
80153494 80153AFC 80159A3C 8015B200 8015B4AC 8015B950
```

The dominant call shape passes a transform pointer loaded from owner offset
`+0x20` and signed angle halfwords from record offsets `+0x11E/+0x120/+0x122`.
Other callers use equivalent triplets at `+0xA6/+0xA8/+0xAA`,
`+0x328/+0x32A/+0x32C`, or `+0x4C/+0x4E/+0x50`. Several single-axis callers
pass zero for the first two angles and a signed third angle, corroborating both
the argument order and rotation role. No caller uses a scalar return.

## Angle and fixed-point conventions

Each input is explicitly truncated/sign-extended to 16 bits. The function
calls `SinQ12FromSignedAngle` once for each angle, followed by
`CosQ12FromSignedAngle` once for each in the same order. Their independent
audit proves:

- `0x1000` angle units are one turn;
- sine and cosine outputs are signed Q12 (`4096 == 1.0`);
- angles wrap modulo `0x1000` inside the lookup routines.

Let:

```text
A = a1, B = a2, C = a3
sx = sin(A), cx = cos(A)
sy = sin(B), cy = cos(B)
sz = sin(C), cz = cos(C)
Q(x*y) = arithmetic_shift_right(x*y, 12)
```

All quantities are signed integers. Products shown nested below are staged:
each `Q` corresponds to a real intermediate arithmetic shift in the routine,
so the formulas preserve the game's fixed-point rounding rather than merely
describing the ideal real-valued matrix.

## Matrix field formulas

The nine signed halfword stores are row-major:

| Offset | Field | Exact fixed-point formula |
|---:|---:|---|
| `+0x00` | `m00` | `Q(cx*cz) + Q(Q(sx*sz)*sy)` |
| `+0x02` | `m01` | `Q(cx*sz) - Q(Q(sx*sy)*cz)` |
| `+0x04` | `m02` | `Q((-sx)*cy)` |
| `+0x06` | `m10` | `Q((-sz)*cy)` |
| `+0x08` | `m11` | `Q(cy*cz)` |
| `+0x0A` | `m12` | `-sy` |
| `+0x0C` | `m20` | `Q(sx*cz) - Q(Q(cx*sy)*sz)` |
| `+0x0E` | `m21` | `Q(sx*sz) + Q(Q(cx*sy)*cz)` |
| `+0x10` | `m22` | `Q(cy*cx)` |

Ignoring staged Q12 rounding, this is exactly the standard column-vector
composition:

```text
M = Ry(-A) * Rx(B) * Rz(-C)
```

The convention is also visible from isolated axes:

- only `A` nonzero produces `Ry(-A)`;
- only `B` nonzero produces `Rx(B)`;
- only `C` nonzero produces `Rz(-C)`.

## State semantics

The destination's first 18 bytes receive only the nine rotation halfwords.
The routine does not clear or overwrite the neighboring padding, translation,
or other transform fields. It separately stores halfword `1` at destination
offset `+0x40`; this is demonstrably a containing-transform status flag, while
the distinction between “active” and “dirty” is not yet proven. There are no
global writes and no meaningful return value; all externally relevant state is
in those ten destination halfwords.

Final matrix stores truncate intermediate signed results to 16 bits, matching
the PSX `MATRIX` rotation-field representation. The function computes values
with ordinary integer multiply/shift operations rather than using the GTE.

## Evidence

- `disc/SCES_028.45`, exact MIPS disassembly and direct-JAL census;
- `generated/SCES_028.45_full_121.c`, complete translated arithmetic and
  stores;
- `generated/SCES_028.45_full.ranges`, exact range;
- `docs/audits/semantic-8015c318.md`, independently recovered sine, cosine,
  angle-unit, and Q12 conventions.
