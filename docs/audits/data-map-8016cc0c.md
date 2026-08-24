# Data map: guest `0x8016CC0C..0x8016F180`

## Result

The entire requested interval is **data, not code**. There are no authentic
basic blocks, function entries, or callable code islands in
`0x8016CC0C..0x8016F183` (the final word beginning at the inclusive address
`0x8016F180`). It is the main executable's packed read-only pool: diagnostic
and UI strings, scalar constants, and address tables used by code below it.

This identified two generated false positives, both now removed by the seed
correction and psxrecomp analysis hardening:

- `func_8016CC0C` was emitted as `psx_unknown_dispatch`, while its CFG was
  incorrectly advertised as a broad code range in the manifest.
- `func_8016F180` is fabricated from the middle of a string. Its three decoded
  words are ASCII/padding, not MIPS control flow.

The closest genuine code boundaries are outside the interval. The preceding
function returns at `0x8016CBF8`, executes its stack-restore delay slot at
`0x8016CBFC`, and is followed by 12 zero bytes at `0x8016CC00..0x8016CC0B`.
After the data pool, the next authenticated function entry is `0x8016F94C`
(`ProjectAndSubmitPackedFaceGroups`), with a complete prologue and return at
`0x8016FA68`. Thus neither endpoint in the current manifest is a plausible
code boundary.

## Authenticated extent

The inspected executable is `disc/SCES_028.45`, SHA-256
`7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`.
With load address `0x80010000` and the PS-X EXE payload beginning at file
offset `0x800`, the requested words map to file offsets
`0x15D40C..0x15F983`. Those 9,592 bytes have SHA-256
`5b1271552a3d76a74b37ff4e24a4c6f2aa7edb1506ea25d8116ff241872642c7`.

The first 20 bytes establish the start as a small scalar table followed by a
string, not an instruction sequence:

```text
8016CC0C  96 00 FA 00 2C 01 5E 01  41 75 74 6F 70 69 6C 6F
8016CC1C  74 20 6A 75 ...
            ^ four uint16 values       "Autopilot jump speed [%d]"
```

The final words establish that `0x8016F180` is interior data:

```text
8016F170  2E 0A 00 00 50 75 74 44  69 73 70 45 6E 76 28 25
8016F180  30 38 78 29 2E 2E 2E 0A  00 00 00 00
                       "PutDispEnv(%08x)...\n\0"
```

The actual object begins at `0x8016F174`; its reference is constructed at
`0x8015D780..0x8015D784`. No code constructs, stores, directly jumps/calls,
or contains an aligned pointer word equal to `0x8016F180`.

## Structural inventory

A conservative byte inventory over `0x8016CC0C..0x8016F18B` (including the
whole object falsely rooted at `0x8016F180`) finds:

- 235 NUL-terminated printable ASCII runs covering 4,748 bytes;
- 30 aligned runs containing 905 nonzero pointers into the executable image;
- the balance is alignment, small integer/halfword arrays, packed constants,
  and short tables separated by strings.

The pointer runs are overwhelmingly switch/jump tables. Representative exact
spans and consumers are:

| Data span | Entries | Address family / role |
| --- | ---: | --- |
| `0x8016CCA8..0x8016CD3B` | 36 pointers + one zero | animation primitive handlers in `0x800E60EC..0x800E79D4` |
| `0x8016CD80..0x8016CE2B` | 43 pointers | animation/chunk handlers in `0x800E7BEC..0x800EC3BC` |
| `0x8016D380..0x8016D46B` | 59 pointers | repeated menu/result handler table rooted near `0x80105C4C` |
| `0x8016D52C..0x8016D633` | 65 pointers + one zero | front-end dispatch entries in `0x8010E208..0x8010F324` |
| `0x8016D6B4..0x8016D89B` | 120 pointers + two zeros | large front-end/menu dispatch table in `0x80110110..0x801104F8` |
| `0x8016E0FC..0x8016E153` | 22 pointers | race/menu handlers in `0x8011C094..0x8011FCE0` |
| `0x8016E2AC..0x8016E31B` | 25 pointers + two zeros | scene/radius handler tables in `0x801293B0..0x8012B4F4` |
| `0x8016E4E0..0x8016E56B` | 34 pointers + one zero | primitive-type dispatch around `0x8012D254..0x8012D968` |
| `0x8016E9FC..0x8016EAE3` | 54 pointers + two zeros | file/system dispatch tables in `0x8014BE74..0x80151A44` |
| `0x8016EB4C..0x8016EDCB` | 160 pointers | SDK/system dispatch tables, heavily defaulting to `0x80156634` |
| `0x8016EDD8..0x8016EE3B` | 24 pointers + one zero | handlers in `0x80157328..0x801575A4` |
| `0x8016EE44..0x8016EFAB` | 76 pointers + five zeros | SDK conversion/format dispatch in `0x8015B630..0x8015CE20` |

Other smaller pointer runs occur at `0x8016CEA4`, `0x8016CEF8`,
`0x8016CF60`, `0x8016D474`, `0x8016D8B8`, `0x8016DB64`, `0x8016DB98`,
`0x8016E598`, `0x8016E620`, `0x8016E684`, `0x8016E6C4`, `0x8016E730`,
`0x8016E798`, `0x8016E7F8`, `0x8016EAEC`, and `0x8016EB0C`. Their word
values likewise point backward to already-authenticated code, rather than
forming executable words at their own storage addresses.

The embedded strings form recognizable producer-local clusters:

- `0x8016CC14..0x8016CF2D`: animation/droid diagnostics and names, separated
  by animation dispatch tables;
- `0x8016CF80..0x8016D37F`: race results, team names, cheat tokens and labels,
  viewport-layout labels;
- `0x8016D46C..0x8016DB61`: lap/demo text, track paths, texture/decompression
  failures, and ISO9660 diagnostics;
- `0x8016DB94..0x8016E20E`: serial/movie/heap/menu strings, music metadata,
  player labels, `.pbp`, `About to Start Race`, and the confirmed
  `c:\\wipeout3\\common\\shads.bt4` at `0x8016E170`;
- `0x8016E31C..0x8016E9F8`: scene, radius, ship, texture, track and TTF
  diagnostics;
- `0x8016EA24..0x8016F180`: system-library tables and debug strings, including
  hexadecimal digit alphabets, Psy-Q source IDs, GPU operation labels, and
  the `PutDispEnv` format string crossing the false endpoint.

These are not merely printable coincidences. A scan of authenticated compiled
ranges finds address constructions for 203 distinct starts in this pool. For
example, code loads `0x8016CCA8` at `0x800E60CC..0x800E60D0`,
`0x8016D380` at `0x80105C04..0x80105C08`, `0x8016E0FC` at
`0x8011C074..0x8011C078`, `0x8016E170` at
`0x80124928..0x8012492C`, and `0x8016F174` at
`0x8015D780..0x8015D784`. The references use the addresses as table bases or
ordinary data pointers. Conversely, exhaustive aligned-word and direct
`j`/`jal` scans find no reference to either false entry `0x8016CC0C` or
`0x8016F180`.

## Why the generated `0x8016F180` body is impossible

The generated body interprets the string bytes as:

```text
8016F180  29783830  slti  t8,t3,14384       # ASCII "08x)"
8016F184  0A2E2E2E  j     0x88B8B8B8        # ASCII "....\n"
8016F188  00000000  nop                     # terminator/padding
```

`0x88B8B8B8` is outside the loaded executable and is produced solely by the
three period bytes. There is no incoming control-flow reference, no callable
prologue, no valid return, and no executable target for the apparent jump.
Treating an out-of-image direct jump as a clean tail exit allowed this
12-byte seed to evade the current data classifier.

## Required corrections

1. Remove `0x8016F180` from `seeds/ghidra_funcs.txt`. It is a demonstrably
   false Ghidra function seed.
2. Add `0x8016F94C` as the real private renderer root (preferably with the
   project's explicit indirect/call-root form if supported). This closes the
   currently omitted `0x124`-byte function at `0x8016F94C..0x8016FA6F`.
3. Make `CodeGenerator::generate_ranges_manifest` omit functions classified
   as data/untranslatable, matching the dispatch-table filter already applied
   to `GeneratedFunction::dispatchable`. This removes the false
   `F/R 8016CC0C` claim instead of advertising an unknown-dispatch stub's CFG
   as native code.
4. Tighten the function-extent classifier: a direct `j` outside the loaded
   executable (such as `0x88B8B8B8`) must not by itself prove a clean tail
   exit. It needs a recognized executable/import target or independent
   control-flow evidence.
5. Regenerate the base outputs. The corrected manifest should contain neither
   `F 8016CC0C` / `R 8016CC0C 2574` nor `F 8016F180` /
   `R 8016F180 C`; it should gain the authentic `F 8016F94C` with its exact
   `R 8016F94C 124` before the existing `0x8016FA70` sibling.

Until regeneration, coverage calculations must subtract
`0x8016CC0C..0x8016F18B` from base native-code coverage. The existing range is
not weak evidence for code; it is contradicted by the executable bytes and by
the generated fail-closed body itself.

## Evidence inspected

- authenticated bytes in `disc/SCES_028.45`;
- `generated/SCES_028.45_full.ranges`;
- `generated/SCES_028.45_full_140.c`;
- `generated/SCES_028.45_dispatch.c` and declarations;
- `seeds/ghidra_funcs.txt` and `game.toml`;
- retained renderer evidence and the boundary audit in
  `docs/audits/semantic-8016f94c.md`;
- direct address-construction, aligned-pointer, and `j`/`jal` scans over the
  authenticated base-image compiled ranges.
