# Semantic audit: `0x800ECB6C`

## Result

`0x800ECB6C` is `ResolveObjectTrackSurfaceCollision`, the central owner which
tests an actor/work vector against track-surface records associated with an
object and dispatches the appropriate collision response.

## Boundary, ABI, and return

The function allocates a `0x98`-byte frame at `0x800ECB6C`, saves every saved
register plus `fp`/`ra`, and returns through the common epilogue at
`0x800ED45C`.  `jr ra` is at `0x800ED484` and its delay slot restores `sp` at
`0x800ED488`.  The next independent owner starts at `0x800ED48C`, proving the
exact interval `[0x800ECB6C,0x800ED48C)`, `0x920` bytes.

The four-register ABI is conservatively recovered as:

```c
int ResolveObjectTrackSurfaceCollision(
    void *actor, void *motion_or_position, void *track_object, int force_mode);
```

All seven retained callers pass zero for `force_mode`; mode 1 is nevertheless
an explicit live path in the owner.  Return values form a tri-state contract:

- `-1`: candidate rejected by the optional range gate before collision work;
- `0`: processed candidates produced no handled collision;
- `1`: a collision response was selected/handled.

## Geometry selection and tests

`track_object+0x34` supplies a signed starting index into the 24-byte record
array at `[0x801985D4]+0x10`.  As in the related indexed-weight owner, records
without byte `+0x0F` bit 0 are skipped.  The scan count is retained so the
routine can step backward through earlier candidates if neither of the two
adjacent face tests handles a collision.  No local record-array bound exists;
a flagged record is a data invariant.

When byte `0x8018AE3E` is nonzero, the first indexed 12-byte vertex accumulator
from `[0x801985D4]+0x0C` is compared with `motion_or_position+0x04`; a delta of
1501 or more returns `-1`.  Otherwise the routine loads indexed vertex/normal
data from the selected and adjacent records, performs fixed-point dot products
and GTE vector operations, and computes signed plane-intersection ratios for
two neighboring surface candidates.  Positive ratios reject that face;
nonpositive ratios proceed to containment/response handling.

The descriptor byte at `[0x801D1B7C]+0x05` is classified by the only external
game helper `0x801025E4`, which returns true exactly for values 16, 19, or 22.
That classification participates in the fixed-point setup but performs no
mutation itself.

## Mode and response dispatch

For the first candidate, normal `force_mode != 1` calls `0x800EAA04` with the
surface record, computed point/vector workspace, signed intersection ratio,
and candidate selector.  Only predicate result 1 dispatches response helper
`0x800EB158`.  `force_mode == 1` bypasses that predicate gate and dispatches
`0x800EB158` directly.

The adjacent candidate again requires `0x800EAA04 == 1`.  Response selection
then depends on state proven in this owner:

- if `track_object+0x3A` has either bit 2 or bit 3, call `0x800EAF60` and mark
  the collision handled;
- otherwise, if the first candidate already established the local hit flag,
  call `0x800EAC7C`;
- otherwise call `0x800EAF60`.

The owner itself primarily builds stack workspaces and selects records.  Its
observable actor/motion mutations are delegated to `0x800EB158`, `0x800EAC7C`,
and `0x800EAF60`; their full source-level response distinctions remain to be
audited, so the symbol does not claim a narrower wall/floor/weapon identity.

## Calls, callers, and interior targets

The eight static calls inside the owner are:

```text
800ECE8C -> 801025E4
800ED184 -> 800EAA04
800ED1A0 -> 800EB158
800ED1BC -> 800EB158
800ED33C -> 800EAA04
800ED39C -> 800EAF60
800ED3E8 -> 800EAC7C
800ED424 -> 800EAF60
```

An exhaustive aligned EXE scan finds exactly seven direct calls to the entry:

```text
800ED7A4 800ED7C8 800ED7E8 800ED804 800ED830 800ED85C 800ED880
```

All seven belong to owner `0x800ED48C`, which tries a primary object and linked
objects reached through `+0x04`, `+0x08`, and `+0x0C`, stopping when this owner
returns 1.  No external J or JAL targets an interior instruction, so there is
no callable alias hidden inside the `0x920` range.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  call graph, callers, argument values, and external interior-target scan;
- `generated/SCES_028.45_full.ranges` and the generated owner;
- `docs/audits/semantic-801701b4.md` and `semantic-8014f920.md` for the
  independently recovered active geometry context and 24-/12-byte layouts.
