# Semantic audit: `0x8013535C`

## Result

`0x8013535C` is a real callable function and is now named
`RebuildViewportClipCoefficients` with `status = "known"`.

It rebuilds a seven-word viewport/frustum parameter set from the current
display-mode index and six indexed scalar tables.  It is not merely a four-word
frustum setter, as the previous guessed symbol note claimed.

## Boundary and callers

- Entry: `0x8013535C`
- Return: `jr $ra` at `0x801354EC`; its delay slot at `0x801354F0` performs the
  final output store.
- Size: `0x198` bytes (408 bytes).
- The next range/function begins at `0x801354F4`.
- The retained executable has one direct `jal`, at `0x80133D1C`, inside
  `0x80133B64`.  The return address is `0x80133D24`.
- No internal address is a separate callable entry.  The extra dispatch rows at
  `0x801353C4` through `0x8013547C` are control-flow continuation points.

These facts agree between `generated/SCES_028.45_full.ranges`, the generated
function body, and the generated CPS call edge.

## ABI

The routine is an argument-free leaf:

- It overwrites `$a0` at its first instruction and never consumes the incoming
  argument registers.
- It allocates no stack frame, makes no calls, and touches no callee-saved GPR.
- It clobbers `$at`, `$v0-$v1`, `$a0-$a3`, `$t0-$t4`, HI, and LO.
- `$v0` happens to retain the last coefficient on return, but the sole direct
  caller does not consume it.  The semantic return type is `void`.

## Inputs

Let signed 32-bit table element `T(addr)` mean the word at
`addr + 4 * mode`, where `mode = *(s32 *)0x80173B00`.

| Local | Source |
|---|---|
| `A` | `T(0x80173B38)` |
| `B` | `T(0x80173BA0)` |
| `C` | `T(0x80173B6C)` |
| `D` | `T(0x80173B04)` |
| `E` | `T(0x80173C08)` |
| `F` | `T(0x80173BD4)` |

All arithmetic is signed MIPS integer arithmetic.  The compiler-emitted
`break` instructions are the standard signed divide overflow/zero guards, not
semantic branches in the successful calculation.

## Math and outputs

Define the fixed-point scale helper exactly as the instruction sequence does:

```text
scale269(x) = ((((x << 4) + x) << 2) - x) << 2) + x
scale269(x) = scale269(x) >> 8
```

This is signed `269*x/256` with the original operation order and arithmetic
right shift.  Then the function computes:

```text
span       = B - C
vertical   = (A * span) / D
horizontal = ((E * D) << 8) / (A * F)

0x801E14F0 = vertical
0x801E14EC = horizontal
0x801E14F4 = C
0x801E25CC = span
0x801E2484 = span
0x801E2488 = scale269(D >> 1)
0x801E2480 = scale269((F * horizontal) >> 9)
```

The `0x801E2480`, `0x801E2484`, `0x801E2488`, and `0x801E25CC` values are later
loaded together by the scene/object visibility path beginning at `0x80142634`.
The `0x801E14EC`, `0x801E14F0`, and `0x801E14F4` values are separately consumed
by viewport/projection setup code around `0x80135FF8-0x80136130`.  That combined
consumer evidence is why `RebuildViewportClipCoefficients` is more precise than
the old `SetupFrustumClipBounds` guess.

## Evidence locations

- `generated/SCES_028.45_full.ranges`: `F 8013535C`,
  `R 8013535C 198`, followed by `F 801354F4`.
- `generated/SCES_028.45_full_122.c`: direct call at guest `0x80133D1C` and the
  complete body at guest `0x8013535C-0x801354F0`.
- `generated/SCES_028.45_full_126.c`: visibility consumer loads at guest
  `0x80142664`, `0x8014266C`, `0x80142690`, and `0x80142694`.
