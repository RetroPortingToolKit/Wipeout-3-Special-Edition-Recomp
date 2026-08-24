# Semantic audit: custom heap release family `0x80116D1C` / `0x80116DBC`

## Result

The two neighboring callables are distinct layers of the custom arena heap:

```c
void ReleaseHeapPrefixToMarker(HeapArena *arena, uint8_t *marker); /* 80116D1C */
void FreeHeapBlock(HeapArena *arena, void *payload);               /* 80116DBC */
```

`FreeHeapBlock` is the general DFree implementation: it finds the exact payload
in the arena's linked block list, marks it free, restores accounting, and
coalesces adjacent free blocks. `ReleaseHeapPrefixToMarker` is not a general
pointer-free wrapper. It moves a separately tracked prefix boundary downward,
turning newly exposed prefix storage into a normal free block and then passing
that block to `FreeHeapBlock`.

## Exact boundaries

- `ReleaseHeapPrefixToMarker` owns `[0x80116D1C, 0x80116DBC)`, exactly `0xA0`
  bytes. Its 32-byte frame and sole epilogue are wholly contained in that
  range.
- `FreeHeapBlock` owns `[0x80116DBC, 0x80116EC0)`, exactly `0x104` bytes. It has
  its own 24-byte frame, converges on the epilogue at `0x80116EB0`, and is
  followed by an independent function at `0x80116EC0`.

Both ranges agree with generated metadata. Neither routine defines a useful
return value; callers use them for state effects.

## Arena and block model

The model agrees with the independently audited allocator at `0x8011686C`:

- arena `+0x08`: total currently free bytes;
- arena/block `+0x10`: next block header;
- block `+0x00`: allocation flag (`0` free, `1` allocated);
- block `+0x04`: payload byte count;
- block `+0x0C`: payload pointer;
- each header occupies `0x14` bytes.

The linked list includes the arena header itself. Blocks are kept in address
order, enabling adjacency to be represented by predecessor/successor links.

## `FreeHeapBlock` semantics

Starting at the arena header, the function searches until `block->payload ==
payload`. If the list ends first (including a null arena), it calls the shared
formatter at `0x8015CF48` with the retained string:

```text
error: DFree - memory block not found!
```

On an exact match it:

1. adds the block's payload size to `arena->free_bytes`;
2. if the successor exists and is free, grows the current block by
   `0x14 + successor->size`, bypasses the successor, and adds the recovered
   header's `0x14` bytes to `free_bytes`;
3. if there is no predecessor, or the predecessor is allocated, writes zero
   to the current block's allocation flag;
4. otherwise merges backward into the free predecessor by adding
   `0x14 + current->size`, bypasses the current block, and restores the second
   header's `0x14` bytes to `free_bytes`.

This yields maximal coalescing with both immediate neighbors in one call. The
routine verifies only exact payload membership; it does not reject an already
free block, so a double free can over-credit accounting and is outside its
contract.

## Prefix-marker wrapper semantics

The current boundary is the global word at `0x801749B0`, initialized by the
arena setup routine at `0x80116C40`.

- With `marker == NULL`, the wrapper frees `arena->payload` through
  `FreeHeapBlock(arena, arena->payload)` and clears `0x801749B0`. This releases
  the entire initially reserved prefix.
- With nonnull `marker`, it computes the unsigned distance
  `gHeapPrefixMarker - marker`. A distance below `0x25` is ignored, ensuring
  there is room for a 20-byte header plus usable payload.
- Otherwise it writes an allocated block header at `marker`: payload
  `marker+0x14`, size `distance-0x14`, next equal to the old `arena->next`.
  It installs this block after the arena header and subtracts the whole
  distance from the arena header's prefix size. It then calls
  `FreeHeapBlock(arena, marker+0x14)` so the synthesized block becomes free and
  coalesces normally, and finally publishes `marker` as the new boundary.

The wrapper assumes monotonically decreasing markers. Because subtraction and
the threshold test are unsigned, a marker above the current boundary is not a
supported input.

## Direct callers

The retained EXE contains four direct calls to `ReleaseHeapPrefixToMarker`, all
in the tracked temp-heap cleanup/pop family:

```text
8014B9F8 8014BA20 8014BBF0 8014BC18
```

It contains 15 direct calls to `FreeHeapBlock`:

```text
800E609C 800EE3E4 800EE3F4 800EE404 800EE414 80111E54 80111F34
80112018 801128E4 80115CF8 801160F4 80116D3C 80116D9C 8012D2BC
8014B9A8
```

Two are the wrapper's own delegation sites. The others include archive and
texture-resource teardown, graphics/system cleanup, and the temp heap's
separately held block, confirming that `FreeHeapBlock` is the shared primitive.
No JAL targets an internal block of either owner.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instruction decoding, retained diagnostic, and exhaustive aligned JAL scans;
- `generated/SCES_028.45_full.ranges` for the two adjacent owner ranges;
- `docs/audits/semantic-8011686c.md` for the independently recovered allocator
  model and `docs/audits/semantic-8014b96c.md` for the prefix-marker consumers;
- direct disassembly of arena initialization at `0x80116C40`.
