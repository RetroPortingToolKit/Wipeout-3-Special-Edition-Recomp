# Semantic audit: guest `0x801704F0`

## Conclusion

`0x801704F0` is an internal, register-convention helper used by the adaptive
quad subdivision/rendering code.  It replaces four input scratch vertices with
the four edge midpoints and appends the quad center, then projects those five
new vertices with GTE `RTPS` and writes screen position, depth, and a four-bit
screen clip code into each 16-byte vertex record.

Recommended symbol: `BuildProjectQuadMidpoints` (**high confidence**).

It is not merely an alternate clip-code packer.  Clip-code construction is the
last stage of a much larger interpolation-and-projection operation.

## Calling convention and signature

This is not a normal four-argument o32 C function.  Its callers prepare
temporary registers directly:

```c
// Conceptual signature; registers shown because this is a private asm helper.
void BuildProjectQuadMidpoints(
    // t6, t7, t8, t9: pointers to four 16-byte input vertex records
    ScratchVertex *corner0, ScratchVertex *corner1,
    ScratchVertex *corner2, ScratchVertex *corner3,
    // gp: destination, normally the same scratch array as the inputs
    ScratchVertex midpoint_and_center[5],
    // fp+52 / fp+56: signed right and bottom screen bounds
    int32_t right_bound, int32_t bottom_bound);
```

There is no scalar return value.  The output is five records at `gp +
{0,16,32,48,64}`.  The helper clobbers normal caller-saved registers and also
uses `t6-t9`, `gp`, and the GTE as explicit inputs/work state.  `fp` remains the
enclosing renderer's frame pointer; bounds are loaded from `[fp+52]` and
`[fp+56]` (and reloaded after the interpolation phase).

Observed caller setup includes:

- `0x8016FB50`: inputs at scratch offsets `0,32,16,48`, destination at
  `0x1F800010`; call after the four original clip flags are ANDed.
- `0x80170850`, `0x8017089C`, `0x80170938`, `0x801709E4`, and `0x80170A7C`:
  the recursive/adaptive quad paths select different four-record arrangements
  and call the same helper.

## Record layout

The writes prove this 16-byte scratch layout:

```c
typedef struct ScratchVertex {
    int16_t x;          // +0, camera/object-space coordinate used by RTPS
    int16_t y;          // +2
    int16_t z;          // +4
    uint16_t unknown6;  // +6, untouched here
    int16_t sx;         // +8, low half of GTE SXY2
    int16_t sy;         // +10, high half of GTE SXY2
    int16_t sz;         // +12, GTE SZ3 (low half retained)
    uint16_t clip;      // +14, screen clip bitmask
} ScratchVertex;
```

The packed SXY word is stored with `swc2 $14,+8`; SZ3 comes from GTE data
register 19.  Field `+6` is neither read nor written by this helper.

## Exact interpolation

For each signed 16-bit coordinate independently, arithmetic addition followed
by arithmetic shift right computes:

```text
out[0] = (corner0 + corner1) >> 1
out[1] = (corner0 + corner2) >> 1
out[2] = (corner1 + corner3) >> 1
out[3] = (corner2 + corner3) >> 1
out[4] = (out[1] + out[2]) >> 1
```

Thus outputs 0-3 are the four edge midpoints and output 4 is the center.  The
store ordering deliberately permits `gp` to alias the input array.  Negative
odd sums round toward negative infinity because the original uses `sra`, not C
signed division.

## Projection and clip code

The helper pipelines five GTE `RTPS` commands.  For each generated vertex it
stores SXY2, SZ3, then packs:

```c
clip = (right_bound  < sx ? 1 : 0)
     | (sx < 0             ? 2 : 0)
     | (bottom_bound < sy ? 4 : 0)
     | (sy < 0             ? 8 : 0);
```

Therefore bits `0,1,2,3` mean right, left, bottom, and top respectively.  The
current generated C wraps only selected horizontal comparisons in the
widescreen participation hook; the underlying guest instructions remain
visible in the instruction comments (`slt`/`slti`).

The first four projected records execute RTPS in a software pipeline: the next
RTPS is issued while the previous SXY/SZ result is consumed.  The fifth uses
the already pending final result without issuing a sixth command.

## Confidence

- **High:** five-output interpolation formulas, 16-byte stride, xyz/SXY/SZ/clip
  offsets, clip bit meanings, GTE RTPS use, no scalar return.
- **High:** purpose as quad edge-midpoint/center generation and projection;
  formulas and the six subdivision call sites independently support it.
- **Medium:** the broader renderer's exact source-level type and whether xyz is
  best named camera-space or transformed object-space.  GTE projection proves
  it is a signed `SVECTOR`-compatible triplet, but this audit does not assign a
  canonical game type name.

