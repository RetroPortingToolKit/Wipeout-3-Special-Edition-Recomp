# Semantic audit: `0x800DF694` / `InitializeDefaultHighScoreBank`

## Result

`0x800DF694` is a runtime-overlay high-score table initializer now named
`InitializeDefaultHighScoreBank`:

```c
void InitializeDefaultHighScoreBank(int unused, int validity, int bank);
```

When `validity == -1`, it seeds one complete bank of default race-time and
lap-time records. Any other validity value returns without changing state.

## Exact logical owner and overlay evidence

Generated metadata reserves `[0x800DF694,0x800DF8A4)`, exactly `0x210` bytes,
but the retained base executable contains zeroes there. This address is an
overlay execution slot: the sole caller first loads an archive into the
`0x800BF800` overlay arena, invokes other entry points in that image, and then
issues the five calls below.

The implementation was recovered while resident in the retained
`saves/_stale_prepatch/state_80162474_slot00.pst` RAM image. It has a
conventional `0x48`-byte frame, one early branch to the common epilogue, two
calls to `Strcpy`, and one return at `0x800DF89C` with the stack restore at
`0x800DF8A0`. The next generated owner begins at `0x800DF8A4`, proving the
logical range exactly. Later snapshots contain unrelated data at the same
address, independently confirming that this is reused overlay memory rather
than a zero-filled base-EXE function.

## ABI and gate

`a0` is overwritten before use and is semantically unused. `a1` is compared
with `-1`; a mismatch branches directly to the restore/return sequence. `a2`
selects the destination bank without validation. No caller consumes a scalar
result, so the function is mutating and void.

The five calls, all from one initialization sequence, pass:

```text
call        a0  a1  a2       effect
80117AB0   -1  -1   1       initialize bank 1
80117AC0   -1  -1   0       initialize bank 0
80117AD0    0   0   0       no-op
80117AE0    1   5   0       no-op
80117AF0    2   7   0       no-op
```

Thus the validity/sentinel interpretation is exact even though the higher
level meaning of the nonnegative values is not assigned here.

## Record layout and exact state effects

The selected destination is `0x80188A10 + bank * 0xDC0`. It contains 22
`0xA0`-byte groups. Each group has four `0x14`-byte race-time records at
offsets `0x00..0x3C` and four paired lap-time records at `0x50..0x8C`.
For each rank `i = 0..3`, both records receive:

- an inline NUL-terminated name copied from a 17-entry, 9-byte-stride table at
  `0x80175AE4`;
- a signed default time widened from the corresponding halfword in the
  22-row table at `0x80175824`, stored at record `+0x0C`;
- the current low byte of word `0x8017BE9C`, stored at record `+0x10`.

The two halves select adjacent time columns. For track `t` and rank `i`, bank
1 reads byte offsets `t*0x20 + i*8 + {0,2}` and bank 0 reads
`t*0x20 + i*8 + {4,6}`. The name index advances modulo 17 after each record,
so paired records receive consecutive names. The global record byte advances
once per pair as `(value + 1) & 7`; both records in a pair receive its old
value. Overall, one accepted call writes 88 records and advances that global
88 times, returning it to its original modulo-eight value.

The resident source strings (`NEIL`, `MCGINLEY`, `PATRIK`, `FINNIGAN`, and 13
other developer-style names) and paired larger/smaller time values establish
that these are default high-score race/lap records, rather than generic named
objects. The name stays conservative about the higher-level identity of banks
0 and 1.

## Callers, callees, and interior targets

An exhaustive aligned J/JAL scan of both the retained executable and the
resident overlay snapshot finds exactly the five entry calls listed above.
There is no call or jump to an interior address and no second resident copy of
the implementation. The five instructions are also the complete generated
call set; this is overlay loading/reuse, not duplicated call emission.

The only outgoing calls are `0x800DF77C` and `0x800DF7E4`, both to confirmed
`Strcpy` at `0x8015F614`. All other control flow is local branching.

## Sources checked

- `generated/SCES_028.45_full.ranges` for the reserved exact owner range;
- `disc/SCES_028.45` for the zero-filled overlay destination and unique caller
  sequence;
- `saves/_stale_prepatch/state_80162474_slot00.pst` for the complete resident
  implementation, data tables, destinations, and full-RAM target scan;
- generated call sites for the absence of duplicated emissions.
