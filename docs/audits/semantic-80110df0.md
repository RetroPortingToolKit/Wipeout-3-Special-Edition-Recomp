# Semantic audit: `0x80110DF0` / `LoadFrontendTexture`

## Result

`0x80110DF0` is cataloged as `LoadFrontendTexture` to distinguish it from the
same-named race-overlay implementation at `0x8014195C`; its retained diagnostic
names it `LoadTexture`. It accepts an in-memory PlayStation TIM image, builds a compact
runtime texture descriptor, uploads the CLUT and pixels, and returns the new
descriptor. Only 4-bit and 8-bit indexed TIM modes are supported.

```c
TextureDescriptor *LoadFrontendTexture(void *tim, int16_t abr, int16_t variant);
```

## Exact boundary and callers

The function owns `[0x80110DF0, 0x801116A0)`, exactly `0x8B0` bytes. It opens a
56-byte frame at the entry; every branch converges on the restore/return at
`0x80111678..0x8011169C`. The next independently generated owner begins at
`0x801116A0`.

An exhaustive aligned scan of the loaded EXE finds exactly two direct calls:

```text
80111DF8: LoadFrontendTexture(bundle_entry, 1, 0)
80111FBC: LoadFrontendTexture(bundle_entry, 1, 1)
```

Both callers append nonnull results to the global texture-descriptor table.
The first is the path used by `RegisterTextureBundleEntries`; the second is its
paired variant registrar. No JAL targets an internal block.

## Accepted TIM layout

The word at input `+0x04` is the standard TIM flags word. Its low three bits
select pixel mode and its sign bit selects an optional source-pixel transform.

Mode 0 follows the 4-bit indexed TIM layout:

```text
+0x0C CLUT RECT (x,y,w,h)   +0x14 16-color CLUT data
+0x38 image RECT            +0x40 packed 4-bit image data
```

Mode 1 follows the 8-bit layout, whose 256-color CLUT makes the image block
start later:

```text
+0x0C CLUT RECT (x,y,w,h)   +0x14 256-color CLUT data
+0x218 image RECT           +0x220 packed 8-bit image data
```

Modes other than 0 and 1 return null without allocation or upload. For mode 1,
an all-zero image origin is rejected through the retained diagnostic
`tim.c:LoadTexture(): Cannot auto place 8 bit textures`; 8-bit image
auto-placement is therefore explicitly unsupported.

## Descriptor creation

For either supported mode the routine allocates `0x26` bytes from the heap
whose pointer is stored at `0x8018F098`. Allocation failure is not checked.
The descriptor contains:

- `+0x00`: TIM pixel mode (0 or 1; a special early-return case writes 2);
- `+0x02`: TPAGE value, combining VRAM page coordinates, pixel mode, and
  `(abr << 5)`;
- `+0x04`: CLUT identifier derived from CLUT VRAM coordinates;
- `+0x06..+0x0C`: copied CLUT rectangle;
- `+0x0E..+0x14`: copied image rectangle;
- `+0x16..+0x24`: derived U/V start and inclusive edge coordinates used by
  sprite/font packet consumers.

Mode-specific shifts convert TIM image widths in 16-bit VRAM words to texel
width: four texels per word for mode 0 and two per word for mode 1. UV origins
are reduced relative to the 64-word TPAGE boundary and the 256-line page.

If the 4-bit image X field at `+0x38` is the sentinel 1 or 2, the routine emits
a small special descriptor (mode 2, resource code `0x120` or `0x124`) and
returns without image upload.

## Placement, upload, and variants

Zero CLUT coordinates request automatic placement from the rolling CLUT
allocator at `0x801CD944/0x801CD948`, in 16-pixel units with row wrap. Zero
4-bit image coordinates request placement through the library's rolling VRAM
page state and placement-region tables. Explicit nonzero coordinates are
preserved, with configured region offsets applied when they fall within the
managed rectangles.

The graphics transfer routine at `0x800EEC80` receives a stack RECT plus the
source pointer. The loader invokes it for the CLUT and image blocks. With
`variant == 1`, it sets bit 15 in all 16 entries of the 4-bit CLUT and uploads
that CLUT again into a secondary rolling slot before the image transfer.

When the TIM flags sign bit is set and global byte `0x801732B0` is nonzero, the
source image is transformed in place before its final upload. Mode 0 reverses
pixels across each row by swapping bytes and reversing the nibble order inside
each byte; mode 1 reverses bytes across each row. This mutation affects the
caller-owned TIM buffer.

## State and failure behavior

Successful ordinary loads advance the applicable CLUT/VRAM placement cursors
and return the allocated descriptor. The function has no rollback if a later
transfer fails, and the transfer API has no checked result here. Unsupported
TIM modes return null. The two callers explicitly skip null descriptors.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instructions, strings, data layout, and exhaustive JAL scanning;
- `generated/SCES_028.45_full.ranges` for the exact 0x8B0 owner range;
- `docs/audits/semantic-80111da0.md` and both direct caller sequences for
  registration behavior and fixed arguments;
- direct disassembly of the graphics transfer routine at `0x800EEC80`.
