# Semantic audit: `0x80105F3C`

## Result and ABI

`0x80105F3C` is the confirmed table builder
`InitializeFontGlyphMetricTables`:

```c
void InitializeFontGlyphMetricTables(uint8_t selector, uint8_t variant);
```

Both inputs are explicitly masked to eight bits. The routine returns no
meaningful scalar value, calls no other function, allocates nothing, and emits
no GPU packet. Its persistent effects are writes to the global font/glyph
metric bands later consumed by `EmitFontGlyphPacket` and related text owners.

## Exact boundary and target proof

The generated owner is exactly `[0x80105F3C,0x80106350)`, `0x414` bytes. It
opens a 48-byte frame at `0x80105F3C`; the common epilogue starts at
`0x80106324`, returns at `0x80106348`, and restores the frame at `0x8010634C`.
The independent textured-quad packet builder begins at `0x80106350`.

The range has 42 generated blocks because selector dispatch and nested metric
loops have many branch targets. A search across every generated shard finds no
JAL or jump to an interior row. Since the owner itself contains no JAL, none of
those rows is a call-return alias.

## Selector and source selection

The routine masks `a0/a1` into `selector/variant`. Selectors zero through nine
choose embedded signed-halfword source bands in `0x80173C74..0x80173F0C`, an
entry extent of either 64 or 100, and a selector-specific scale/height constant
from the set `{1,2,8,9,16,18}`. Any selector of ten or greater is normalized to
the selector-nine path, rather than indexing an unchecked jump table.

The `variant` byte participates in the selector dispatch and source choice; it
is not a pointer, count, or full-width arithmetic argument. Observed callers
use only small constants. The exact embedded source choice is preserved in the
audit evidence without assigning unsupported language or font-family names to
the numeric modes.

## Metric construction and global state

For each selected source halfword, the function performs signed scaling using
the compiler's multiply-high implementation of division by 100
(`0x51EB851F`, correction for negative values). It then runs bounded inner
metric loops, derives signed positions/widths, and writes halfword pairs into
the six global bands rooted at:

```text
801CDA88  801CDCE0  801CDF38
801CE218  801CE518  801CE698
```

The arrays use six-byte glyph entries. The writes fill the coordinate/extent
halfwords that the audited glyph renderer later reads as atlas position and
signed width. Fixed per-band biases visible in the stores include 158, 70,
204, 61, 5, and 102; selector scale and source values determine the remaining
coordinate. Arithmetic deliberately narrows intermediate results back to
signed 16-bit values, matching the halfword table representation.

This consumer relationship is direct: `EmitFontGlyphPacket` selects bands
`0x801CDA88`, `0x801CDCE0`, `0x801CE518`, or `0x801CE698`, indexes them at six
bytes per glyph, and interprets their fields as atlas U/V and glyph width.
Thus this is a metric/atlas-table initializer, not a renderer.

## Exhaustive caller and overlay census

There are exactly six calls, all consecutive setup calls in owner
`0x80104AC4`:

```text
80104C68 80104C74 80104C80
80104C8C 80104C98 80104CA4
```

They are six unique guest instructions. All target the entry, there is no
overlay duplicate, and no generated call targets an interior block. The caller
varies small selector/variant constants to initialize the required bands as a
batch before continuing frontend/text setup.

## Confidence

`confirmed`: boundary, byte ABI, selector clamp, source region, 64/100 extents,
signed divide-by-100 scaling, global destination bands, call-free behavior,
consumer relationship, and caller/interior census are instruction-backed.
Numeric selector meanings are intentionally not given source-level names.
