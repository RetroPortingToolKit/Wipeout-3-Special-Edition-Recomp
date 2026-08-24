# Semantic audit: `0x8014F814`

## Result

`0x8014F814` is the one-unit companion of the independently recovered
`AccumulateIndexedVertexWeights` at `0x8014F920`. It is now named
`AccumulateOneIndexedVertexWeight`:

```c
void AccumulateOneIndexedVertexWeight(void *object);
```

Given an object with a positive pending-weight byte, it locates the applicable
geometry record, applies one fixed set of additions to three indexed vertex
accumulator words, decrements the pending weight once, and returns.

## Exact boundary and leaf proof

The function is a frameless leaf at `[0x8014F814, 0x8014F920)`, exactly `0x10C`
bytes, matching generated range metadata. It makes no calls, has no stack
frame, and all paths return through `jr ra` at `0x8014F918` with a NOP delay
slot. `0x8014F920` begins an independent routine with its own entry validation.

Only `a0` is read. No path deliberately establishes `v0`, so there is no
semantic return value.

## Validation and record selection

The routine immediately returns if `object == NULL` or signed byte
`object+0x3F <= 0`. Thus only pending values 1..127 are admitted; raw bytes
`0x80..0xFF` fail the signed test.

Halfword `object+0x34` is used as an initial index. The active context pointer
at `0x801985D4` supplies:

- `context+0x10`: a 24-byte geometry-record array;
- `context+0x0C`: a 12-byte indexed vertex/accumulator array.

The routine starts at `records + index*24` and advances by 24 bytes until
record flag byte `+0x0F` has bit 0 set. There is no local bound or null check,
so a flagged record at or after the authored index is a data invariant.

## One-unit accumulator behavior

The selected record supplies these signed-halfword indices:

```text
record +0x00       primary index
record +0x06       second index
(record +0x18)+0x00 third index (the next record's leading index)
```

Each index selects `vertices + index*12`; word `+0x04` is updated. One call
performs exactly:

```text
primary accumulator += 200, then += 50   (net +250)
second accumulator  += 200
third accumulator   += 200
object[0x3F]        -= 1
```

The repeated primary load/store is literal: the function applies +200 and +50
as two separate additions. Arithmetic is wrapping 32-bit MIPS addition with
no saturation or overflow check. Apart from the object's byte and those three
accumulator words, no shared state is written.

The paired `0x8014F920` owner uses the same lookup, indices, and constants but
loops until the positive weight is exhausted. That exact structural match is
why the conservative name uses `One` rather than assigning an unproven visual
meaning such as height, deformation, or animation.

## Direct callers and interior targets

An exhaustive aligned scan of the loaded EXE finds exactly six JAL sites:

```text
80151044 80151074 80151094 801510F8 80151128 80151148
```

All six belong to owner `0x801508CC`. That owner walks linked neighboring
track/geometry objects through fields `+0x08` and `+0x0C`, sometimes filtering
flag byte `+0x3A` bit 2 or 3, and invokes this leaf only while byte `+0x3F` is
nonzero. The repeated calls spread one-unit updates across its outer loops.

There are no external direct `j` or `jal` targets inside
`0x8014F818..0x8014F91F`; the six calls all target the true entry.

## Sources checked

- `disc/SCES_028.45`, respecting the 0x800-byte PS-X EXE header, for exact
  instruction decoding and exhaustive direct/interior jump scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- `docs/audits/semantic-8014f920.md` for the independently recovered paired
  all-units operation and `docs/audits/semantic-801701b4.md` for the active
  geometry context rooted at `0x801985D4`;
- all six caller windows in owner `0x801508CC`.
