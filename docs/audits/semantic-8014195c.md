# Semantic audit: guest `0x8014195C`

## Result

`0x8014195C..0x80141D6B` is the game's full indexed-TIM texture loader. The
original source-level role is authenticated by its own retained diagnostic,
`tim.c:LoadTexture(): Cannot auto place 8 bit textures`, at `0x8016E820`.
The callable symbol is therefore `LoadTexture`.

It accepts an in-memory TIM image and a signed page/blend selector, allocates a
38-byte texture descriptor, derives PSX CLUT/TPAGE values and four UV corners,
uploads the CLUT and pixel blocks, and returns the descriptor. It implements
the indexed TIM modes 0 (4-bit) and 1 (8-bit); every other pixel mode returns
null without allocating or uploading.

## Exact boundary and callers

The generated range is exactly `R 8014195C 410`: 1,040 bytes ending at
`0x80141D6B`. The entry makes a 48-byte frame and saves `$s0..$s3` and `$ra`;
the common epilogue restores them and returns at `0x80141D64`, with the stack
restore in the delay slot at `0x80141D68`. The next independently seeded
function starts at `0x80141D6C`, so neither routine borrows the other's code.

There is exactly one retained direct call to this entry, at `0x80141E68` in
`CreateTextureDescriptorFromImageData`. That 36-byte public adapter preserves
its `$a0` image pointer, sign-extends `$a1` to 16 bits, calls `LoadTexture`, and
returns the result unchanged. Its four direct callers are therefore the four
retained external users of this loader; there are no additional direct calls
or dispatch entries targeting `0x8014195C`.

The recovered prototype is:

```c
TextureDescriptor *LoadTexture(const TimImage *tim, int16_t page_mode);
```

The type names are descriptive, but the argument and return dataflow is exact.

## TIM parsing and accepted modes

At entry the loader retains `$a0` as the TIM base, canonicalizes `$a1` with
`sll 16; sra 16`, and reads `tim[+0x04] & 7`, the standard TIM pixel-mode
field.

- Mode 0 selects the 4-bit indexed layout. The first block's `RECT` is at
  `+0x0C` and its 16-colour data starts at `+0x14`; the second block's `RECT`
  is at `+0x38` and its pixel data starts at `+0x40`. The 44-byte separation
  is precisely a TIM header/rectangle plus 32 bytes of CLUT data.
- Mode 1 selects the 8-bit indexed layout. Its first block likewise uses
  `+0x0C/+0x14`, while the second uses `+0x218/+0x220`; the fixed separation
  accounts for the 512-byte 256-colour CLUT.
- Any mode other than 0 or 1 branches directly to the common return with
  `$s0 = 0`.

Both accepted paths call the arena allocator at `0x8011686C` with the arena
stored at `0x8018F098` and size 38. Neither checks allocation failure.

## Placement behavior

For a 4-bit TIM whose first rectangle word is zero, the loader auto-places its
CLUT using the halfword cursors at `0x801CD944/0x801CD948`. It supplies a
16-by-1 rectangle, advances the X cursor by 16, wraps it to 960 when necessary,
and increments Y on wrap.

If the 4-bit image rectangle word at `+0x38` is zero, it similarly assigns the
image from cursors at `0x801A4B78/0x801A4B8C`, advancing the X cursor by eight
and resetting the secondary cursor when the retained bounds tests require a
new placement row.

The 8-bit path deliberately does not auto-place a missing pixel rectangle. If
the word at `+0x218` is zero it reports the literal at `0x8016E820` through
`0x800F82EC`, passing severity/value 2, and then continues constructing and
uploading from the supplied TIM fields. This control flow is what ties the
retained source diagnostic directly to this function.

## Descriptor layout

The allocation consists of 19 halfwords. Both indexed paths populate the same
layout:

| Offset | Meaning |
| --- | --- |
| `+0x00` | TIM pixel mode, 0 or 1 |
| `+0x02` | PSX TPAGE value, including `page_mode << 5` and the 8-bit `0x80` bit where applicable |
| `+0x04` | PSX CLUT value, `(clut_y << 6) | (clut_x >> 4)` |
| `+0x06..+0x0C` | CLUT rectangle `x, y, w, h` |
| `+0x0E..+0x14` | image rectangle `x, y, w, h` in VRAM words |
| `+0x16..+0x24` | four `(u,v)` corner pairs: top-left, top-right, bottom-left, bottom-right |

For mode 0, `u0 = (image_x & 63) * 4` and
`u1 = (image_width + (image_x & 63)) * 4 - 1`. For mode 1 the multiplier is
2. Both paths use `v0 = image_y & 255` and
`v1 = image_height + (image_y & 255) - 1`. Those bounds are duplicated into
the four corner pairs in the conventional order. The TPAGE construction uses
the image rectangle's page coordinates; mode 1 ORs in `0x80`.

## GPU uploads and return

Each accepted path calls the SDK-style `LoadImage` implementation at
`0x800EEC80` twice:

1. `LoadImage(&tim->clut_rect, tim->clut_data)` from `+0x0C/+0x14`;
2. `LoadImage(&tim->image_rect, tim->image_data)` from `+0x38/+0x40` in mode
   0 or `+0x218/+0x220` in mode 1.

The allocated descriptor remains in `$s0` across both calls and is returned
unchanged in `$v0`. Unsupported modes return zero.

## Symbol decision

Add `[[func]] pc = 0x8014195C` as `LoadTexture`, `status = "known"`, and
`emit = false`. The function boundary, direct caller, TIM offsets, accepted
modes, allocation size, descriptor stores, upload calls, return behavior, and
embedded original diagnostic are all instruction- or byte-level facts.
`known` retains the catalog's convention for semantically recovered callable
symbols whose exact original prototype/type spellings are unavailable.

## Evidence inspected

- `generated/SCES_028.45_full_124.c`: complete body and adapter call.
- `generated/SCES_028.45_full.ranges`: exact `0x410` range.
- `generated/SCES_028.45_dispatch.c`: entry and continuation ownership.
- `generated/SCES_028.45_full_101.c`: `0x800EEC80` upload implementation.
- `disc/SCES_028.45`: diagnostic bytes at guest `0x8016E820`.
- `docs/audits/semantic-80141e5c.md`: adapter boundary and four-caller census.
