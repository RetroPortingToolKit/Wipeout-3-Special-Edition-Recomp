# Semantic audit: `0x80164AC8`

## Result

`0x80164AC8` is the canonical LibCD `CdPosToInt` conversion leaf. It decodes a
packed-BCD minute/second/frame location and converts it to the signed logical
sector index measured after the standard 150-frame CD lead-in.

## Boundary and ABI

The generated manifest assigns exact owner range `0x80164AC8..0x80164B47`
(0x80 bytes). It is one straight-line 32-instruction block, returning with
`jr $ra` at `0x80164B40` and completing the arithmetic in the delay slot at
`0x80164B44`. The next owner begins independently at `0x80164B54`; bytes
`0x80164B48..0x80164B53` are outside this function.

The canonical ABI is:

```c
int CdPosToInt(const CdlLOC *position);
```

Only `$a0` is consumed. The function reads the first three bytes of `position`
and returns a signed integer in `$v0`. It has no stack frame, calls, branches,
or interior entry targets.

## Exact conversion

For each input byte `x`, the repeated nibble arithmetic computes:

```c
decimal(x) = 10 * (x >> 4) + (x & 0x0F);
```

With the three bytes interpreted as packed-BCD minute, second, and frame, the
remaining shifts/adds implement exactly:

```c
minute = decimal(position->minute);
second = decimal(position->second);
frame  = decimal(position->sector);
return ((minute * 60) + second) * 75 + frame - 150;
```

The `-150` maps absolute CD MSF time past the two-second lead-in to logical
sector zero. Inputs are not validated as legal BCD or constrained MSF values,
so malformed nibbles are still evaluated by the same formula and early
positions may produce a negative result.

The routine performs no stores and touches no LibCD globals, callbacks, CD
registers, MMIO, COP0, or GTE state.

## Caller and target census

There are exactly seven generated instances and seven unique executable `jal`
sites, with no overlay duplication:

`0x80118350`, `0x80118BD4`, `0x80118EF4`, `0x80137F40`, `0x801605E8`,
`0x801609EC`, and `0x80164944`.

All target the true entry `0x80164AC8`. The body is a single basic block with
no interior target, and there are no data-derived false callers.
