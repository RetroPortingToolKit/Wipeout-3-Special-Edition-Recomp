# Semantic audit: `0x80105B78`

## Result

Recommended symbol: `DrawFontString` (`known`, `emit = false`).

The function is the caller-facing byte-string renderer above the glyph packet
builder at `0x80105CDC`.  Its ABI is:

- `a0`: NUL-terminated byte string;
- `a1`: starting horizontal coordinate (used as a signed 16-bit baseline for
  the returned width);
- `a2`: vertical coordinate passed unchanged to the glyph builder;
- `a3`: font/style selector, conditionally remapped as described below;
- `v0`: final horizontal coordinate minus the sign-extended starting `a1`.

Thus the scalar result is the total horizontal advance, including spaces.  It
is not a character count or an end pointer.

## Exact owner and call graph

- Entry: `0x80105B78`, with a 40-byte frame saving `s0..s4` and `ra`.
- Exit: the common restore at `0x80105CBC..0x80105CDB`.
- Exact size: `0x164` bytes, matching
  `R 80105B78 164` in the generated range manifest.
- The next instruction, `0x80105CDC`, starts the independently callable glyph
  packet builder with its own frame and separate `R 80105CDC 260` range.
- Generated call evidence contains 69 distinct direct call PCs: 62 in the main
  `0x8010....0x8017` image and seven expansion/overlay call sites.  Main-image
  callers span menus, resource-string adapters, race presentation, and the
  text paths at `0x801009C0..0x8014B7C4`; the small adapter at `0x80113BEC`
  loads a string pointer from an indexed table and delegates directly here.
- The only direct callee is `0x80105CDC`, called once in the source loop for
  each non-NUL, non-space byte.

The manifest also emits `F` entries at `0x80105C24`, `0x80105C2C`,
`0x80105C34`, `0x80105C3C`, `0x80105C44`, `0x80105C4C`, `0x80105C54`,
`0x80105C5C`, `0x80105C64`, `0x80105C6C`, `0x80105C74`, and `0x80105C7C`.
All share `R 80105B78 164`; they are internal destinations from the ASCII jump
table at `0x8016D380`, retain the owner's frame and live `s` registers, and
have no independent ABI.

## Loop and return semantics

At `0x80105BDC` the routine reads one unsigned byte and advances the source
pointer in the branch delay slot.  The cases are:

1. NUL: return `current_x - sign_extend_16(start_x)`.
2. Space (`0x20`): add exactly eight to `current_x` and continue.
3. Any other byte: derive a glyph index, call
   `0x80105CDC(glyph, current_x, y, style)`, use that function's return value
   as the next `current_x`, and continue.

The helper reserves and OT-links one 40-byte textured-quad packet and returns
`current_x + 2 + 2 * glyph_width_field`.  This proves that the loop result is
pixel/coordinate advance rather than bytes consumed.

An empty string returns zero.  A string of only `N` spaces returns `8*N`.

## Byte-to-glyph mapping

The general ranges are:

- `A..Z` (`0x41..0x5A`) -> glyphs `0..25`;
- `0..9` (`0x30..0x39`) -> glyphs `26..35`;
- bytes `>= 0x60` -> `byte - 0x41` (therefore lowercase `a..z` maps to
  glyphs `32..57`, as encoded rather than case-folded).

The jump table overrides eleven punctuation bytes:

| Byte | Glyph |
|---|---:|
| `:` | 36 |
| `.` | 37 |
| `/` | 38 |
| `+` | 39 |
| `-` | 40 |
| `%` | 41 |
| `[` | 46 |
| `]` | 47 |
| `_` | 48 |
| `=` | 49 |
| `?` | 50 |

Other non-space bytes below `0x30` retain the loop's zero-initialized glyph
index.  Unlisted bytes `0x3A..0x40` use the general `byte - 0x16` mapping, and
unlisted bytes `0x41..0x5F` use `byte - 0x41`.  The punctuation table above
overrides those general results where applicable.

## Style remap

Before processing text, the function checks three conditions:

1. the word at `0x8018AE94` equals 2;
2. the word at `0x801757A8` is nonzero;
3. the caller's style is less than 6.

Only when all three hold does it add 14 to the style passed to the glyph
builder.  The original caller argument is otherwise preserved.  This is a
font/style-bank selection; the higher-level meaning of those two globals is
not yet proven.

## Confidence limits

The boundary, callers, byte mapping, spacing, style remap, packet-builder
delegation, and return arithmetic are instruction-level facts.  Font-family
names, language ownership of the remap globals, and intended behavior for
bytes outside the title's authored strings remain unresolved, so the symbol
uses a behavioral rather than source-original name.
