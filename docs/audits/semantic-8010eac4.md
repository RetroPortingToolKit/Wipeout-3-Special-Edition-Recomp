# Semantic audit: `0x8010EAC4`

## Result

`0x8010EAC4` is `EmitLocalizedFontGlyphPacket`, the packet-level glyph sink
used by the localized text family.  It selects glyph metrics and a font
texture from the requested height/mode and bank, builds one textured quad,
links it into the active ordering table, advances the global packet cursor,
and returns the next X pen.

## Exact boundary and entry integrity

The entry allocates a `0x38`-byte frame and saves `ra`, `s0`--`s7`, and `fp`.
All paths join at `0x8010F218`; the sole return is `jr ra` at `0x8010F240`
with stack restoration at `0x8010F244`.  The generated owner is therefore
exactly `[0x8010EAC4,0x8010F248)`, `0x784` bytes and 102 blocks.  No external
J or JAL targets any interior address.  The next callable localized-string
owner does not begin until `0x8010F3CC`; the intervening bytes are not part of
this routine's executed control flow.

## ABI and return

The recovered seven-argument ABI is:

```c
int EmitLocalizedFontGlyphPacket(
    int glyph, int x, int y, int height_mode,
    int unused, int intensity, int bank);
```

The fifth stack argument (`old sp+0x10`) is never read.  The sixth is loaded
from old `sp+0x14` and supplies the grayscale intensity in the modes that use
caller color.  The seventh, old `sp+0x18`, selects language/font-bank variants
including special values `0x200`, `0x201`, `0x302`, `0x900`, and `0x901`.

Supported `height_mode` values are 8, 9, 16, and 18.  Other values take the
common epilogue without allocating or linking a packet and do not establish a
useful return contract.  Successful paths return:

```text
x + scaled_glyph_width + gap
```

The glyph width is read as a signed halfword from the selected six-byte metric
entry.  Several large/special paths double it.  The normal gap is two pixels;
the special paths which set the internal flag to one use one pixel.  This is
the next-X value threaded by `DrawLocalizedFontString`.

## Metric, texture, and mode selection

The routine indexes each metric table as `base + glyph*6`.  An entry supplies
U at `+0`, V at `+2`, and signed width at `+4`.  The four height modes and the
bank selector choose among separate metric-table roots and texture descriptors.
The special `0x900/0x901` banks select extended tables and a negative four-pixel
left bearing; modes 8/9 and 16/18 select the corresponding small/large layout
families.  This table-driven bank selection matches the independently audited
extended-byte mapping in `DrawLocalizedFontString`.

The selected texture descriptor contributes CLUT and TPAGE source halfwords.
Those values are packed into the primitive's CLUT/TPAGE fields together with
the selected semitransparency/blend policy.  The only callee, `0x8015FD4C`, is
a 40-byte primitive helper which sets or clears bit 1 of packet byte `+7`;
there are eight static calls covering the mode/bank cases.

## Packet and global state effects

The authoritative packet cursor is the word at `0x801CD94C`.  It selects one
40-byte record in the packet pool rooted at `0x8019BEAC`.  On a successful
path the routine:

1. copies metric U/V coordinates into all four textured vertices and applies
   width/height to the far edges;
2. writes mode-selected RGB bytes (zero, `0x1F`, `0x7F`, or caller intensity)
   and the packed CLUT/TPAGE values;
3. writes screen coordinates derived from X, Y, bearing, scaled width, and
   `height_mode` into the four vertices;
4. links the 40-byte packet at the ordering-table slot selected by signed
   halfword `0x80175CB0` through the table rooted at `0x8018AE88`;
5. increments `0x801CD94C` by one.

The primitive layout and 40-byte stride identify a `POLY_FT4`.  Rejected modes
perform none of these writes.  The function does not write GPU registers; it
queues the primitive for the existing render path.

## Exhaustive direct callers

An aligned scan of the loaded EXE finds exactly 15 JAL sites:

```text
8010F8B0 8010FA10 8010FA30 8010FA50 8010FA70 8010FA90
8010FC38 8010FC74 8010FD20 8010FD58 8010FDBC 8010FDE8
8010FE7C 8010FF68 80110048
```

Grouped by owner, these are one call from confirmed
`DrawLocalizedFontString` (`0x8010F3CC`), seven from `0x8010F988`, four from
`0x8010FC84`, two from `0x8010FE24`, and one from `0x8010FFA0`.  The latter
owners are specialized localized numeric/text adapters.  No call or jump from
outside the owner targets an internal block.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  entry/interior-target scans, and call counts;
- `generated/SCES_028.45_full.ranges` and the generated 102-block owner;
- `docs/audits/semantic-8010f3cc.md` for the independently established caller
  ABI, byte-to-glyph mapping, extended banks, and returned-pen use.
