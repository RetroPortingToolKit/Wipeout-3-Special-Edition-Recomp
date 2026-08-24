# Semantic audit: `0x80105CDC`

## Result

`0x80105CDC` is the confirmed glyph renderer `EmitFontGlyphPacket`. It selects
font metrics and texture state from a style number, emits and ordering-table
links one neutral-gray 40-byte `POLY_FT4`, advances the global packet cursor,
and returns the next horizontal pen coordinate.

Its recovered ABI is:

```c
int EmitFontGlyphPacket(int glyph, int x, int y, int style);
```

The function does not preserve or return the packet address. Its scalar return
is exactly `x + 2*glyph_width + 2`.

## Exact boundary and caller census

The function opens a 16-byte leaf frame at `0x80105CDC`, has one return at
`0x80105F34` with stack restoration in the delay slot at `0x80105F38`, and is
followed by a distinct 48-byte-frame function at `0x80105F3C`. Therefore its
exact interval is `[0x80105CDC,0x80105F3C)`, or `0x260` bytes. The generated
range agrees.

An aligned opcode scan of the retained main executable finds exactly 47 direct
calls, and none to an interior block:

```text
801055E0 801055F4 80105608 80105624 80105638 80105654 80105668
8010567C 80105698 801056AC 801057F4 80105810 801058A8 801058D4
801058E8 80105900 80105A44 80105A60 80105B44 80105CA4 80106634
801066FC 80106794 8010685C 801068C8 8010698C 80106B04 80106BC8
80106C44 80106D08 80106D74 80106E38 8010714C 80107214 80107458
80107520 801076A4 8010776C 801086C8 80108790 8010881C 801088E4
8010A5D8 8010A92C 80110170 801105C4 801106A0
```

Callers pass constants for punctuation/digits or map character codes to glyph
indices before the call, and repeatedly feed the returned `x` into the next
glyph call. This independently proves both the glyph-index input and pen-return
semantics.

## Font metric bands

Each glyph metric is a six-byte entry selected as `band + glyph*6`. The style
chooses the band and a fixed glyph height:

| Style range | Metric band | Height |
|---:|---:|---:|
| `< 6` | `0x801CE518` | 18 |
| `6..11` | `0x801CE698` | 8 |
| `12..13` | `0x801CDCE0` | 9 |
| `14..19` | `0x801CDA88` | 16 |

Metric byte `+0` is atlas U, byte `+2` is atlas V, and signed halfword `+4`
is glyph width. Styles of 20 or greater reach the common code without
initializing the band pointer or height and are outside the valid ABI domain.

The texture-state pointer is loaded from the word table at `0x801A4B90`, using
index `*(int16_t *)0x8019BE68 + style - 1`. Its halfwords at `+6/+8` supply the
packed CLUT, while `+0x0E/+0x10` supply the packed TPAGE. Styles 3, 9, and 17
select semitransparency by changing command `0x2C` to `0x2E` and ORing `0x20`
into TPAGE.

## Packet and packed writes

The old value of `gPrimCursor` at `0x80198620` is the destination packet. The
cursor is advanced by 40 bytes before any packet field is written. This audit
also corrects the catalog's previous typo `0x801A8620`: the instruction is
`lui 0x801A` plus signed offset `-0x79E0`, whose exact effective address is
`0x80198620`, consistent with every renderer audit.

The packet layout is:

| Offset | Packed field |
|---:|---|
| `+0..+3` | ordering-table DMA tag; length byte 9 at `+3` |
| `+4..+7` | RGB `(128,128,128)` and command `0x2C`/`0x2E` |
| `+8,+10` | `(x-1, y)` |
| `+12,+13,+14` | `(u, v, CLUT)` |
| `+16,+18` | `(x+2*width, y)` |
| `+20,+21,+22` | `(u+width+1, v, TPAGE)` |
| `+24,+26` | `(x-1, y+height)` |
| `+28,+29` | `(u, v+height)` |
| `+32,+34` | `(x+2*width, y+height)` |
| `+36,+37` | `(u+width+1, v+height)` |

CLUT packing is `(descriptor[+8] << 6) | ((descriptor[+6] >> 4) & 0x3F)`.
TPAGE packing combines descriptor Y bits from `+0x0E`, X/page bits from
`+0x10`, and the semitransparency bit described above.

The final `lwl/swl` sequence inserts the packet at the head of ordering-table
bucket `0x8018AE88[*(int16_t *)0x80175CB0]`: the packet tag receives the prior
head and the bucket receives the packet's low 24-bit address. Therefore this
routine both constructs and links the primitive, unlike the nearby reusable
`EmitTexturedQuadPacket` builder at `0x80106350`.

## State and return semantics

Observable effects are limited to advancing `gPrimCursor`, writing the 40-byte
packet, and replacing the selected ordering-table head. The returned pen
coordinate derives from the right edge plus two:

```text
return x + 2*glyph_width + 2
```

The two-times width applies to screen geometry and pen advance, while atlas U
advances by the unscaled metric width plus its inclusive-edge adjustment.

## Evidence

- `disc/SCES_028.45`, exact MIPS disassembly and direct-JAL scan;
- `generated/SCES_028.45_full_106.c`, full control flow and packed writes;
- `generated/SCES_028.45_full.ranges`, exact range;
- `docs/audits/semantic-80106350.md`, independent 40-byte `POLY_FT4` layout;
- renderer audits independently locating `gPrimCursor` at `0x80198620`.
