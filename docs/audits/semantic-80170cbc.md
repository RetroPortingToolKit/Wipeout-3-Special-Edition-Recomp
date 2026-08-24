# Semantic audit: guest `0x80170CBC`

## Result

`0x80170CBC` is the sole callable owner of the contiguous routine ending at
`0x80170D3B`.  Its best-supported name is
`ValidateAndRepairReverseOrderingTable`.

The generated entries at `0x80170CD0`, `0x80170D04`, and `0x80170D24` are
control-flow splits introduced by the recompiler.  They are not independent
guest functions and should not receive semantic catalog entries.

## Exact boundary

The preceding renderer routine returns at `0x80170CB4`, restoring its stack in
the delay slot at `0x80170CB8`.  The audited routine begins independently at
`0x80170CBC`.  All paths in the following `0x80` bytes terminate through one
of four `jr ra` instructions at `0x80170CEC`, `0x80170CFC`, `0x80170D1C`, or
`0x80170D34`, each followed by a NOP delay slot.  The next established callable
entry is `InitializeSceneryClipScratchpad` at `0x80170D3C`.

No aligned word in the main executable encodes a `jal` to any of
`0x80170CBC`, `0x80170CD0`, `0x80170D04`, or `0x80170D24`, and none of those
addresses occurs as an aligned function pointer.  Consequently, the internal
split entries have no independent call evidence.  The owner boundary follows
from the independent prologue at `0x80170CBC`, the closed branch graph, and the
four returns before the established next function.

## Ordering-table operation

The inputs are an ordering-table base in `a0` and a word count in `a1`.
`0x80170CBC..0x80170CCC` converts the count to a byte displacement, positions
`a0` at the penultimate entry, and clears the two working registers.  The two
backward scan loops compare the packed 24-bit link stored in the next table
entry with the current entry address.  This is the PlayStation GPU ordering
table invariant: a reverse table entry links to its predecessor using a
24-bit DMA address.

On a mismatch, the path through `0x80170CE0` remembers the affected link and
continues scanning backward.  The join at `0x80170D24` uses `swl` to rewrite
the packed link without disturbing the tag byte, then either resumes the scan
or returns.  The repeated `addiu a0,-4` steps and count decrements prove that
the routine operates on four-byte ordering-table entries rather than geometry
records or executable instructions.

## Catalog decision

Only `0x80170CBC` is cataloged.  The generated range/dispatch machinery may
retain `0x80170CD0`, `0x80170D04`, and `0x80170D24` as resumable split pieces,
but their semantic identity belongs to `ValidateAndRepairReverseOrderingTable`.
