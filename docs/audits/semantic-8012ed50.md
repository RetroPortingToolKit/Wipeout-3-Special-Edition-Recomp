# Semantic audit: `0x8012ED50`

## Result

`0x8012ED50` is the true leaf now named `FindNearestTrackSection`:

```c
TrackSection *FindNearestTrackSection(TrackSection *seed,
                                      int32_t x,
                                      int32_t y,
                                      int32_t z);
```

It searches a bounded window of the linked track-section graph, compares 3D
section positions with the supplied point, optionally explores a connected
branch, and returns the closest section pointer.

## Exact boundary, leaf, and return

The owner is exactly `[0x8012ED50, 0x8012EFB0)`, `0x260` bytes, matching the
generated range. It reserves 16 stack bytes but saves no return address because
it makes no calls. The only return is `jr ra` at `0x8012EFA8`, with the stack
restore in its delay slot. The independent next callable starts at
`0x8012EFB0` with its own frame setup.

There are no generated aliases inside the owner and an exhaustive JAL scan
finds no direct targets in its interior. `v0` is maintained as the current
best section pointer and returned on every path. The seed and linked-node
invariants imply a nonnull result; the function performs no null checks.

## Section layout and ABI evidence

Callers consistently pass a previously cached section pointer in `a0` and
object/world coordinates in `a1/a2/a3`; most immediately store the returned
pointer back into that cache. The fields used from each section are:

- `+0x04`: signed identifier/state (`-1` means no valid branch identity);
- `+0x08` and `+0x0C`: the two linked traversal directions;
- `+0x10/+0x14/+0x18`: signed X/Y/Z position;
- `+0x3A`: branch/direction flag byte (bits `0x04`, `0x08`, and `0x10`).

Several callers write an additional word to their outgoing stack at `sp+0x10`
before the call, but this owner never reads the fifth argument slot; its ABI is
four arguments only.

## Primary search

The mode word at `0x801D1B80` selects the search horizon:

```text
normal modes: follow +0x08 twice, test that node plus 5 successors via +0x0C
mode 1:       follow +0x08 eight times, test that node plus 20 successors
```

The post-skip node is the initial best candidate. For each candidate it
computes:

```c
dx = (x - node->x) >> 4;
dy = (y - node->y) >> 4;
dz = (z - node->z) >> 4;
distance = dx*dx + dy*dy + dz*dz;
```

Each multiply contributes the low 32 bits and additions are ordinary 32-bit
operations. Comparison uses signed `slt`; coordinate ranges are therefore a
caller invariant that keeps the squared sum in the nonnegative signed domain.
Strictly smaller distances replace the best pointer, so ties retain the first
candidate encountered.

During the scan, the last encountered node whose `+0x04` is not `-1` is saved
as a possible branch anchor. This selection is independent of which node is
currently closest.

## Optional branch search

If no valid branch anchor was encountered, the primary result is returned.
Otherwise the anchor and flags select a second bounded scan:

- normally start at the anchor itself;
- if anchor flag `0x08` is clear while the primary starting node's flag
  `0x10` is set, start at `anchor->link_0C`;
- after each comparison, follow `+0x08` when anchor flag `0x04` is set,
  otherwise follow `+0x0C`.

The same 5- or 20-node horizon and the same distance/tie rules are used. The
anchor's direction flag remains fixed throughout this pass. This is graph
branch handling, not an unbounded nearest-neighbor search.

The routine writes only its stack scratch slots and return register. It does
not modify track sections, the seed, the point, or global state; the mode word
is its sole global read.

## Direct callers

The retained EXE contains exactly twelve direct JALs to the entry:

```text
800E8EF0 800E9684 800E9AD0 800EA16C 800EF1B0 800F334C
800F9D18 8014CB84 80150998 801553C4 80158FF4 8015A5AC
```

Calls span vehicle/object movement, race effects, and scene objects. Repeated
patterns load coordinates from object offsets `+0x2C/+0x30/+0x34`, load the
cached section from `+0x08`, call this owner, and store `v0` back to `+0x08`.
That producer/consumer behavior proves the track-section result semantics.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instructions and exhaustive entry/interior JAL scanning;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- all twelve direct caller windows for argument and returned-pointer usage.
