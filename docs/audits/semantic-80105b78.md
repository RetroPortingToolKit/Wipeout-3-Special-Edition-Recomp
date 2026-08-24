# Semantic audit: `0x80105B78` / `DrawAsciiTextString`

## Result

`0x80105B78` is the common uppercase-ASCII string renderer now named
`DrawAsciiTextString`:

```c
int DrawAsciiTextString(const uint8_t *text, int x, int y, int style);
```

It maps each non-space byte to the game's glyph numbering, threads the X pen
through `EmitFontGlyphPacket`, and returns rendered width rather than final X.

## Exact logical owner and ABI

The complete owner is `[0x80105B78,0x80105CDC)`, exactly `0x164` bytes. It
opens a `0x28`-byte frame, has one loop and one common epilogue, returns at
`0x80105CD4`, and restores the stack at `0x80105CD8`. The independent glyph
renderer begins at `0x80105CDC`.

Generated ranges repeat the same owner after 12 labels at
`0x80105C24..0x80105C7C`. Eleven are punctuation case arms and the last is the
generic mapping block; they are indirect jump-table destinations emitted as
alias entries, not separate functions.

`a0` is the NUL-terminated byte string, `a1` the starting X, `a2` Y, and `a3`
style. Initial X is narrowed to a signed halfword solely for the final
measurement baseline. On NUL the function returns:

```c
current_x - (int16_t)initial_x;
```

## Mapping, spacing, and style behavior

The exact authored mapping is:

- `A..Z` to glyphs `0x00..0x19`;
- `0..9` to `0x1A..0x23`;
- `:` `.` `/` `+` `-` `%` to `0x24..0x29`;
- `[` `]` `_` `=` `?` to `0x2E..0x32`.

Space emits no packet and advances X by exactly eight. Other unsupported
punctuation below `'0'` retains initialized fallback glyph zero. Bytes at or
above `'A'` mechanically use `byte - 'A'`, so lowercase/high bytes are not
validated and may index beyond the authored glyph set.

Before the loop, style is increased by 14 only when all three conditions hold:

```text
*(uint32_t *)0x8018AE94 == 2
*(uint32_t *)0x801757A8 != 0
style < 6
```

Otherwise it is unchanged. This extra `0x801757A8` gate distinguishes the
common renderer from neighboring numeric formatters.

## Render and packet behavior

Every non-space byte performs:

```c
current_x = EmitFontGlyphPacket(glyph, current_x, y, selected_style);
```

The owner itself writes no packet or ordering-table state. Its sole callee
advances `gPrimCursor` by 40, builds and OT-links one fixed-neutral-gray
`POLY_FT4`, selects style-dependent semitransparency, and returns the next X.
Thus an input with `n` non-space bytes emits exactly `n` packets. This function
does not use caller-tinted `EmitGrayscaleFontGlyphPacket`.

## Exhaustive caller and translation census

The retained executable has 69 direct entry calls. Static overlay translations
add runtime PCs `0x800D5B20`, `0x8017BAE0`, and `0x8017BB1C`, giving 72 unique
runtime call addresses:

```text
800D2FAC 800D3014 800D5B20
800FE284 800FE328 800FE384 800FE3C8 800FF030
801009C0 80100FE4 80101034 801013EC 80101438 801014C8 80101510
80102468 80102584 8010729C 801072D4 8010732C 80107578 80107D04
80108444 8010B6EC 8010B768 8010B7A4 8010D6D0 8010D704 8010D720
8010D744 8010D760 80113C08 80117070 80117194 8011721C 80117368
801173DC 80117530 801175A4 801270E8 80127320 801281C0 801281E0
80128258 8012844C 80128488 801284C0 801284E8 80128694 801286B0
8012873C 80128758 801287A4 801287D0 801287FC 80128828 8012925C
8012929C 801292CC 801295C4 801295E4 80129AA4 8012A2B8 8012A2D0
8014B74C 8014B79C 8014B7C4 8017BAE0 8017BB0C 8017BB1C 8017BB48
8017BB74
```

Generated output has 104 emissions. The duplication multiplicities are:

```text
8017BAE0 x10  8017BB1C x10  8017BB48 x11
800D5B20 x4   80107D04 x2   all other unique PCs x1
```

These are repeated overlay/overlap translations, not additional runtime
addresses. An exhaustive aligned J/JAL scan finds no external target into the
owner interior. Its direct interior jumps are local case convergence, loop,
and epilogue edges. The only outgoing call instruction is `0x80105CA4` to
`EmitFontGlyphPacket`; it is itself emitted twice through an overlapping case
translation but remains one physical instruction.

## Sources checked

- `disc/SCES_028.45`, excluding its PS-X EXE header, for exact instructions,
  69 retained calls, mapping table, and interior-target scan;
- `generated/SCES_028.45_full.ranges` and dispatch aliases for exact owner and
  case-entry proof;
- all generated main/overlay call translations for the 72-unique/104-emitted
  census;
- `docs/audits/semantic-80105cdc.md` and
  `docs/audits/semantic-80105354.md` for the sibling packet behaviors.
