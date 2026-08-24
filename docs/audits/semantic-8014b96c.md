# Semantic audit: `0x8014B96C`

## Result

`0x8014B96C` is the no-argument temp-heap cleanup routine now named
`ReleaseAllTrackedTempHeapBlocks`. It empties the allocation-offset stack
maintained beside the temporary heap and conditionally returns those blocks to
the custom arena allocator.

## Exact boundary

- `0x8014B96C` creates a 32-byte frame and saves `ra/s0/s1/s2`.
- All exits converge on the single epilogue at `0x8014BA28..0x8014BA3C`.
- `0x8014BA40` begins an independent routine with its own 24-byte frame and
  first calls `0x800EEB80`.

The exact callable is therefore `[0x8014B96C, 0x8014BA40)`, `0xD4` bytes, in
agreement with the generated range metadata. It consumes no ABI arguments and
has no defined return value.

## State and behavior

The temp-heap arena pointer is the word at `0x801A862C`. Initialization in the
neighboring `0x8014BAEC` routine establishes the tracker layout: the word at
`0x801E97FC` points to the first 12-byte record (`arena + 0x58`), while
`0x801E9808` is initialized from the word at `arena + 0x50` minus one and acts
as the signed top index. Thus `-1` is the empty sentinel.

If the top index is already `-1`, cleanup returns without touching any other
state. Otherwise it performs these operations:

1. If `0x80178358` is nonnull, pass that internal block pointer with the temp
   arena to `0x80116DBC`, then clear `0x80178358`.
2. Read record `gRecords[top]`, take its first word as an arena-relative block
   offset, decrement and publish the top index, and repeat until it reaches
   `-1`. The other two words in each 12-byte record are not read here.
3. When the lock/suppression word at `0x80178354` is zero, non-final records
   are returned through `0x80116D1C(arena, arena + offset)`. On the final
   record it instead calls `0x80116D1C(arena, NULL)` and clears the suppression
   word; that callee maps null to the arena's initial payload at `arena+0x0C`.
4. When `0x80178354` is nonzero, the routine still discards all tracker records
   by driving the top index to `-1`, but skips the public deallocation calls.

The name says "tracked" deliberately: the routine drains this LIFO tracker,
not every possible allocation in the arena. It does not clear record contents.

## Callees

- `0x80116DBC` is the allocator's internal free-block insertion/coalescing
  path. This routine uses it only for the separately held pointer at
  `0x80178358`.
- `0x80116D1C` is the public arena-block release path. It accepts the arena in
  `a0` and either a payload pointer in `a1`, or null to select `arena+0x0C`, and
  ultimately delegates to `0x80116DBC`.

There are three static call instructions in the cleanup body: one to the
internal path and two mutually exclusive sites to the public path. A single
invocation can call the public path repeatedly while popping records.

## Direct callers

An aligned scan of the retained EXE's loaded text finds **23**, not 24, direct
JALs to the entry:

```text
800F48A8 80110944 80110968 80110A34 80110B3C 80112358 80117A74
80122814 80122854 8012CF00 80131AD4 80131B80 80131BCC 80131CD8
80131DCC 80136238 80141134 80141EAC 80144D0C 80144D80 80144DCC
80144DFC 8014AD14
```

These sites occur after resource loads/builds and before subsequent arena
reuse across menu, race, rendering, and shared setup paths, consistent with a
central temporary-allocation lifetime boundary. No JAL targets an internal
basic block. The earlier count of 24 is therefore an inventory discrepancy,
not an additional proven caller.

## Sources checked

- `disc/SCES_028.45`, respecting its `0x800` PS-X EXE header, for direct MIPS
  disassembly and exhaustive aligned JAL scanning;
- `generated/SCES_028.45_full_45.c` and `.ranges` for control-flow continuity,
  state accesses, and the adjacent independent entry;
- `docs/audits/semantic-8011686c.md` plus direct callee disassembly for the
  custom heap's arena/block model.
