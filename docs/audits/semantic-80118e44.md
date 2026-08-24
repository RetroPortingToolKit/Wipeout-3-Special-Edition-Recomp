# Semantic audit: `0x80118E44`

## Result

`0x80118E44` is the confirmed bump-arena descriptor initializer
`ConfigureBumpArena`. It selects one of the three descriptors used by
`AllocateFromThreeBumpArenas`, stores a base pointer and four-byte-aligned
capacity, and deliberately leaves the descriptor's current cursor unchanged.

Its recovered ABI is:

```c
void ConfigureBumpArena(int32_t arena_index,
                        void *base,
                        uint32_t capacity);
```

Equivalent successful behavior is:

```c
BumpArena *arena = &g_load_arenas[arena_index];
arena->base = base;
arena->capacity = capacity & ~3u;
// arena->cursor at +8 is not written
```

## Boundary and exits

The generated range is exactly `[0x80118E44,0x80118EA4)`, `0x60` bytes. The
entry opens a 24-byte frame and saves `ra`; both the successful store path and
the error path join the epilogue at `0x80118E94`, return at `0x80118E9C`, and
restore the frame in the delay slot at `0x80118EA0`. The next independent owner
begins at `0x80118EA4` with a new 56-byte frame.

Generated entries `0x80118E54`, `0x80118E60`, `0x80118E64`, `0x80118E8C`, and
`0x80118E94` are internal branches or the post-`Exit` continuation. No direct
caller targets any interior address.

For `arena_index >= 3`, the function calls confirmed BIOS `Exit(-1)` at
`0x8015FD7C`. Under the BIOS contract that call does not return; the shared
epilogue is reachable only if a replacement BIOS violates the contract.

## Descriptor and validation semantics

The descriptor array begins at `0x801D38B8`, and each element is 12 bytes:

```c
struct BumpArena {
    uint8_t *base;       // +0
    uint32_t capacity;   // +4
    uint32_t cursor;     // +8
};
```

The selector check is signed `arena_index < 3`, not an unsigned range check.
Consequently indices 0, 1, and 2 select the intended descriptors, indices 3
and above terminate, but negative indices pass and address memory before the
array. All retained callers pass valid constants 0..2, so this latent missing
lower-bound check is not exercised.

The capacity is rounded down with `capacity & 0xFFFFFFFC`; it is not rounded up
and is not compared with the base. The routine performs exactly two ordinary
stores on success. In particular it does not reset `cursor`, allocate memory,
write an allocation header, or return a pointer.

## Caller census

There are exactly six unique direct entry calls:

```text
0x80115E28
0x80115E38
0x80115E50
0x80116068
0x80116078
0x80116088
```

They form two groups of three arena configurations in resource-loading code.
The callers select indices 0, 1, and 2 and provide region bases plus capacities
such as 30000, `0x28000`, and `0xF000` or values derived from region bounds.
There are no overlay duplicates or data-derived false callers.

This identity is corroborated independently by
`AllocateFromThreeBumpArenas` at `0x80117FD0`, which reads the same three
base/capacity/cursor descriptors and first-fits allocations across them.
