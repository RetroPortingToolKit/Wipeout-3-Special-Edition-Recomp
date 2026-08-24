# Semantic audit: `0x8014F920`

## Result

`0x8014F920` is `AccumulateIndexedVertexWeights`, a game-side leaf which
converts an object's small positive weight into fixed additions on
three indexed geometry accumulator words, consuming the object's weight
byte to zero.

The conservative name records the exact indexed accumulator behavior; the
source-original visual/gameplay meaning of those weights is not yet proven.

## Boundary and ABI

The routine is a frameless leaf beginning with `move t0,a0` at `0x8014F920`.
All validation and update paths return through `jr ra` at `0x8014FA44` with a
NOP delay slot at `0x8014FA48`.  The exact interval is therefore
`[0x8014F920,0x8014FA4C)`, `0x12C` bytes, matching the generated range.

The recovered ABI is:

```c
void AccumulateIndexedVertexWeights(void *object);
```

Only `a0` is read.  No path deliberately establishes `v0`, so the routine has
no semantic return value.

## Validation and record selection

The function returns immediately when `object == NULL` or signed byte
`object[0x3F] <= 0`.  Consequently only authored weights `1..127` enter the
update path; raw values `0x80..0xFF` are rejected by the signed test.

Halfword `object+0x34` is the starting track-record index.  The active context
pointer at `0x801985D4` supplies:

- `context+0x10`: a 24-byte geometry-record array;
- `context+0x0C`: a 12-byte indexed vertex/state array.

Starting at `records + index*24`, the function advances by 24 bytes until
record byte `+0x0F` has bit 0 set.  There is no local array bound, so the
presence of a flagged record at or after the starting index is a data-format
invariant.

## Accumulator behavior

For each unit of the unsigned weight byte, the selected record supplies three
signed-halfword indices:

```text
record +0x00       primary index
record +0x06       second index
(record +0x18)+0x00 third index (the next record's leading index)
```

Each selects `vertices + index*12`; the target is word `+0x04`.  One unit adds:

```text
primary accumulator += 200, then += 50   (net +250)
second accumulator  += 200
third accumulator   += 200
```

The record pointer and indices remain fixed across iterations.  After each
unit, byte `object+0x3F` is decremented and written back.  Iteration continues
while its signed low byte remains positive, so every admitted `1..127` weight
is consumed in the same call.  Arithmetic is ordinary wrapping 32-bit MIPS
addition; there is no saturation or overflow check.

This routine calls no helper and writes only the three accumulator words plus
the object's consumed weight byte.  It does not directly submit geometry or
touch GPU registers.

## Direct callers and interior-target scan

An exhaustive aligned scan of the retained EXE finds exactly nine JAL sites:

```text
801511E8 80151228 80151264 801515BC 801515D4
80151624 80151644 80151684 8015B594
```

The first eight belong to owner `0x801508CC`; the last belongs to
`0x8015B564`.  Callers pass direct objects and objects reached through linked
fields `+0x08`, `+0x0C`, and `+0x14`; two loops additionally filter object
flag byte `+0x3A` bits 2 or 3 before applying the accumulator.  No external J
or JAL targets an interior instruction.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  exhaustive caller/interior-target scans, and caller argument provenance;
- `generated/SCES_028.45_full.ranges` and the generated owner;
- `docs/audits/semantic-801701b4.md` for the independently recovered active
  track/geometry context rooted at `0x801985D4`.
