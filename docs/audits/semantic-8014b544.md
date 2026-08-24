# Semantic audit: `0x8014B544`

## Result

`0x8014B544` is `ComputeCrossProductShift9`, a fixed-point vector leaf which
writes a three-component cross product after an arithmetic nine-bit scale-down.

## Boundary and ABI

The function is frameless and unsplit.  It begins by loading the final stack
operand at `0x8014B544`, returns with `jr ra` at `0x8014B5D0`, and stores the Z
component in the delay slot at `0x8014B5D4`.  The next independent owner begins
at `0x8014B5D8`, proving exact extent `[0x8014B544,0x8014B5D8)`, `0x94` bytes.

The recovered scalar ABI is:

```c
void ComputeCrossProductShift9(
    int32_t out[3],
    int32_t ax, int32_t ay, int32_t az,
    int32_t unused,
    int32_t bx, int32_t by, int32_t bz);
```

`out` is `a0`; vector A is `a1/a2/a3`; vector B is read from incoming stack
`+0x14/+0x18/+0x1C`.  Stack argument `+0x10` is never read.  The routine spills
`a1..a3` to the caller argument-save area at `sp+4/+8/+0x0C`, without creating
or retaining a frame.

## Exact arithmetic and stores

The three outputs are:

```c
out[0] = wrap32(ay*bz - az*by) >> 9;
out[1] = wrap32(az*bx - ax*bz) >> 9;
out[2] = wrap32(ax*by - ay*bx) >> 9;
```

Each signed MIPS `mult` contributes only its low 32-bit `LO` half.  `subu`
wraps each difference modulo `2^32`; `sra 9` then interprets that wrapped word
as signed and rounds negative values toward negative infinity.  This ordering
is significant: it is not a full-precision 64-bit cross product followed by a
shift, and there is no saturation or overflow trap.

The last shifted component remains in `v0`, but the sole caller overwrites
`v0` immediately and uses the three output words.  The evidence therefore
supports a void vector-writer contract; the residual scalar is incidental.
Apart from output and ABI home-area stores, there are no memory effects, calls,
globals, or hardware accesses.

## Caller and overlay reconciliation

There is exactly one unique executable guest call, at `0x800EC278` inside
owner `0x800EBDF0`.  It passes a stack-local three-word destination and scalar
components assembled from its geometry workspace, then consumes the stored
vector rather than the residual `v0`.

Generated output contains two instances of the same guest call because owner
`0x800EBDF0` is present in two mutually exclusive overlay variants.  The raw
image contains exactly one aligned JAL opcode to this entry, with no
opcode-shaped data false positive.  No external J or JAL targets an interior
instruction.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  formula, sole caller, and post-call scalar liveness;
- `generated/SCES_028.45_full.ranges` for the exact unsplit extent;
- generated overlay instances for reconciling two emitted calls to one guest
  address.
