# Semantic audit: `0x8014BB94`

## Result and exact owner

`0x8014BB94` is the no-argument tracker pop now named
`PopTrackedTempHeapBlock`. The generated range is exactly
`R 8014BB94 9C`, so the owner is `[0x8014BB94, 0x8014BC30)`. It creates a
24-byte frame, saves `ra/s0`, and all paths converge on the epilogue at
`0x8014BC20..0x8014BC2C`. The preceding callable ends at `0x8014BB93`, and
`GetCurrentArchiveEntryOutputSize` begins independently at `0x8014BC30`.

An exhaustive aligned control-transfer scan found no external target inside
the owner. The only direct interior jump is the owner's own
`0x8014BBF8 -> 0x8014BC20` epilogue edge. A byte scan of the retained EXE found
no duplicate of the complete 0x9C-byte body, so this is neither a generated
split nor an overlay alias.

## ABI and behavior

The function reads no incoming argument register and callers do not consume
`v0`; its ABI is therefore `void PopTrackedTempHeapBlock(void)`.

The signed word at `0x801E9808` is the top index for the 12-byte archive-entry
table whose base pointer is stored at `0x801E97FC`. The table layout was proved
by `AcquireCurrentArchiveEntry`: word `+0` is an arena-relative archive/block
offset, `+4` is output size, and `+8` is stored size. This routine does exactly
one LIFO pop:

1. If the top is `-1`, return without changing state.
2. Read word `+0` from `table[top]`, decrement the top, and publish the new top
   immediately at `0x801E9808`.
3. If another record remains and suppression word `0x80178354` is zero, call
   `ReleaseHeapPrefixToMarker(arena, arena + popped_offset)`, where the arena
   pointer is `0x8019862C`.
4. If the popped record was final and suppression is zero, call
   `ReleaseHeapPrefixToMarker(arena, NULL)`. Whether or not that final call was
   suppressed, clear `0x80178354` as the stack becomes empty.

Thus a nonzero suppression word still discards the tracker entry but prevents
the arena rollback. On a non-final pop it remains set; on the final pop it is
cleared. Record contents and the separately tracked pointer at `0x80178358`
are untouched. The all-record sibling `ReleaseAllTrackedTempHeapBlocks` at
`0x8014B96C` performs the same rollback policy in a loop and additionally
services that separate pointer.

## Callers and callees

The retained EXE has exactly five direct entry JALs:

```text
80112368 80131AE4 80131B90 80132178 8013220C
```

No call targets an interior block. These sites lie in resource-processing
loops; several explicitly pair the one-record pop with calls to
`ReleaseAllTrackedTempHeapBlocks` on the alternate cleanup path. No caller
sets an argument specifically for this call or reads a return value.

The only callee is confirmed `ReleaseHeapPrefixToMarker` (`0x80116D1C`), at
two mutually exclusive sites for final-null and non-final-offset rollback.

## Sources checked

- `disc/SCES_028.45`, after its 0x800-byte PS-X EXE header, for direct MIPS
  disassembly, callers, interior targets, and exact-body duplication;
- `generated/SCES_028.45_full_45.c` and `.ranges` for owner continuity and
  state accesses;
- the audits for `AcquireCurrentArchiveEntry`,
  `ReleaseAllTrackedTempHeapBlocks`, `GetCurrentArchiveEntryOutputSize`, and
  `ReleaseHeapPrefixToMarker` for the shared table and allocator semantics.
