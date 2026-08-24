# Semantic audit: guest `0x801719DC`

## Result

`0x801719DC` is a genuine renderer-private callable spanning exactly
`0x801719DC..0x80171AD7` (`0xFC` bytes).  It is best described as
`CallSceneryClipperPreservingRendererContext`: it checkpoints the renderer's
nonstandard live register context, calls `ClipAndEmitSceneryPrimitive` at
`0x80142634`, then restores that context before returning.

## Boundary and callers

The entry is the target of four direct `jal` instructions in the immediately
preceding renderer at `0x80170E04`:

- `0x80171708`, returning to `0x80171710`;
- `0x801717D8`, returning to `0x801717E0`;
- `0x801718A0`, returning to `0x801718A8`;
- `0x80171978`, returning to `0x80171980`.

There are no other emitted direct callers.  Each site has just finished
marshalling another polygon candidate and resumes by decrementing the
caller's primitive loop count in `v0`.  Entry does not allocate an o32 stack
frame: `fp` already names scratchpad `0x1F800000`, and the routine deliberately
uses that shared renderer context.  Its own `jr ra` is at `0x80171AD0`, with a
NOP delay slot at `0x80171AD4`.  The distinct tail-entry shim at `0x80171AD8`
starts the next generated range, proving the `0xFC`-byte endpoint.

## Register and memory contract

Before the nested call, the routine stores the following incoming registers
relative to scratchpad `fp`:

- `at`, `ra`, `t0..t7`, `t8`, `t9`, and `k1` at `+0xBC..+0xEC`;
- `v0`, `v1`, `a0..a3` at `+0xF0..+0x104`;
- `s1..s7` at `+0x10C..+0x124` (matching the original hand-coded context
  layout).

`s0` is handled separately: it is written to global `0x80198620`, the active
GPU packet/primitive cursor.  The routine then reloads `gp` from `[sp+0]` and
`fp` from `[sp+4]` before `jal 0x80142634`.  This reconstructs the conventional
ABI context required by the C-style clipper while leaving the renderer's
scratchpad state recoverable.

On return, it reloads `s0` from `0x80198620`, reestablishes
`fp = 0x1F800000`, and restores every scratchpad-saved register, including
the caller's original `v0`, `v1`, argument registers, and `ra`.  Consequently:

- it has no meaningful o32 input arguments of its own;
- all polygon input is already in the global clipping workspace populated by
  the caller;
- updates to the packet cursor at `0x80198620` survive;
- the scalar value returned by `ClipAndEmitSceneryPrimitive` is intentionally
  discarded, because the original `v0` is restored.

The helper does not preserve `k0`, and it does not need to save `sp`, `gp`, or
`fp`: `sp` remains live, `gp` follows the nested ABI, and `fp` is explicitly
switched between conventional and scratchpad contexts.

## Why this is not the clipper itself

No projection, clipping, packet selection, or ordering-table insertion occurs
inside this `0xFC`-byte range.  Those operations belong to the sole callee
`ClipAndEmitSceneryPrimitive` (`0x80142634..0x80144687`).  This wrapper exists
because the parent renderer keeps long-lived values in registers that the
normal ABI permits the large clipper to overwrite.  Naming it as a clipper or
packet emitter would therefore conflate the context bridge with the work it
protects.

## Evidence

- `generated/SCES_028.45_full.ranges`: exact `R 801719DC FC` extent and next
  root at `0x80171AD8`.
- `generated/SCES_028.45_full_140.c`: all four direct call sites, the complete
  scratchpad save/restore sequence, the sole nested call, and the return.
- `docs/audits/semantic-80142634.md`: independently establishes the nested
  clipper's boundary, workspace contract, and packet-cursor behavior.
