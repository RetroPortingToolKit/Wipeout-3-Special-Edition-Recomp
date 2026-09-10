# Semantic audit: `0x801649C4` / `CdIntToPos`

## Result and ABI

`0x801649C4` is the canonical LibCD integer-sector to disc-position converter:

```c
CdlLOC *CdIntToPos(int32_t sector, CdlLOC *position);
```

It adds the standard 150-sector/two-second lead-in, converts the resulting
sector count to minute/second/frame components, packs each component as BCD,
writes the first three output bytes, and returns the original output pointer.

## Exact owner and target census

The generated range assigns `[0x801649C4, 0x80164AC8)`, exactly `0x104` bytes,
to this frameless 65-instruction leaf.  The preceding owner ends at
`0x801649B8`; three NOPs at `0x801649B8..0x801649C0` are alignment padding and
not part of either callable.  `CdIntToPos` returns at `0x80164AC0`, performs its
final minute-byte store in the delay slot at `0x80164AC4`, and the next owner
starts exactly at `0x80164AC8`.

An aligned scan of the authenticated executable finds exactly seven direct
entry JALs:

```text
801188F4 80118A38 80118C54 80137F4C 801645A4 80164750 80164950
```

There are no direct JALs to an interior instruction in
`[0x801649C8, 0x80164AC8)`.  The function itself is call-free.

## Conversion formula

Using signed 32-bit arithmetic and division truncated toward zero, the leaf
implements:

```c
int32_t total   = sector + 150;
int32_t seconds_total = total / 75;
int32_t frames  = total - seconds_total * 75;
int32_t minutes = seconds_total / 60;
int32_t seconds = seconds_total - minutes * 60;

position[2] = ((frames  / 10) << 4) + (frames  % 10);
position[1] = ((seconds / 10) << 4) + (seconds % 10);
position[0] = ((minutes / 10) << 4) + (minutes % 10);
return position;
```

The divisions are compiled as fixed-multiplier/high-word sequences:
`0x1B4E81B5` for `/75`, `0x88888889` for `/60`, and `0x66666667` for `/10`,
with sign corrections after each high-word shift.  These constants and the
remainder subtractions prove the exact formula without relying only on caller
context.

## Output and edge behavior

The output byte layout is the standard `CdlLOC` minute, second, frame prefix:

| Offset | Value |
|---:|---|
| `+0` | packed-BCD minutes |
| `+1` | packed-BCD seconds |
| `+2` | packed-BCD frames |
| `+3` | untouched |

The result pointer is copied from incoming `a1` to `v0` before any arithmetic
and never changed.  Stores occur in reverse field order (frame, second,
minute), with the last store in the return delay slot.  There is no null check,
range clamp, saturation, or capacity argument.

Normal LibCD callers supply nonnegative logical sector indices, yielding
minutes `>=0`, seconds `0..59`, and frames `0..74`.  Negative input below
`-150`, signed addition overflow, or minute values beyond two decimal digits
are not rejected; signed quotient/remainder results are simply packed and
truncated to bytes.  Thus the routine should not be generalized to a validated
calendar/time formatter.

## Caller and state effects

Callers use stack, global, or record-owned `CdlLOC` outputs for CD read/play
positions and TOC-derived offsets.  Several immediately pass the three-byte
position to LibCD control/read helpers; others retain it in streaming state.
The returned pointer is available under the canonical API even where the
caller already retains the destination.

The leaf reads no RAM except its arguments through registers and writes only
`position[0..2]`.  It has no stack frame, global access, BIOS call, CD register
access, callback, interrupt effect, or synchronization behavior.

## Confidence

`confirmed`: exact range and external padding, ABI/return, exhaustive
caller/interior-target census, lead-in constant, signed division/remainder
formulas, BCD packing, output byte order and untouched fourth byte, and absence
of shared/hardware effects are instruction-proven.
