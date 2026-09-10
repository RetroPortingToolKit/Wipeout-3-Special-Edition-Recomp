# Semantic audit: `0x8012DA10`

## Result and exact owner

`0x8012DA10` is the frontend image-table builder now named
`PrepareFrontendImageWarpTables`. The generated range is exactly
`[0x8012DA10, 0x8012DCB4)`, `0x2A4` bytes. It creates a 56-byte frame, saves
`ra` and `s0..s7`, and returns through the sole epilogue at
`0x8012DC88..0x8012DCB0`. The next independent layout owner begins at
`0x8012DCB4`.

An exhaustive aligned transfer scan finds one entry caller and no external
interior target. The complete owner bytes occur only once, so there is no
overlay duplicate or generated split.

## ABI and mode handling

The recovered ABI is:

```c
void PrepareFrontendImageWarpTables(
    uint8_t *rgb24_pixels,
    void *ignored_texture_descriptor,
    int mode,
    uint32_t ignored_a3);
```

Incoming `a1` and `a3` are never read. The function establishes no semantic
return value.

It first snapshots signed layout halfwords `0x801D749C`, `0x801D749E`, and
`0x801D74A0` into words `0x801DDE48`, `0x801DDE4C`, and `0x801DDE44`.
For each of the source's first three bytes, it clears the low three bits and,
when the resulting value is at least 9, sets bit 2. This produces an
eight-step-aligned value with a four-unit midpoint bias for non-dark channels.

Modes `1`, `13`, and `14` publish those adjusted bytes to
`0x801DEE74/0x801DEE7C/0x801DEE78` in source-channel order and then become mode
zero. In mode zero, including those three remapped modes, the adjusted bytes
are written back over the source's first RGB triplet. Other nonzero modes
leave that triplet unchanged. The remaining source pixels are never mutated.

## 64-by-8 color-command table

The first double loop consumes exactly `8 * 64 * 3 = 1536` source bytes, i.e.
512 RGB24 pixels. Each pixel becomes:

```c
0x38000000 | r | (g << 8) | (b << 16)
```

In little-endian packet memory this is `R,G,B,0x38`, a GPU color/command word.
Words are written at `0x801DE670 + 32*row + 4*column`, transposing the linear
eight groups of 64 source pixels into a 64-row by eight-column table. The
routine writes all 512 words and no packet links.

## Trigonometric warp rows

The second outer loop builds 26 records of 80 bytes at `0x801DDE50`. For row
`i = 0..25`, it evaluates the confirmed Q12 helpers at angle `(i+4)*64`:

```text
x_component = -CosQ12FromSignedAngle(angle)
z_component =  SinQ12FromSignedAngle(angle)
```

It writes eight eight-byte entries. Entry zero receives twice the X component;
entries one through seven receive the ordinary X component. Every entry gets
the common sine component at `+4`; its `+2` value advances in eight steps from
`layout_A - 7*layout_C - 5000` by `layout_C`, where `layout_A` is the snapshot
from `0x801D749C` and `layout_C` is from `0x801D74A0`.

The 16-byte tail at record `+0x40` copies the last generated entry's first
three halfwords, repeats its X and sine halfwords at `+0x48/+0x4C`, and writes
the delta
`*(int16_t *)0x801D749C - *(int16_t *)0x801D749E` at `+0x4A`. Fields not listed
are not initialized by this owner. This table is subsequently consumed by the
adjacent frontend layout builder, supporting the conservative “warp tables”
identity without assigning a specific screen effect.

## Caller and callees

The sole direct caller is `0x80110960` in `PrepareFrontendImageResource`. It
passes reacquired archive image data in `a0`, a texture descriptor in ignored
`a1`, and option byte `options[5]` in `a2`.

The only callees are `CosQ12FromSignedAngle` (`0x8015C3E8`) and
`SinQ12FromSignedAngle` (`0x8015C318`), one static call site each inside the
26-row loop. No call or jump enters an internal block.

## Sources checked

- `disc/SCES_028.45`, after its 0x800-byte PS-X EXE header, for exact MIPS,
  caller/interior scanning, and body duplication;
- `generated/SCES_028.45_full_37.c` and `.ranges` for owner continuity;
- audits for `PrepareFrontendImageResource` and the two Q12 trigonometric
  helpers for producer/callee context.
