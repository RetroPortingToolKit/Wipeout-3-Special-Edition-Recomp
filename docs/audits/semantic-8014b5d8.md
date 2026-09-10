# Semantic audit: `0x8014B5D8`

## Result

`0x8014B5D8` is `SubtractVector3Int32`, a scalarized three-component vector
subtraction leaf.  Its operand order is second vector minus first vector.

## Boundary and ABI

The function is frameless and unsplit.  It begins by loading incoming stack
argument five at `0x8014B5D8`, returns with `jr ra` at `0x8014B60C`, and stores
the Z result in the delay slot at `0x8014B610`.  The next independent owner
begins at `0x8014B614`, proving exact extent
`[0x8014B5D8,0x8014B614)`, `0x3C` bytes.

The recovered scalar ABI is:

```c
void SubtractVector3Int32(
    int32_t ax, int32_t ay, int32_t az, int32_t unused0,
    int32_t bx, int32_t by, int32_t bz, int32_t unused1,
    int32_t out[3]);
```

Vector A arrives in `a0/a1/a2`; vector B is at incoming stack
`+0x10/+0x14/+0x18`; destination is the pointer at `sp+0x20`.  `a3` and stack
argument `sp+0x1C` are unused.  The compiler spills `a0..a3` into the caller's
argument-save area at `sp+0..0x0C` but allocates no frame.

## Exact arithmetic and effects

The stores are exactly:

```c
out[0] = bx - ax;
out[1] = by - ay;
out[2] = bz - az;
```

All subtraction uses nontrapping MIPS `subu`, so every component wraps modulo
`2^32`; there is no scaling, saturation, or overflow exception.  The final Z
difference remains in `v0` after the delay-slot store, but callers use the
destination vector and do not establish a scalar API contract from it.

Apart from the three destination stores and ABI home-area spills, the routine
has no memory effects.  It calls no helper and touches no global or hardware
state.

## Executable callers and overlay reconciliation

There are exactly nine unique guest call sites, all within owner `0x800EBDF0`:

```text
800EBF6C 800EBFA8 800EBFE4 800EC020 800EC05C
800EC098 800EC200 800EC23C 800EC424
```

Generated output contains 12 call instances because guest sites
`0x800EC200`, `0x800EC23C`, and `0x800EC424` occur in two mutually exclusive
overlay variants.  Deduplication by guest PC yields nine.  The raw image has
exactly those nine aligned JAL opcodes, with no opcode-shaped data false
positive.  No external J or JAL enters the function interior.

The caller uses these differences as geometry work vectors and feeds one of
them into adjacent `ComputeCrossProductShift9`; that relationship confirms the
vector, rather than arbitrary three-word, interpretation.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS and
  the nine unique call sites;
- `generated/SCES_028.45_full.ranges` for the exact unsplit boundary;
- generated overlay instances for the 12-instance/9-PC reconciliation;
- `docs/audits/semantic-8014b544.md` for the adjacent consumer relationship.
