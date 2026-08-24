# Semantic audit: `0x8014B830`

## Result and exact boundary

`0x8014B830` is the current archive-entry accessor
`AcquireCurrentArchiveEntry` (`known`).  The exact range is
`R 8014B830 13C`, covering 316 bytes through `0x8014B96B`.  It creates a
40-byte frame, saves `ra` and `s0..s3`, and returns through the
`jr ra` / stack restore at `0x8014B964..0x8014B968`.  `0x8014B96C` is the next
independent callable, and no direct call enters an interior block.

The sole input is `a0`, used as a zero/nonzero materialization policy.  The
returned `v0` is either null or the selected entry's usable data pointer.

## Current-entry record

The signed global index at `0x801E9808` selects a 12-byte record from the table
whose base pointer is stored at `0x801E97FC`:

```c
struct ArchiveEntryRecord {
    uint32_t archive_offset;  // +0
    uint32_t output_size;     // +4
    uint32_t stored_size;     // +8
};
```

The field names describe their proven use.  The record address is computed as
`table + index*12`; `+4` and `+8` are compared, `+4` is supplied as an
allocation/copy size, and `+0` is added to the archive base.  An index of `-1`
returns null immediately.  The archive base/arena handle is the word at
`0x8019862C`.  The temporary destination publication at `0x80198358` is zeroed
before materialization and updated with each successful allocation.

## Callee graph and materialization paths

There are four distinct callees across seven call instructions:

- `AllocateHeapBlock` (`0x8011686C`) is used on the nonzero-policy path with
  `(archive_arena, output_size)`.
- `0x80116A54` is the alternative destination-acquisition path used when the
  policy is zero, also with `(archive_arena, output_size)`.  Its returned
  pointer is published at `0x80198358`.
- `LoadPackedResourceBundle`'s downstream payload worker `0x80115B1C` is called
  as `(archive_base + archive_offset, destination)` when
  `output_size != stored_size`.  Thus unequal sizes select the packed/decode
  path.
- The BIOS A0/2A thunk at `0x8015CF38` is called as
  `(destination, archive_base + archive_offset, output_size)` when the sizes
  are equal and a nonzero policy requests owned storage.  A0/2A is the Psy-Q
  `memcpy` service, proving the equal-size owned-copy path.

When the sizes are equal and the policy is zero, no allocation or copy occurs;
the function returns `archive_base + archive_offset` directly.  When sizes are
unequal, either policy first obtains destination storage and then invokes the
packed payload worker.  Both allocation branches test for null.

On allocation failure, the function calls `0x800F82EC` with the fixed text at
`0x8016E9E4` and severity/value `2`, then returns the null allocation result.
This diagnostic call appears twice because both allocation choices have their
own failure branch.

## Complete caller proof

An aligned scan of the base executable contains 23 unique direct JAL sites:

- `0x800F4874` (`0x800F4838`);
- `0x801108B4`, `0x8011094C` (`0x80110894`);
- `0x80110A1C`, `0x80110B04` (`0x801109BC`);
- `0x80112278` (`0x801121F0`), `0x80117A38` (`0x801179B8`);
- `0x801227F8`, `0x8012283C` (`RenderRaceFrame`, `0x8012268C`);
- `0x8012CEEC` (`0x8012CD44`);
- `0x80131A4C`, `0x80131B60`, `0x80131BB4`, `0x80131CC0`, and
  `0x80131DB4` (`0x8013199C`);
- `0x80136224` (`0x801361AC`), `0x801410FC` (`0x801410C8`),
  `0x80141E8C` (`0x80141E80`);
- `0x80144C6C`, `0x80144D50`, `0x80144DBC`, and `0x80144DEC`
  (`0x80144C5C`, `0x80144D44`, `0x80144DB4`, `0x80144DE4`);
- `0x8014AD04` (`0x8014ACEC`).

Generated shards contain 24 emitted call occurrences because the single base
instruction at `0x8012CEEC` is duplicated across an overlapping/shard-boundary
emission.  It is one executable call site, not two callers.  The semantic
caller count is therefore 23 unique JAL addresses.

## Semantic limits

The current-index selection, 12-byte layout, direct/owned-copy/packed paths,
allocation arguments, null handling, and returned pointer are exact.  The
specific archive format and the zero-policy allocator's arena behavior remain
separate audits.  “Archive entry” is intentionally broader than texture,
audio, or model because the caller set spans multiple resource-loading paths.
