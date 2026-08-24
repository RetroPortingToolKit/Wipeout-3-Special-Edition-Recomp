# `0x8011686C` custom heap allocator

## Result

`0x8011686C..0x80116A53` is the exact 488-byte callable now named
`AllocateHeapBlock`:

```c
void *AllocateHeapBlock(HeapArena *arena, uint32_t requested_bytes);
```

The next independently generated callable begins at `0x80116A54`, and the
committed range/dispatch metadata both give `0x8011686C` size `0x1E8`.

## Allocation semantics

- The request in `a1` is rounded upward to a four-byte boundary by
  `(size + 3) & ~3`; an already aligned request is unchanged.
- The arena argument in `a0` is the head/sentinel of a linked block list and
  owns a remaining-byte counter at `arena + 8`.
- Each block has an allocation flag at `+0`, payload size at `+4`, payload
  pointer at `+12`, and next pointer at `+16`. A split therefore costs a
  20-byte header.
- The first traversal accepts only free blocks with at least
  `aligned_size + 20` bytes. Allocation is carved from the tail: the old free
  block shrinks, a new allocated header is written at its new end, linked
  after it, and its payload is returned from `new_header + 12`.
- The arena remaining count is reduced by `aligned_size + 20` for that split.
  When the arena matches the main-heap identity stored at `0x801D1B8C`, the
  resulting remaining count can lower the main-heap low-water value at
  `0x801749AC`.
- If no block can be split, the routine prints
  `insufficient continuous memory! %s` and identifies the arena as either
  `temp heap` (the identity stored at `0x801A862C`) or `main heap`, followed by
  `requested:%d  -  free:%d`. It then makes a second traversal.
- That fallback may consume a whole free block whose payload size is strictly
  greater than the aligned request. It sets the allocation flag, subtracts
  only the aligned payload size from `arena + 8`, and returns the existing
  payload pointer at block `+12`.
- If that fallback also finds no usable free block, it returns null. Thus the
  diagnostic marks failure to find *splittable contiguous* storage; it can
  precede a successful whole-block allocation and is not by itself a null
  result.

## Callers and subsystem role

The base executable contains 85 direct JAL sites spanning at least 40 generated
callables. They include race runtime/resource initialization (`0x8012482C`),
texture setup (`0x80141D6C`), rendering/resource builders, menu code, and lower
address overlay-facing routines. That breadth, together with the embedded
main/temp heap diagnostics and arena identity globals, proves this is the
game's shared custom arena allocator rather than a race-only allocator or the
BIOS `InitHeap` service.

No callee other than the diagnostic formatter at `0x8015CF48` is invoked.
Normal success and failure both return through `0x80116A40..0x80116A53`.
