# Semantic audit: `0x8014B4F8`

## Result

`0x8014B4F8` is `DotProduct3Int32`, an effect-free three-component integer dot
product expressed through the o32 six-scalar calling convention.

## Boundary and ABI

The routine is a frameless leaf.  It begins by loading incoming stack argument
five at `0x8014B4F8`, returns with `jr ra` at `0x8014B53C`, and performs the
final addition in the delay slot at `0x8014B540`.  The next independent owner
begins at `0x8014B544`.  Its exact extent is therefore
`[0x8014B4F8,0x8014B544)`, `0x4C` bytes, exactly matching the generated range;
unlike the adjacent helpers at `0x8014B6A4/C0`, it has no split suffix.

The recovered scalar ABI is:

```c
int32_t DotProduct3Int32(
    int32_t ax, int32_t ay, int32_t az, int32_t unused,
    int32_t bx, int32_t by, int32_t bz);
```

`ax/ay/az` arrive in `a0/a1/a2`; `bx/by/bz` are stack arguments at incoming
`sp+0x10/+0x14/+0x18`.  `a3` is not read by the formula.  The compiler spills
`a0..a3` into the caller-provided argument-save area at `sp+0..0x0C`, but does
not allocate a frame or retain any pointer to those copies.

## Exact arithmetic

The return is exactly:

```c
(int32_t)((uint32_t)((int64_t)ax * bx) +
          (uint32_t)((int64_t)ay * by) +
          (uint32_t)((int64_t)az * bz));
```

Each MIPS `mult` produces a signed 64-bit product, but only `LO` is retained.
The three low halves are combined with nontrapping `addu`, so products and sums
wrap modulo `2^32`; no scaling shift, rounding, saturation, or overflow trap is
performed.  `v0` is the final low 32-bit dot product.

Apart from its ABI home-area spills, the leaf reads only its incoming stack
arguments.  It calls no helper and has no global, heap, or hardware effects.

## Caller reconciliation

There are exactly ten unique executable guest call addresses:

```text
800EC458 800EC490 8014AE40 8014AEFC 8014AFB8
8014B074 8014B1B0 8014B26C 8014B328 8014B428
```

Grouped by owner, these are two calls from `0x800EBDF0`, four from
`0x8014AD98`, three from `0x8014B124`, and one from `0x8014B3EC`.

Generated output contains 12 instances because the two guest sites at
`0x800EC458/0x800EC490` occur in two mutually exclusive overlay variants.
Deduplicating by guest PC yields the correct ten-call interface.  The raw image
contains exactly the same ten aligned JAL opcodes, with no opcode-shaped data
false positives.  No external J or JAL targets an interior instruction.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS and
  the ten unique JAL sites;
- `generated/SCES_028.45_full.ranges` for the exact unsplit boundary;
- generated overlay call instances for the 12-instance/10-PC reconciliation.
