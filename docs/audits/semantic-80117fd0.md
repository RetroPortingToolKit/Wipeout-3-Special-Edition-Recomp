# Semantic audit: `0x80117FD0` / `AllocateFromThreeBumpArenas`

## Result

`0x80117FD0` is the first-fit allocator for three fixed bump arenas:

```c
void *AllocateFromThreeBumpArenas(uint32_t payload_size);
```

It rounds the request to four-byte alignment, searches three arena
descriptors, prefixes the allocation with its rounded size, advances the
selected cursor, and returns the payload pointer. Exhaustion terminates via
the PlayStation BIOS `exit(-1)` adapter.

## Exact owner and ABI

The generated extent `[0x80117FD0,0x8011805C)` is exact, length `0x8C`. It
opens a 24-byte frame, saves only `ra`, and has one epilogue at
`0x8011804C..0x80118058`. `0x8011805C` begins a separate `0x298`-byte framed
owner. Internal dispatch entries are loop/branch continuations, and no
external direct jump or call targets them.

- `a0` is an unsigned requested payload byte count.
- `v0` is a four-byte-aligned payload pointer on the returning path.

There is no free operation, zero-size special case, maximum-size guard, or
integer-overflow check in this routine. A zero-size request still consumes its
four-byte allocation header and returns the following address.

## Descriptor layout and allocation formula

The three 12-byte descriptors begin at `0x801D38B8`:

```c
struct BumpArena {
    uint8_t *base;       // +0
    uint32_t capacity;   // +4, maximum cursor/end offset
    uint32_t cursor;     // +8
};
```

The exact operation is equivalent to:

```c
uint32_t size = (payload_size + 3) & ~3u;

for (unsigned i = 0; i != 3; ++i) {
    BumpArena *arena = &g_load_arenas[i];
    uint32_t old = arena->cursor;
    if (old + size + 4 <= arena->capacity) {
        uint8_t *header = arena->base + old;
        arena->cursor = old + size + 4;
        *(uint32_t *)header = size;
        return header + 4;
    }
}

exit(-1);
```

The comparison is unsigned. Arenas are tried in ascending descriptor order,
so this is first-fit across the three independent bump regions. Only the
selected descriptor's cursor and the new allocation header are written.

The exhaustion call at `0x80118044` targets `0x8015FD7C` with `a0 = -1`.
Independent BIOS-thunk evidence identifies selector `B0/38` as `exit`; under
that contract it does not return. The following epilogue is shared with the
successful path and would expose an incidental BIOS result only if a
replacement BIOS violated that contract.

## Complete caller and interior-target census

An exhaustive aligned scan finds exactly six direct calls:

```text
80118604 80118660 80118694 801186C8 80118730 8011878C
```

All six are within the single resource-construction owner beginning at
`0x80118588`. That caller first clears all three arena cursors, then allocates
a `0x6C`-byte top-level record, a `0x10000`-byte block, two count-scaled
arrays, and up to two blocks sized from the largest remaining arena capacity.
It immediately stores and initializes every returned pointer and never checks
for null, consistent with fatal exhaustion.

The only direct interior transfer is the allocator's own jump from
`0x80118030` to its epilogue at `0x8011804C`. No external JAL or jump targets
any interior address. `AllocateFromThreeBumpArenas` is an exact behavioral
name; the loader-specific purpose of the arena contents is not narrowed
beyond the sole caller evidence.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` PS-X EXE header, for exact
  instructions, descriptor arithmetic, all caller windows, and exhaustive
  entry/interior target scans;
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_full_30.c` for the exact range and complete flow;
- `docs/audits/semantic-800f82ec.md` for the independently identified BIOS
  `exit` thunk at `0x8015FD7C`.
