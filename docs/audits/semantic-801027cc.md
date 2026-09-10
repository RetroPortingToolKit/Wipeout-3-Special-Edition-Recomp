# Semantic audit: `0x801027CC`

## Result

`0x801027CC` is the confirmed nine-element mapper
`BuildNineByteLookupIndexMap`. For every byte in a fixed-size source array, it
searches a shared byte lookup and writes the matching zero-based lookup index
to the corresponding output element. A missing lookup or missing element maps
to zero.

Its recovered ABI is:

```c
uint8_t BuildNineByteLookupIndexMap(
    const uint8_t source[9],
    uint8_t output[9]);
```

The returned value is `output[0]`, zero-extended.

## Boundary and control flow

The generated range is exactly `[0x801027CC,0x80102884)`, `0xB8` bytes. The
entry creates a 32-byte frame and saves `s0`, `s1`, and `ra`. The common exit
reloads them at `0x8010286C..0x80102878`, returns at `0x8010287C`, and restores
the frame in the delay slot at `0x80102880`. The next independent owner begins
at `0x80102884`.

Generated dispatch rows at `0x801027F4`, `0x8010280C`, `0x80102818`,
`0x80102828`, `0x80102838`, `0x8010283C`, `0x80102850`, and `0x80102864` are
loop branches or the post-`Memset` continuation inside the saved frame. No
direct caller targets any of them.

The sole callee is confirmed BIOS `Memset` at `0x8015FEF4`.

## Exact mapping behavior

The outer loop runs exactly nine times. For element `i`, it reads
`source[i]`. Shared word `0x80173A38` is the inclusive maximum lookup index:
if it is negative, the element maps directly to zero. Otherwise the inner loop
searches indices `0..maximum` in the byte array addressed by pointer word
`0x80173A30`. On equality it writes the matching index; exhausting the range
writes zero.

Thus zero represents both a match at lookup index zero and failure to find a
match. The routine does not distinguish those cases and does not reject
duplicate lookup bytes; the first matching index wins.

After writing all nine output bytes it calls:

```c
Memset((void *)0x8018AE44, 0, 8);
```

It then clears word `0x8018AE6C` (`base+0x28`), stores `output[0]` as a word at
`0x8018AE68` (`base+0x24`), and returns that same byte. Source and output should
not overlap in a way that overwrites unread source bytes, because mapping and
stores are interleaved element by element.

## Caller census

The complete generated corpus contains six unique executable entry sites:

```text
0x800DDC88
0x800DDCC4
0x80117904
0x80117910
0x8012ADA4
0x8012ADB4
```

The two `0x800D...` sites occur in four static overlay images each (eight
generated instances total). The two `0x8012AD...` sites each occur in two
coverage copies, while the `0x801179...` sites occur once each. Consequently
14 generated call instances represent six unique executable addresses. Every
call targets the true entry and none targets an interior block.

Caller setup consistently passes two nine-byte arrays; several second calls
advance the output pointer by nine, corroborating the fixed nine-element map.

## Naming limit and side effects

The byte lookup's higher-level domain is not proven by this owner alone. The
conservative symbol states the exact mapping operation without guessing that
the bytes are controls, racers, menu entries, or another game-specific ID.
Beyond the nine output bytes and the three described writes in the shared
`0x8018AE44` state block, it has no state or hardware effects.
