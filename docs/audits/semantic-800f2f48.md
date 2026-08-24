# Semantic audit: `0x800F2F48` / `TransformIndexedPairedVertex`

## Result

`0x800F2F48` is the packed-pair vertex selector and GTE transformer now named
`TransformIndexedPairedVertex`:

```c
uint32_t TransformIndexedPairedVertex(VECTOR *destination,
                                      const VertexPairOwner *owner,
                                      int32_t vertex_index);
```

It selects one endpoint from a packed array of 12-byte vertex-pair records,
reconstructs a local `SVECTOR`, transforms/translates it through the current
GTE matrix state, writes the resulting `VECTOR`, and returns the GTE FLAG word.

## Exact owner and ABI

The complete owner is `[0x800F2F48,0x800F2FF4)`, exactly `0xAC` bytes, matching
the generated range table. It opens a 0x28-byte frame, saves `ra`, and uses
stack `+0x10..+0x16` for the local input vector plus `+0x18` for the GTE flag.
The sole exit restores `ra` and returns at `0x800F2FE4..0x800F2FF0`.

The preceding owner returns at `0x800F2F40` with its stack-restore delay slot
at `0x800F2F44`. The next independent callable creates a 0xC8-byte frame at
`0x800F2FF4`. There is no semantic continuation across either boundary.

The ABI is:

- `a0`: destination `VECTOR *`, three signed 32-bit output words;
- `a1`: owner whose word at `+0x18` points to packed vertex pairs;
- `a2`: signed vertex index (used arithmetically, without bounds checking);
- `v0`: the GTE FLAG value propagated unchanged from `0x8015FB54`.

## Packed index and component formula

The record pointer is computed exactly as:

```c
uint8_t *record = owner->vertex_pairs + (vertex_index >> 1) * 12;
```

The shift is arithmetic. Valid callers provide nonnegative indices, but the
implementation itself does not reject negative or out-of-range values.

Parity selects one of two packed endpoints:

```text
even index: local.x = *(int16_t *)(record + 0x00)
            local.y = *(int16_t *)(record + 0x02)
            local.z = *(int16_t *)(record + 0x04)

odd index:  local.x = *(int16_t *)(record + 0x08)
            local.y = *(int16_t *)(record + 0x0A)
            local.z = *(int16_t *)(record + 0x06)
```

Loads use `lhu` and stores use `sh`, preserving each 16-bit pattern exactly;
the downstream GTE interprets the local fields as signed `SVECTOR` components.
The unusual odd-endpoint Z placement at `+0x06` is instruction-proven and must
not be normalized into a naïve pair of consecutive six-byte structures.

## GTE transform and return

The helper calls `0x8015FB54` as:

```c
uint32_t flag = RotTransLike(&local, destination, &stack_flag);
return flag;
```

That leaf loads the local `SVECTOR` into GTE V0, executes MVMVA with the
current rotation/translation control state, stores IR1/IR2/IR3 as three
32-bit words at destination `+0/+4/+8`, reads GTE FLAG, stores FLAG through
the third argument, and also returns FLAG in `v0`. This owner does not modify
`v0` after the call, so the return is propagated exactly even though retained
callers use the destination vector rather than branching on the flag.

No matrix is installed here. Callers establish GTE rotation and translation
before invoking this owner, often transform several indices consecutively,
then consume the resulting stack vectors.

## Unique calls, generated overlap, and interior targets

An exhaustive aligned instruction-word scan of the retained PS-X EXE finds
exactly seven unique executable JAL sites:

```text
800EBF00 800EBF10 800EBF20 800EBF30
800EC194 800EC1A4 800EC1B4
```

The first group transforms indices `0,1,2,3` into four consecutive 16-byte
stack vectors. The second group supplies three runtime byte indices and writes
three consecutive stack vectors.

Generated translation contains ten emitted call occurrences: the four first-
group calls once each, while overlapping/overlay translations repeat the
three second-group call PCs. Those repetitions are not additional executable
instructions or semantic callers; the unique census remains seven.

All unique calls target the true entry. The only direct control transfer whose
target lies inside the owner is its own `j 0x800F2FD4` at `0x800F2F90`, joining
the even path to the shared GTE-call block. No external `j` or `jal` targets an
interior address.

## Effects and semantic limits

The function writes destination words `+0/+4/+8` through the GTE leaf and uses
only its own stack otherwise. Its remaining effects are the transient GTE data
register/FLAG activity inherent in the transform. It does not modify the
source pair table, owner, installed matrix controls, global pointers, heap, or
GPU packet state.

The packed endpoint layout, current-GTE transform, output vector, and FLAG
return are exact. The higher-level asset type owning the pair table is not
named because the caller evidence does not require choosing between all model,
vehicle, or effect resource formats.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, unique callers, arguments, and aligned interior targets;
- `generated/SCES_028.45_full.ranges` for the exact `0xAC` boundary;
- `generated/SCES_028.45_full_17.c` for duplicated emitted caller instances;
- `generated/SCES_028.45_full_19.c` for the single owner translation;
- `generated/SCES_028.45_full_52.c` for the exact GTE operation, destination
  stores, FLAG store, and propagated return of `0x8015FB54`.
