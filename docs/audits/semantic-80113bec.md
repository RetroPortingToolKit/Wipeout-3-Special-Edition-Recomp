# Semantic audit: `0x80113BEC`

## Result

`0x80113BEC` is the confirmed resource-string adapter `DrawResourceString`.
It replaces an integer string index with the corresponding pointer from the
runtime table at `0x8017BF1C`, forwards the remaining coordinates and style to
`DrawFontString`, and returns that renderer's horizontal advance unchanged.

Its exact ABI is:

```c
int DrawResourceString(int string_index, int x, int y, int style) {
    return DrawFontString(gResourceStringTable[string_index], x, y, style);
}
```

There is no bounds check, null check, coordinate conversion, style conversion,
or additional state write in the adapter.

## Boundary and call graph

The preceding function restores its own 40-byte frame and returns at
`0x80113BE4..0x80113BE8`. This adapter opens a fresh 24-byte frame at
`0x80113BEC`, saves only `ra`, and returns at `0x80113C18` with its stack
restore at `0x80113C1C`. The next function opens a separate 32-byte frame at
`0x80113C20`. Therefore the exact interval is
`[0x80113BEC,0x80113C20)`, or `0x34` bytes, matching the generated manifest.

Its sole callee is `DrawFontString` at `0x80105B78`. Because the adapter does
not write `v0` after that call, it returns the callee's total horizontal
advance—not the final pen X, character count, string pointer, or packet
address. `x`, `y`, and `style` remain in `a1`, `a2`, and `a3` throughout and
are forwarded bit-for-bit.

An aligned JAL scan finds exactly 41 direct callers:

```text
800FE1E4 80100858 801008A4 80101064 801010F0 80101180 80101450
801014DC 801023D0 8010781C 80107884 80126EDC 80126F40 80127000
80127238 80127264 80127280 801272EC 801273D8 801273F0 80127404
8012743C 80128194 80128204 80128290 801282A4 801282B8 801282D8
80128334 801285DC 801285FC 80129510 80129534 8012954C 80129568
80129580 80129A54 8012A230 8012A25C 8012A278 8012A8B8
```

Many callers add the returned width to an existing X coordinate or feed it to
the next layout operation. Constant string indices include `6`, `9`, `11`,
`0x7B`, `0xC9..0xD0`, `0xF3`, `0xF4`, `0x105`, `0x106`, and `0x147`; other
callers derive indices from menu/race state.

## Resource-string table

The adapter forms the exact effective address
`0x80180000 - 0x40E4 = 0x8017BF1C`, then loads
`*(uint32_t *)(base + string_index*4)`. The table contains pointers to
NUL-terminated strings consumed by `DrawFontString`; other code passes entries
from the same base to the recovered `FormatString`, proving that these are
resource/format strings rather than inline glyph records.

The retained executable bytes at `0x8017BF1C..0x8017BFFF` are all zero. The
generated entries `F 8017BF1C` / `R 8017BF1C E4` therefore misclassify the
zero-filled tail before the next 4 KiB boundary as executable code. They do
not establish a 228-byte function or even the table's true size.

Runtime users access well beyond that artificial boundary: fixed loads at
base offsets `0x544`, `0x54C`, and `0x550` address entries 337, 339, and 340.
The strongest bounded conclusion is therefore a runtime-populated word table
with at least 341 entries. This audit names its base `gResourceStringTable` but
does not claim a final array bound or a specific language-file loader without
additional initialization evidence.

## Downstream rendering semantics

The independent `DrawFontString` audit proves that the downstream function:

- reads the selected pointer as a NUL-terminated byte string;
- treats `a1` as starting X and `a2` as Y;
- uses `a3` as font/style, including its conditional high-bank remap;
- emits one glyph packet for each non-space byte and advances spaces by eight;
- returns `final_x - sign_extend_16(start_x)`.

All these semantics carry through this transparent adapter. Its only unique
behavior is indexed string selection.

## Evidence

- `disc/SCES_028.45`, exact MIPS disassembly, zero-tail inspection, table-use
  scans, and exact direct-JAL census;
- `generated/SCES_028.45_full_110.c`, complete adapter control flow;
- `generated/SCES_028.45_full.ranges`, exact adapter boundary and false table
  code range;
- `docs/audits/semantic-80105b78.md`, independently recovered string-renderer
  ABI, layout, and return semantics;
- `symbols.toml`, recovered `FormatString` identity at `0x8015C698`.
