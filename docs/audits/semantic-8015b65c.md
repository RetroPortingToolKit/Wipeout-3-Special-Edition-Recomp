# Semantic audit: `0x8015B65C` / `EmitTexturedScreenQuadPacket`

## Result

`0x8015B65C` constructs and submits one screen-space GPU `POLY_FT4`, and is now
named `EmitTexturedScreenQuadPacket`. Its effective ABI is:

```c
void EmitTexturedScreenQuadPacket(
    int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    int16_t x2, int16_t y2, int16_t x3, int16_t y3,
    int depth, uint8_t r, uint8_t g, uint8_t b,
    bool semitransparent, const TextureDescriptor *texture);
```

The first four coordinates occupy `a0..a3`; the remaining ten arguments are
caller-stack arguments at `+0x10..+0x34`.

## Exact owner, callers, and duplication

The generated range `[0x8015B65C,0x8015B7E4)` is exact, length `0x188`. It
uses a 24-byte frame, saves `ra` and `s0`, and returns at `0x8015B7DC` with
stack restoration in the delay slot. `0x8015B7E4` begins a separate owner.

Exactly three direct calls target the entry:

```text
80152AA0 80152B38 80152DD8
```

All belong to one UI/font-style renderer and supply axis-aligned quads, depth,
color, a semitransparency flag, and a texture descriptor. No external jump or
JAL targets an interior instruction; `0x8015B784 -> 0x8015B798` is the sole
direct internal join. An aligned full-body byte search finds only
`0x8015B65C`, so overlapping/generated overlay ranges do not represent a
second logical implementation or additional caller set.

## Packet construction

The routine reserves exactly 40 bytes from the cursor at `0x80198620` and
advances that cursor by `0x28`. It writes:

- packet length 9 and GPU opcode `0x2C` for opaque `POLY_FT4`, or opcode
  `0x2E` when semitransparency is requested;
- the four signed XY pairs at packet offsets `+8/+0A`, `+10/+12`,
  `+18/+1A`, and `+20/+22`;
- caller RGB bytes at `+4..+6`;
- four UV pairs copied from texture bytes `+0x16/+0x18`, `+0x1A/+0x1C`,
  `+0x1E/+0x20`, and `+0x22/+0x24`;
- texture halfword `+4` as the CLUT field at packet `+0x0E`.

For an opaque packet, texture halfword `+2` is copied directly to packet
TPAGE at `+0x16`. For a semitransparent packet, the routine instead calls
canonical `GetTPage(texture->tp, 1, texture->x, texture->y)` using signed
halfwords at texture `+0`, `+0x0E`, and `+0x10`, and stores that ABR-1 TPAGE.
The texture pointer is assumed valid; there is no validation.

## Ordering-table submission and return

The signed depth is arithmetically shifted right six and multiplied by four.
The active ordering-table base is selected through the signed index at
`0x80175CB0` and pointer table rooted at `0x8018AE88`. The final unaligned
three-byte link operations preserve the packet's length byte, copy the old OT
head into its tag, and replace the OT head with the new packet address—the
standard prepend operation.

The caller ignores `v0`, and the epilogue leaves an incidental link value;
there is no semantic return. Observable effects are the 40-byte packet, cursor
advance, OT head update, and the `GetTPage` call on the semitransparent path.
No GPU MMIO is touched directly.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` PS-X EXE header, for exact MIPS,
  ABI stack offsets, callers/interior targets, and duplicate-body search;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_50.c` for the exact owner and full flow;
- the independent `GetTPage` audit for canonical TPAGE packing semantics.
