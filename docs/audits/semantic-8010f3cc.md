# Semantic audit: `0x8010F3CC`

## Result

`0x8010F3CC` is the confirmed localized byte-string renderer
`DrawLocalizedFontString`. It does not produce a transliterated output buffer.
It reads a NUL-terminated byte string, converts each non-space byte to a glyph
index, emits that glyph through `0x8010EAC4`, and carries the returned X pen
into the next iteration.

Its recovered ABI is:

```c
int DrawLocalizedFontString(
    const unsigned char *text, int x, int y, int mode, int language_or_bank);
```

The fifth argument is loaded from `sp+0x10`. Its numeric values alter the
extended-character and spacing paths and, together with the input byte, can
replace the downstream seventh argument with `0x900` or `0x901`. The exact
source-level enum name is not proven, so the behavioral name above avoids
claiming a specific language enumeration.

## Exact boundary and call graph

The entry at `0x8010F3CC` opens a fresh `0xE0`-byte frame and saves
`s0..s7`, `fp`, and `ra`. The only return restores that frame at
`0x8010F958..0x8010F984`. Thus the exact interval is
`[0x8010F3CC,0x8010F988)`, or `0x5BC` bytes, matching
`generated/SCES_028.45_full.ranges`. `0x8010F988` is the next independent
generated owner.

An exhaustive aligned JAL scan finds no target in the owner's interior and
exactly 24 direct calls to its entry:

```text
800FEA5C 800FEAAC 800FEB58 800FEBC4 800FEC6C 800FECB0
800FED44 800FED90 800FEE00 800FEE44 800FF080 800FF0B4
800FF0E8 800FF11C 80100888 80101554 80101588 8010D418
80110060 80113C44 80127388 801273A4 80128354 8012A81C
```

The calls use modes `8` and `16` directly; generic adapters also forward the
mode and fifth argument. The many mode-16 calls pass zero as the fifth
argument. Calls at `0x80100888`, `0x8010D418`, `0x80128354`, and `0x8012A81C`
provide modes/fifth arguments `(8,6)`, `(8,7)`, `(8,7)`, and `(8,7)`.

The routine calls only two functions: `0x8015F5D4` to obtain the length of its
local 41-byte lookup list, and `0x8010EAC4` once for each rendered glyph. The
latter receives `(glyph, current_x, y, mode, 0, *(u32 *)0x80173F9C, bank)` and
returns the next X pen.

## Input, output, and loop contract

The source is read with `lbu`; therefore all comparisons use unsigned bytes.
The first NUL terminates the loop. A non-space byte is mapped and passed to the
glyph emitter. A space emits no packet and advances X according to the mode
rules below.

At termination the function returns:

```text
current_x - sign_extend_16(initial_x)
```

For the authored screen-coordinate domain this is the total horizontal pixel
advance. The implementation deliberately preserves the full initial `a1` as
the working X but subtracts its signed low halfword, a distinction relevant
only for caller values outside the normal signed-16-bit coordinate domain.

## Base byte-to-glyph mapping

Before localized overrides, the glyph variable is initialized to zero once at
function entry and then updated as follows:

| Input | Glyph |
|---|---:|
| `A..Z` | byte - `0x41` (`0..25`) |
| `0..9` | byte - `0x16` (`26..35`) |
| `:` | 36 |
| `/` | 38 |
| `-` | 40 |
| `[` | 46 |
| `]` | 47 |

Other bytes at least `0x41` take the raw `byte-0x41` path unless an extended
mode overrides them. Other bytes in `0x30..0x40` take `byte-0x16`. Unlisted
bytes below `0x30` retain the previous glyph (or zero before the first mapped
byte), rather than being reset per character. The authored modes add these
overrides:

- Mode `16`: bytes `0x04..0x07 -> 42..45`, `: -> 36`, `. -> 37`,
  `% -> 41`, `+ -> 39`, `[ -> 46`, `] -> 47`, and `_ -> 48`.
- Mode `9`, when the fifth argument is `6`, `7`, `8`, or `9`: `^ -> 82`,
  `? -> 76`, `[ -> 77`, `] -> 78`, `; -> 79`, `. -> 37`, `+ -> 39`,
  `% -> 41`, `A..Z -> 42..67`, `a..z -> 0..25`, and
  `0x04..0x07 -> 80..83`.
- Mode `8` with fifth argument `6`, `7`, `8`, or `9` enters the same
  localized lookup section, while retaining the base mapping for ordinary
  ASCII bytes.

## Accented-byte tables

The routine copies two immutable executable tables to its stack before
processing text. `gLocalizedFontByteTable` at `0x8016D634` is exactly 42
bytes: 41 ISO-8859-1 input values followed by NUL. The paired
`gLocalizedFontGlyphTable` at `0x8016D660` is exactly 41 little-endian signed
halfwords. A linear search uses the byte-table index to load its glyph value.

| Input bytes | Characters | Paired glyphs |
|---|---|---|
| `E0 E1 E2 E4` | `à á â ä` | `0 1 2 3` |
| `E8 E9 EA EB` | `è é ê ë` | `4 5 6 6` |
| `EC ED EE EF` | `ì í î ï` | `7 8 9 9` |
| `F2 F3 F4 F5` | `ò ó ô õ` | `10 11 12 13` |
| `F9 FA FB FC` | `ù ú û ü` | `14 15 16 17` |
| `E7 F1` | `ç ñ` | `18 19` |
| `C0 C1 C4` | `À Á Ä` | `20 21 22` |
| `C8 C9` | `È É` | `23 24` |
| `CC CD` | `Ì Í` | `25 26` |
| `D2 D3 D6` | `Ò Ó Ö` | `27 28 29` |
| `D9 DA DC` | `Ù Ú Ü` | `30 31 32` |
| `C7 D1 DF` | `Ç Ñ ß` | `33 34 35` |
| `B0 A1 BF` | `° ¡ ¿` | `37 38 39` |

On a successful lookup, the downstream bank argument becomes `0x900` when
the fifth argument is `6` or `7`, and `0x901` otherwise. On lookup failure the
base/mode glyph and caller-provided bank remain in effect. The table has no
entry producing glyph 36, which is consistent with that index being handled
as punctuation rather than an accented character.

## Space/layout behavior

Ordinary spaces advance X by 12. For mode `8` or `9` with fifth argument
`6`, `7`, `8`, or `9`, the renderer peeks at the next byte:

- before `A..Z`, the space advance is `12-mode` (4 pixels for mode 8 and
  3 pixels for mode 9);
- otherwise the advance is 2 pixels.

The peek does not consume the next character: pointer bookkeeping advances
the lookahead pointer while leaving that byte as the next loop input.

## Confidence limits

The extent, callers, byte mappings, tables, glyph-emitter arguments, space
advances, and scalar return are instruction-level facts. The exact original
names of modes `8`, `9`, and `16`, the fifth-argument enum, and the visual
identity of banks `0x900/0x901` are not yet proven. Accordingly the symbol is
behavioral and the fifth argument remains described rather than assigned an
unsupported source name.

## Evidence

- `disc/SCES_028.45`: authoritative bytes, table contents, disassembly, and
  exhaustive aligned-JAL census.
- `generated/SCES_028.45_full.ranges`: exact `0x5BC` owner range.
- `generated/SCES_028.45_full_27.c`: complete control flow, local table
  copies, byte mapping, packet-emitter call, and return arithmetic.
- the 24 caller sites listed above: concrete argument and layout use.
