# Semantic audit: `0x8011E4B8` / `EmitCenteredTexturedSprite`

## Result and ABI

`0x8011E4B8` is a call-free GPU helper that emits one centered textured square.
Its exact behavior supports the descriptive name `EmitCenteredTexturedSprite`.

```c
void EmitCenteredTexturedSprite(
    int16_t center_x, int16_t center_y, int32_t depth, int32_t size,
    uint8_t red, uint8_t green, uint8_t blue, uint8_t fade);
```

The first two arguments are explicitly sign-normalized from 16 bits. The four
stack arguments are loaded as bytes. No caller consumes a semantic return.

## Boundary and incoming targets

The function owns `[0x8011E4B8, 0x8011E69C)`, exactly `0x1E4` bytes. It is a
single straight-line 121-instruction leaf with one entry and one `jr ra`; the
next independent owner starts at `0x8011E69C`.

An aligned scan of the loaded executable finds exactly six direct JAL sites:

```text
800F6FFC
8011F7AC  8011F7D0  8011FAEC  8011FB3C  8011FC80
```

These group under owners `0x800F6E04` and `0x8011F628`. There are no direct
JALs to any interior instruction. The recompiler may materialize reused
continuations more than once, so the executable's six unique instruction
addresses are the authoritative call count.

## Packet construction

The old `gPrimCursor` (`0x80198620`) is the destination and the function
publishes `old + 40` before filling it. The resulting packet is a standard
ten-word PlayStation `POLY_FT4`:

- DMA length byte `9` and primitive code byte `0x2E`;
- four corners `(x-half,y-half)`, `(x+half,y-half)`, `(x-half,y+half)`, and
  `(x+half,y+half)`, where `half = size >> 1`;
- eight UV bytes copied from offsets `+0x16,+0x18,+0x1A,+0x1C,+0x1E,+0x20,
  +0x22,+0x24` of the descriptor pointed to by `0x801D5410`;
- CLUT and TPAGE values derived from that descriptor's halfwords at `+6/+8`;
- a 24-bit link to the previous head of ordering-table bucket `depth >> 6`.

The ordering-table root is selected by signed halfword `0x80175CB0` from the
pointer table at `0x8018AE88`, matching the other recovered packet emitters.
The helper does not bounds-check the depth bucket or packet arena.

## Color path

The red, green, and blue bytes are packed into GTE `RGB`. The fade byte is
converted to `IR0 = (255 - fade) << 4`, and GTE `DPCS` supplies the packet's
final RGB word. Thus fade 255 keeps the supplied color while smaller values
move it toward the current far color. The helper does not modify the GTE far
color itself.

## Confidence

`confirmed`: the boundary, eight-argument ABI, centered geometry, exact packet
layout and size, descriptor fields, GTE color operation, ordering-table link,
cursor mutation, and every incoming direct JAL are instruction-proven.
