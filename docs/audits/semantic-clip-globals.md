# Semantic audit: scenery clip coefficients

## Result

The guessed normal-component names at `0x801E2480`, `0x801E2488`, and
`0x801E25CC` were too speculative.  Instruction-level dataflow through
`SceneryFrustumClip` (`0x80142634`) establishes that these are fixed-point
coefficients for its four camera side planes:

| Address | Name | Exact producer expression | Consumer role |
|---|---|---|---|
| `0x801E25CC` | `gSceneryClipAxisCoefficient` | `B - C` | coefficient of `x` in left/right tests and `y` in top/bottom tests |
| `0x801E2488` | `gSceneryClipHorizontalDepthCoefficient` | `scale269(D >> 1)` | coefficient of `z` in left/right tests |
| `0x801E2480` | `gSceneryClipVerticalDepthCoefficient` | `scale269((F * horizontal) >> 9)` | coefficient of `z` in top/bottom tests |

All three objects and the two requested load sites are now `known`.  The
names describe demonstrated arithmetic roles without asserting a particular
vector layout, handedness, or world-space plane convention.

## Producers

Both audited coefficient rebuilders publish the same values:

- `RebuildViewportClipCoefficients` (`0x8013535C`) stores the axis coefficient
  at `0x80135490`, the horizontal-depth coefficient at `0x801354C4`, and the
  vertical-depth coefficient at `0x801354F0`.
- `RebuildViewportClipCoefficientsFromScalars` (`0x80135B8C`) stores them at
  `0x80135C78`, `0x80135CAC`, and `0x80135CD8`, respectively.

Using the notation already established by the audits for those functions:

```text
span       = B - C
horizontal = ((E * D) << 8) / (A * F)
scale269(x) = (((((x << 4) + x) << 2) - x) << 2) + x) >> 8

axis coefficient             = span
horizontal depth coefficient = scale269(D >> 1)
vertical depth coefficient   = scale269((F * horizontal) >> 9)
```

The producer also writes the same `span` to `0x801E2484`.  That is a distinct
consumer role: `SceneryFrustumClip` loads it at `0x80142694`, optionally halves
it with the other coefficients, then multiplies it by four at `0x80142AC4` and
uses the result as a camera-space `z` clipping threshold.  This duplicate store
does not make `0x801E25CC` a generic coordinate or normal component.

## Loads and scale selection

At function entry, `SceneryFrustumClip` performs four adjacent loads:

```text
0x80142664: t6 = *(s32 *)0x801E25CC  // shared axis coefficient
0x8014266C: t9 = *(s32 *)0x801E2488  // horizontal z coefficient
0x80142690: s2 = *(s32 *)0x801E2480  // vertical z coefficient
0x80142694: t5 = *(s32 *)0x801E2484  // z-threshold source
```

If the mode flag loaded from `0x801E25D0` is not one, instructions
`0x801426AC-0x801426B8` arithmetic-shift all four values right by one.  Thus
the requested sites are simple full-word loads; neither site loads a packed
pair, and `0x80142690` does not load both a y and z component.

## Side-plane equations

The polygon clipping loops retain the three coefficient registers until use.
Representative left/right arithmetic at `0x80142DEC-0x80142E0C` is:

```text
distance = horizontalDepthCoefficient * z
         - axisCoefficient * x
```

The opposite horizontal plane uses the corresponding sign-reversed axis
term.  The same pair recurs in the second horizontal clipping pass at
`0x80143190-0x80143340`.

Representative top/bottom arithmetic at `0x80143528-0x80143548` is:

```text
distance = verticalDepthCoefficient * z
         - axisCoefficient * y
```

The opposite vertical plane reverses the axis term.  The pair recurs in the
second vertical pass at `0x801438D4-0x80143A80`.  These products are used as
signed edge distances when deciding whether to retain a vertex and when
interpolating a crossing vertex.  This proves the horizontal/vertical and
axis/depth roles independently of the producer formulas.

## Reference inventory

Within the retained executable, the two coefficient rebuilders are the only
direct stores to these three addresses, and `SceneryFrustumClip` contains the
only direct loads.  Evidence is in:

- `generated/SCES_028.45_full_122.c` for both producer bodies;
- `generated/SCES_028.45_full_126.c` for loads and all four clipping passes;
- `docs/audits/semantic-8013535c.md` and
  `docs/audits/semantic-80135b8c.md` for producer boundaries, ABIs, and exact
  fixed-point operation order.
