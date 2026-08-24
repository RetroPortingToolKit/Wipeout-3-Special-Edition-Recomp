# Semantic audit: `0x8015F56C` / `FillBytes`

## Result

`0x8015F56C` is a compact byte-fill leaf now named `FillBytes`:

```c
void FillBytes(void *destination, int fill_value, uint32_t count);
```

For nonzero `count`, it writes the low eight bits of `fill_value` to exactly
`count` consecutive bytes. A zero count returns without touching memory.

## Exact owner and ABI

Generated metadata gives the entry one exact `0x24`-byte range. The complete
instruction sequence ends with `jr ra` at `0x8015F588` and a NOP delay slot at
`0x8015F58C`, so the callable is `[0x8015F56C,0x8015F590)`. The word at
`0x8015F590` is unreachable padding; the next independent BIOS trampoline
begins at `0x8015F594`.

`a0` is the destination pointer, `a1` supplies the fill byte, and `a2` is the
unsigned count. There is no alignment requirement because the loop uses `sb`.
The destination pointer advances internally but is not returned.

The zero-count branch's delay slot computes `v0 = count - 1`, producing `-1`.
For a nonzero count, the loop decrements the same register until it reaches
`-1`. Thus every path leaves `v0 == -1`. Every caller ignores this residue.
This proves a void helper contract and rules out naming it canonical C
`memset`, whose required return is the original destination.

## Exact loop

The implementation is equivalent to:

```c
void FillBytes(uint8_t *destination, int fill_value, uint32_t count) {
    if (count == 0)
        return;

    uint32_t remaining_minus_one = count - 1;
    do {
        *destination++ = (uint8_t)fill_value;
    } while (remaining_minus_one-- != 0);
}
```

There are no reads from the destination, no overlap-specific behavior, no
word-at-a-time optimization, and no global or hardware state effects.

## Exhaustive callers and targets

An aligned scan of the retained executable finds six unique JALs:

```text
8015CFF0  8015D064  8015D074  8015D1FC  8015F14C  8015F160
```

All target the true entry; no J/JAL targets an interior instruction or the
padding word. Generated output also emits exactly these six calls, so there is
no overlay duplication.

Observed calls prove both byte values and the non-returning contract:

- graphics-system initialization clears `0x80`, `0x100`, and `0x1800` bytes
  with value zero;
- packet/environment setup fills `0x5C`, `0x14`, and `0x14` bytes with `0xFF`;
- every site continues without consuming `v0`.

These sites also show that the helper is a general fill operation, not a
zero-only clearing routine.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, unique callers, arguments, and interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated caller windows for fill values, sizes, and return consumers.
