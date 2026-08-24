# Semantic audit: `0x801103FC`

## Result and logical owner

`0x801103FC` is the null-terminated string renderer now named
`DrawMappedTextString`. The true owner is `[0x801103FC, 0x80110560)`, exactly
`0x164` bytes. It creates a 56-byte frame, saves `ra` and `s0..s5`, and has a
single epilogue at `0x8011053C..0x8011055C`. `0x80110560` begins the independent
`DrawSignedDecimalInteger` owner.

The generated ranges repeat `R 801103FC 164` 13 times: once for the real entry
and once after each of twelve `F` rows at `0x801104A0, A8, B0, B8, C0, C8,
D0, D8, E0, E8, F0, F8`. These are not duplicated functions. They are switch
translations for the owner's punctuation jump table; each is an internal
two-instruction case arm that assigns a glyph and jumps to `0x80110518`.
Generated alias wrappers likewise re-enter the same host owner at those cases.
An exact-body scan finds only the physical owner at `0x801103FC`.

## ABI and return

The recovered ABI is:

```c
int DrawMappedTextString(
    const uint8_t *text, int x, int y, int style,
    uint8_t grayscale);
```

The fifth argument is loaded from incoming stack `+0x10`. The initial X is
truncated/sign-extended to 16 bits for the final measurement baseline. The
function returns `current_x - initial_x`, i.e. rendered horizontal width, not
the absolute final pen.

## Mapping and rendering behavior

The loop consumes one unsigned byte at a time until zero. A space emits no
packet and advances X by exactly 8. Other bytes select a glyph as follows:

- `A..Z` map to `0..25`;
- `0..9` map to `26..35`;
- `:` `.` `/` `+` `-` `%` map to `36..41`;
- `[` `]` `_` `=` `?` map to `46..50`;
- unsupported bytes below `A` that are not one of those cases retain the
  initialized fallback glyph `0`; bytes above `_` follow the unchecked
  arithmetic mapping `byte - 'A'`.

The 59-word jump table at `0x8016D7B0` covers bytes `0x25..0x5F`; most rows
route to the generic letter/digit/fallback logic and the eleven punctuation
rows route to the small internal case arms.

Each non-space byte is passed to the packet helper at `0x80105354` as
`(glyph, current_x, y, selected_style, grayscale)`, and its returned next X is
threaded into the following iteration. That helper allocates a 40-byte
textured-quad packet, uses the fifth argument for all three RGB bytes, and
returns the advanced X pen. This owner performs no direct GPU or ordering-table
write beyond that callee.

Before rendering, style is conditionally remapped: if word `0x8018AE94` equals
2 and the caller style is below 6, add 14. This matches the low/high font-bank
selection used by the adjacent numeric text owners.

## Caller and target census

An exhaustive aligned scan of the retained EXE finds **zero direct J/JAL
callers** to `0x801103FC`. No literal function-pointer word for the entry is
present either. The routine is retained but has no statically recoverable
executable caller in this image; this audit does not invent an indirect caller.

No control transfer from outside the owner targets an interior address. All
direct interior transfers are its own loop, punctuation cases, and epilogue
edges. The twelve generated alias/case translations therefore do not increase
the caller count and are catalogued as internal blocks, not callable owners.

## Sources checked

- `disc/SCES_028.45`, after its 0x800-byte PS-X EXE header, for exact MIPS,
  direct/literal references, interior targets, jump-table contents, and body
  duplication;
- `generated/SCES_028.45_full_27.c`, `.ranges`, and dispatch metadata for the
  repeated case translations and logical-owner continuity;
- adjacent quad-permutation and numeric text audits for layout context; the
  actual name and behavior above are limited to this owner's proved effects.
