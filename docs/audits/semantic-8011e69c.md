# Semantic audit: `0x8011E69C` / `TrySpawnRandomGeometryParticle`

## Result and ABI

`0x8011E69C` conditionally allocates and initializes one randomized type-3
record in the renderer's 48-byte effect pool.  The conservative descriptive
name is `TrySpawnRandomGeometryParticle`.

```c
void TrySpawnRandomGeometryParticle(void);
```

The routine never reads an argument register.  Its sole caller happens to clear
`a0` in the JAL delay slot, but that value is not consumed.  All exit paths
leave an incidental intermediate in `v0`; the caller does not inspect it, so
there is no semantic return value.

## Boundary and incoming targets

The generated range table assigns `[0x8011E69C, 0x8011E964)`, exactly `0x2C8`
bytes, to this owner.  The first two instructions load a global before the
stack-frame allocation at `0x8011E6A4`; they are part of the function.  The
single epilogue restores `ra` and `s0..s4`, returns at `0x8011E95C`, and
restores `sp` in the delay slot at `0x8011E960`.  The prior owner ends at
`0x8011E69C`, and the next begins exactly at `0x8011E964`.

An aligned scan of the authenticated executable finds exactly one direct JAL,
at `0x8011EE70`, inside owner `0x8011EE24`.  There are no direct JAL targets in
the interior `[0x8011E6A0, 0x8011E964)`.  The caller invokes this helper only
when global `0x8019AE94` is zero, immediately before its effect-pool update and
render loop.

## Admission and allocation

The helper returns without mutation when byte `0x801D1B3E` equals `1`.  In all
other byte states it calls `NextRandom15`; only `(random & 15) == 0` passes, a
one-in-sixteen admission gate for the 15-bit generator.

It then pops an index, preferring the stack described by count `0x80175D34`
and index-array pointer `0x801D4CF8`.  If that count is zero, it uses count
`0x80175D38` and pointer `0x801D4B10`.  Each successful path decrements its
count before reading the last four-byte index.  If both counts are zero, the
routine returns.  The selected record is
`*(void **)0x801D4B14 + index * 48`; no caller-supplied storage is involved.

## Geometry and record initialization

The current source/owner record is `*(void **)0x801D4B0C`.  Its signed
halfwords at `+0x34` and `+0x36` select 24-byte geometry entries reachable
from the active render object at `*(void **)0x801985D4`.  A signed halfword in
the selected geometry entry indexes the active object's 12-byte XYZ table.
That vertex supplies the spawn center:

- X and Z each receive an independent `(NextRandom15() & 0x7FF) - 0x400`
  displacement;
- Y is copied unchanged;
- record bytes `+0x28/+0x29` are set to `3/1`;
- halfwords `+0x10/+0x12/+0x14` become `(0, -0x1E0, 0)`;
- halfwords `+0x18` and `+0x1C` independently become
  `((NextRandom15() & 7) - 3) << 7`, while `+0x1A` becomes `0x0C00`;
- bytes `+0x2A..+0x2C` are independent sums of three random low bits, each in
  `[0,3]`;
- word `+0x24` is `40`, and word `+0x20` retains the source/owner pointer.

There are fourteen RNG calls on a successful spawn, including the admission
call.  The helper emits no GPU packet itself.  Instead it publishes an active
record in the same pool that caller `0x8011EE24` immediately iterates and
renders, which is why this is classified as renderer/effect state rather than
a generic allocator.  The exact visual identity of type 3 is not asserted by
the symbol.

## Confidence

`confirmed`: exact owner range, pre-prologue inclusion, no-argument/no-return
ABI, exhaustive direct entry/interior-target scan, admission probability,
two-stack allocation order, 48-byte indexing, geometry lookup, every written
field, and sole-caller context are instruction-proven.
