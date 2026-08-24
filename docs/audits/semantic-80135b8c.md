# Semantic audit: `0x80135B8C`

## Result

`0x80135B8C` is a real callable function and is now named
`RebuildViewportClipCoefficientsFromScalars` with `status = "known"`.

The old `SetupFrustumClipBoundsAlt` guess was misleading.  This is not an
argument-free alternate-table routine: it accepts six signed 16-bit scalars
and rebuilds exactly the same seven global viewport/perspective coefficients
as `RebuildViewportClipCoefficients` at `0x8013535C`.

## Boundary and callers

- Entry: `0x80135B8C`.
- Return: `jr $ra` at `0x80135CD4`; its delay slot at `0x80135CD8` performs the
  final output store.
- Size: `0x150` bytes (336 bytes).
- The next range/function begins at `0x80135CDC`.
- The retained executable has one direct `jal`, at `0x80124048` inside
  `RenderRaceFrame` (`0x8012268C`).  Its return address is `0x80124050`.
- The dispatch entries at `0x80135BC0-0x80135C64` are divide-guard and normal
  control-flow continuations, not additional callable entries.

These facts agree between `generated/SCES_028.45_full.ranges`, the generated
function body, and the sole generated CPS call edge.

## ABI

The logical signature is:

```c
void RebuildViewportClipCoefficientsFromScalars(
    int16_t C, int16_t B, int16_t D, int16_t F, int16_t A, int16_t E);
```

- `C`, `B`, `D`, and `F` arrive in `$a0-$a3`.
- `A` and `E` are loaded as signed halfwords from caller stack offsets `+0x10`
  and `+0x14`.
- The routine is a leaf, allocates no stack frame, and touches no callee-saved
  GPR.
- It clobbers `$at`, `$v0-$v1`, `$a0-$t3`, HI, and LO.  The return value is
  semantically `void`; the caller ignores the incidental final `$v0` value.
- All six values are normalized through signed-halfword semantics before use,
  including the four register arguments.

At the sole call site, `C` and `B` come from signed halfwords at current-view
offsets `+0x370` and `+0x372`.  The remaining arguments are indexed words whose
low halfwords are passed as `D = T(0x80173B04)`, `F = T(0x80173BD4)`,
`A = T(0x80173B38)`, and `E = T(0x80173C08)`, with the index loaded from
`0x80173B00`.

## Math and outputs

Define the fixed-point helper with the exact MIPS operation order:

```text
scale269(x) = (((((x << 4) + x) << 2) - x) << 2) + x) >> 8
```

Then the successful calculation is:

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

The `break` instructions at `0x80135BC0`, `0x80135BD8`, `0x80135BFC`,
`0x80135C2C`, `0x80135C44`, and `0x80135C60` are compiler-generated signed
divide zero/overflow traps.  They are not alternate semantic outcomes.

## Relation to `0x8013535C`

The math and all seven destination addresses are identical.  The difference is
input selection:

- `0x8013535C` loads all six 32-bit values itself from mode-indexed tables.
- `0x80135B8C` consumes six caller-selected values after signed-16 narrowing.
- Its one caller substitutes the current view record's `C` and `B` halfwords,
  while taking the other four values from the same table families used by
  `0x8013535C`.

This evidence supports `RebuildViewportClipCoefficientsFromScalars`, while a
stronger “frustum bounds” name would incorrectly describe only part of the
seven-word state it publishes.

## Evidence locations

- `generated/SCES_028.45_full.ranges`: `F 80135B8C`,
  `R 80135B8C 150`, followed by `F 80135CDC`.
- `generated/SCES_028.45_full_117.c`: argument setup at guest
  `0x80123FFC-0x8012404C` and the direct call at `0x80124048`.
- `generated/SCES_028.45_full_122.c`: complete body at guest
  `0x80135B8C-0x80135CD8`.
- `docs/audits/semantic-8013535c.md`: independently audited table-loading
  counterpart and downstream consumers of the seven outputs.
