# Semantic audit: `0x801116A0`

## Result

`0x801116A0` is the compact TIM converter now named `LoadSimpleTimTexture`:

```c
TextureDescriptor *LoadSimpleTimTexture(void *tim, int16_t abr);
```

It accepts 4-bit and 8-bit indexed PlayStation TIM images, allocates the
game's shared `0x26`-byte texture descriptor, derives TPAGE/CLUT/UV fields,
uploads the CLUT and image rectangles, and returns the descriptor. Other TIM
pixel modes return null.

## Exact boundary, return, and callers

The function opens a 48-byte frame at `0x801116A0`, saves `ra/s0..s3`, and
converges on one epilogue at `0x80111ADC..0x80111AF4`. The independent packed
bundle loader begins at `0x80111AF8`. The exact owner is therefore
`[0x801116A0,0x80111AF8)`, `0x458` bytes, matching generated metadata.

The return is the allocated descriptor in `v0` on supported modes and null on
unsupported modes. Allocation failure is not checked before descriptor stores,
so a null allocator result is outside the contract.

There is exactly one direct caller:

```text
80111ED8: LoadSimpleTimTexture(bundle->entries[i], 1)
```

No external direct `j` or `jal` targets an interior instruction. The direct
jumps from `0x80111920` to `0x80111AC8` and from `0x80111AD0` to `0x80111ADC`
originate within this owner and rejoin its upload/return paths.

## Input format

The word at TIM `+0x04` is interpreted as standard TIM flags; the low three
bits select pixel mode. Only these layouts are accepted:

```text
mode 0, 4-bit indexed:
  +0x0C CLUT RECT      +0x14 16-color CLUT
  +0x38 image RECT     +0x40 packed pixels

mode 1, 8-bit indexed:
  +0x0C CLUT RECT      +0x14 256-color CLUT
  +0x218 image RECT    +0x220 packed pixels
```

Modes 2..7 return null before allocation or upload. The second argument is
sign-extended from 16 bits and inserted as `(abr << 5)` into the TPAGE value;
the sole owner always supplies ABR 1.

## Placement behavior

For either mode, a zero packed CLUT origin at TIM `+0x0C` requests placement
from the rolling allocator words at `0x801CD944/0x801CD948`. It assigns a
16-pixel-wide CLUT slot and wraps from X `0x400` to `0x3C0` on the next row.

For 4-bit mode, a zero image origin at `+0x38` requests placement from the
rolling VRAM page state at `0x801A4B78/0x801A4B8C`; rows/pages wrap at the
managed 256/512-line boundaries. Explicit coordinates are retained. 8-bit
mode requires a nonzero image origin: zero invokes the retained diagnostic
`tim.c:LoadTexture(): Cannot auto place 8 bit textures` through the severity-2
handler, then continues under the authored-data invariant.

This compact converter has none of `LoadFrontendTexture`'s special X=1/2
descriptor sentinels or managed-region coordinate-offset tables.

## Descriptor layout and arithmetic

The descriptor allocation comes from the heap pointer at `0x8018F098` and is
exactly `0x26` bytes. The populated halfwords are:

- `+0x00`: TIM mode (0 or 1);
- `+0x02`: TPAGE from image page coordinates, mode, and ABR;
- `+0x04`: CLUT identifier from CLUT X/Y;
- `+0x06..+0x0C`: copied CLUT rectangle X/Y/W/H;
- `+0x0E..+0x14`: copied image rectangle X/Y/W/H;
- `+0x16..+0x24`: U/V origin and inclusive edge coordinates.

Mode 0 converts TIM width in 16-bit VRAM words to four texels per word; mode 1
uses two texels per word. U is relative to the 64-word TPAGE boundary and V to
the 256-line page. Inclusive maximum coordinates subtract one after adding
width/height.

## CLUT mutation and uploads

In 4-bit mode the loader modifies caller-owned TIM data before upload: all 16
halfwords at `tim+0x14` are ORed with `0x8000`, forcing the PlayStation STP bit
on every palette entry. It then calls the graphics transfer routine at
`0x800EEC80` for:

1. CLUT RECT `tim+0x0C`, data `tim+0x14`;
2. image RECT `tim+0x38`, data `tim+0x40`.

In 8-bit mode it does not rewrite the 256-color CLUT and uploads:

1. CLUT RECT `tim+0x0C`, data `tim+0x14`;
2. image RECT `tim+0x218`, data `tim+0x220`.

Transfer results are not checked and there is no rollback. This routine does
not inspect the TIM flags sign bit and performs no source-pixel mirroring. It
also has no third variant argument or secondary CLUT upload, distinguishing it
from `LoadFrontendTexture` at `0x80110DF0`.

## State effects

Successful auto-placement advances the appropriate CLUT/VRAM cursors. The TIM
palette is mutated only in 4-bit mode. The allocated descriptor and graphics
uploads survive return. Unsupported modes have no state effects and return
null.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instructions and exhaustive caller/interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0x458 owner range;
- `docs/audits/semantic-80110df0.md` for comparison with the richer converter
  and `semantic-80111e84.md` for the sole caller and registration lifecycle;
- direct disassembly of `AllocateHeapBlock`, graphics transfer `0x800EEC80`,
  and the retained 8-bit auto-placement diagnostic.
