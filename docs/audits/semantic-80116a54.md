# Semantic audit: `0x80116A54`

## Result and ABI

`0x80116A54` is the confirmed custom allocator
`AllocateArenaBlockBestFit`:

```c
void *AllocateArenaBlockBestFit(Arena *arena, uint32_t requested_bytes);
```

`a0` is an arena/control record and `a1` is the requested payload size.
Incoming `a2/a3` are ignored. The result is either the selected block's
payload pointer or null. The allocator mutates arena metadata and its linked
block headers; it does not initialize payload bytes.

## Exact boundary and targets

The generated owner is exactly `[0x80116A54,0x80116C40)`, `0x1EC` bytes. It
opens a 32-byte frame at `0x80116A54`; the common exit loads the result from
the selected header at `0x80116C28`, returns at `0x80116C38`, and restores the
frame at `0x80116C3C`. The independent next callable starts at `0x80116C40`.

The many dispatch rows are scan, failure, and split/link control-flow blocks.
No direct generated JAL or jump targets an interior row. The only outgoing
callee is the `printf` BIOS/library thunk at `0x8015CF48`, used by two failure
diagnostics.

## Alignment and best-fit selection

If `requested_bytes & 3` is nonzero, the allocator rounds it upward to the next
multiple of four. Already aligned values are unchanged. It then adds the exact
20-byte block-header requirement when checking candidates.

The block list begins at `arena+0x10` and advances through header word `+0x10`.
For each candidate it reads allocation state at `+0` and block size at `+4`.
It considers only free blocks large enough for `aligned_size + 20`, and keeps
the smallest qualifying size, with an initial upper bound of `0x00800000`.
This strict replacement-by-smaller-fit behavior proves best fit rather than
first fit.

## Failure and successful mutation

If no fitting block is found, the function issues two retained diagnostic
prints through `0x8015CF48` using fixed strings near `0x8016DC7C..0x8016DCB8`
and the requested/available arena figures, then returns null. It does not
silently fall back to another heap.

On success it carves a 20-byte header plus aligned payload from the selected
free extent. The new/remaining header fields include allocation state, size,
payload pointer at header `+0x0C`, and next link at `+0x10`; the surrounding
links are repaired. Arena word `+8` is debited by payload plus header overhead.
The allocator also lowers global word `0x801749AC` when the new free-byte total
is below the previous minimum, making that word a low-water mark. The returned
pointer is exactly the selected allocated header's `+0x0C` field.

Zero-byte requests are valid at this level: zero is already aligned, and the
fit still requires/header-charges 20 bytes. One audited bundle caller uses
that behavior deliberately.

## Exhaustive callers

There are exactly five unique direct calls:

```text
80111B80  (LoadPackedResourceBundle)
801123DC
80115B44  (packed/decode path)
80115F64
8014B8A0  (archive materialization owner)
```

All calls target the exact entry and pass an arena pointer in `a0` plus a byte
count in `a1`. There is no emitted duplicate/overlay copy and no interior
caller. Existing audits independently show `0x80111B80` requesting zero bytes
from the arena at pointer word `0x8019862C`, and `0x8014B8A0` using the returned
pointer as the zero-policy destination for archive materialization.

## Confidence

`confirmed`: range, ABI, four-byte alignment, header cost, free-list fields,
best-fit policy, diagnostics/null failure, split/link accounting, low-water
mark, payload return, and caller/interior census are instruction-proven. The
original source struct names are not asserted.
