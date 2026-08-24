# Semantic audit: `0x801415F4`

## Result

`0x801415F4` is a genuine ABI-callable function.  The old guessed
`SceneObjectVisWalk` name describes only its preliminary record walk.  A more
complete, evidence-backed name is `BuildVisibleObjectQuadPackets`: the function
updates per-object activity flags, performs visibility setup and compaction,
then builds and ordering-table-links one GPU `POLY_FT4` packet for each visible
oriented quad.

The symbol can therefore be promoted from `guessed` to `known`.  This is still
a descriptive clean-room name; the original source identifier is not known.

## Boundary and caller

- Start: `0x801415F4`, a conventional non-leaf prologue with a 56-byte frame,
  saved `ra`, `fp`, and `s0-s7`.
- End: `jr ra` at `0x80141954`, with stack restoration in its delay slot at
  `0x80141958`.  The generated extent is exactly `0x368` bytes
  (`0x801415F4..0x8014195B`).
- The retained base image contains one direct call, a `jal` at `0x801243A8`.
  Its caller loads `a0` from an element of a table whose computed element
  stride is 180 bytes, so `a0` is a pointer to an object-group/section record,
  not an individual render primitive.
- There are no direct branches into the middle of the function.  Dispatch
  entries within the extent are continuation points created by the static
  recompiler, not additional ABI entries.

The observed ABI is approximately:

```c
void BuildVisibleObjectQuadPackets(ObjectGroup *group); // a0
```

The caller does not consume `v0`.  The function may leave helper results in
`v0`, so a meaningful return value is not supported by the evidence.

## Input and preliminary activity pass

The function uses at least these fields from `group`:

- `+0`: pointer to the 28-byte source-record array passed to the visibility
  compactor;
- `+12`: signed view-angle bias (negated for the compactor);
- `+68`: transform/object used by the GTE setup helper at `0x80172DDC`.

If the signed global count at `0x801762CA` is non-positive, the function
returns immediately.  When the related count at `0x801762C8` is positive, it
walks that many 28-byte records from `*(void **)0x801E247C`.  For each record it
compares `*(record + 24)->word_0` with `*(uint32_t *)0x801762CC` and sets or
clears bit `0x8000` of `record->halfword_10`.  This is an enable/disable or
selection pass, not rendering by itself.

## Visibility stage

The call at `0x801416A4` targets `0x80172DDC`.  That helper applies the
group's `+68` transform, copies the GTE translation into scratchpad words
`0x1F800000..0x1F800008`, and returns.

The function then calls the independently audited
`BuildVisibleOrientedQuadList` at `0x80172E3C` with:

```text
a0 = group->field_00
a1 = 28
a2 = *(int16_t *)0x801762CA
a3 = -group->field_0C
```

That helper rejects disabled, near, and wholly off-screen records and writes a
compact 20-byte-per-entry list to scratchpad.  It returns the visible count or
`-1` on its fixed-capacity overflow sentinel.  This caller returns without
building packets for `-1` or any non-positive count.

## Packet construction

For every accepted scratch entry, the loop at `0x801416F4..0x801418F4`:

1. Reserves the current 40-byte packet at `*(void **)0x80198620`.
2. Writes packet length 9 and GPU command byte `0x2C`, identifying a textured
   four-point polygon (`POLY_FT4`).
3. Copies the four packed screen-coordinate words from the scratch record into
   packet offsets `+8`, `+16`, `+24`, and `+32`.
4. Uses the scratch source index to address a 28-byte record in
   `*(void **)0x801E247C`, then derives color/UV, CLUT, and texture-page fields
   from that record and the texture tables.  The branch on descriptor flag bit
   0 selects helper `0x8015FD4C` or `0x80160274` for the packet's `+22`
   halfword.
5. Computes the ordering-table slot from the visible record's depth and links
   the packet by exchanging 24-bit DMA-chain pointers at `0x801418D0..DC`.
6. Advances `0x80198620` by exactly 40 bytes.

Thus the central product of the function is a set of depth-sorted GPU packets,
not a retained scene-graph traversal result.

After the packet loop, global state bytes at `0x801D1B3E` and `0x801D1B41`
conditionally cause a call to `0x80141288`.  That epilogue-side behavior does
not change the packet-builder classification, but it means the function is not
safe to model as a pure converter.

## Evidence

- `generated/SCES_028.45_full_126.c`: complete function, frame, visibility
  calls, packet loop, OT insertion, and epilogue
- `generated/SCES_028.45_full_117.c`: sole direct caller at `0x801243A8`
- `generated/SCES_028.45_full.ranges`: `F 801415F4`, range size `0x368`
- `generated/SCES_028.45_dispatch.c`: owner and continuation map
- `docs/audits/semantic-80172e3c.md`: independently established visibility
  helper ABI, capacity, and 20-byte scratch-output format
