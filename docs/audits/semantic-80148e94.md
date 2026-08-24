# Semantic audit: `0x80148E94` / `EmitWorldSpaceRadialGlow`

## Result

`0x80148E94` is a world-space radial-glow packet emitter now named
`EmitWorldSpaceRadialGlow`:

```c
void EmitWorldSpaceRadialGlow(int32_t x, int32_t y, int32_t z,
                              int32_t unused,
                              int32_t size,
                              uint8_t r, uint8_t g, uint8_t b,
                              uint8_t intensity);
```

It transforms and projects an eight-point glow shape, culls invalid or
behind-camera results, emits four semitransparent Gouraud triangles, and links
them into the active ordering table with a colored-center-to-black-outer fade.

## True logical owner

Generated metadata splits the routine into these apparent owners:

```text
80148E94 +07C  80148F10 +00C  80148F1C +0F0
8014900C +030  8014903C +0B4  801490F0 +2E8
801493D8 +010
```

They are one fall-through function. The `0x80148E94` entry opens a `0xE0`-byte
frame and saves `s0/s1`. Each generated piece explicitly falls through to the
next while consuming live registers, GTE state, and stack work arrays created
by the entry. The only restore/return is `0x801493D8..0x801493E4`, whose delay
slot releases that same `0xE0`-byte frame. The next independent owner begins at
`0x801493E8`.

The exact logical callable is therefore `[0x80148E94,0x801493E8)`, `0x554`
bytes. None of the six continuation addresses is independently callable.

## ABI and geometry

The entry saves the first three register arguments as world X/Y/Z. It saves
`a3` at original stack slot `+0x0C` but never reloads or otherwise consumes it;
the fourth argument is observably unused. Original stack slots `+0x10..+0x20`
supply signed size and four promoted values whose low bytes are R, G, B, and
intensity.

The routine obtains the current view object through `0x80194B94`, adds its
three translation words at object `+0x14/+0x18/+0x1C` to caller XYZ, and uses
the GTE rotation transform to obtain a view-space center. From caller size it
derives three signed radii with the exact instruction sequence:

```text
wide = (3 * size) >> 1
half_wide = wide >> 1
narrow = signed-magic-division result derived from wide and 0x2AAAAAAB
```

Those radii construct eight signed XYZ points around the center in stack
`SVECTOR`-style slots. A loop runs GTE `RTPS` exactly eight times, preserving
eight screen XY results, eight depths, and eight flag words.

The function returns without emission if representative GTE flags 0 or 4 have
any bits in mask `0x80460000`, or if representative projected depths 0 or 4
are non-positive. There is no scalar return contract; all four callers ignore
register residue.

## Packet and ordering-table effects

On an accepted projection, the routine reserves four consecutive `0x1C`-byte
packets from `gPrimCursor` at `0x80198620`, advancing it by exactly `0x70`.
Every packet is initialized as:

- tag length 6;
- primitive code `0x32`, a semitransparent `POLY_G3`;
- vertex 0 and vertex 1 RGB equal to
  `(r*intensity >> 8, g*intensity >> 8, b*intensity >> 8)`;
- vertex 2 RGB exactly black;
- three projected XY pairs selected from the eight-point work array.

The two representative depths are each biased by `-64`, clamped at zero, and
shifted right by six to select ordering-table buckets. Using the active table
chosen by signed halfword `0x80175CB0` from the pointer table rooted at
`0x8018AE88`, the routine prepends triangles 0/1 as one linked pair at the
first depth and triangles 2/3 as another pair at the second depth.

Four triangles, symmetric projected geometry, equal color at the inner two
vertices, black outer vertices, and semitransparency establish a radial glow
rather than an opaque model primitive or textured sprite.

## Exhaustive callers and overlay duplication

An aligned scan of the retained executable finds four unique entry JALs:

```text
8011FCD8  80146974  80146AC8  80146B04
```

No external J/JAL targets a continuation or another interior instruction.
Generated output emits five calls because `0x8011FCD8` appears in two
overlapping translations; the other three sites are emitted once. Thus there
are four semantic callers, not five.

The first caller passes particle-record XYZ, size, and four bytes at record
`+0x2A..+0x2D`. The remaining sites pass computed world coordinates, animation
size, RGB, and fade/intensity values from racer/effect rendering paths. All
ignore return registers and continue consuming the shared packet arena.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, GTE flow, callers, and target scans;
- `generated/SCES_028.45_full.ranges` and generated fall-through calls for the
  seven-piece logical-owner proof;
- generated call emissions for the five-emitted/four-unique overlay proof;
- existing renderer audits for `gPrimCursor`, active ordering-table selection,
  and PSX `POLY_G3` packet layout.
