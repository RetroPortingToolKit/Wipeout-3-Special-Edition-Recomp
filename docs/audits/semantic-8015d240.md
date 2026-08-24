# Semantic audit: `0x8015D240`

## Result

`0x8015D240` is the PsyQ GPU synchronization entry `DrawSync`.  It optionally
emits the retained diagnostic and delegates the actual synchronization/query
operation to the active GPU driver's sync method.

## Boundary, ABI, and return

The owner begins with a driver-type load at `0x8015D240`, then allocates its
24-byte frame at `0x8015D248`.  It saves `s0` and `ra`, preserving the incoming
mode across an optional diagnostic call.  `jr ra` is at `0x8015D2A0` and its
delay slot restores `sp` at `0x8015D2A4`.  The next independent owner begins at
`0x8015D2A8`, proving `[0x8015D240,0x8015D2A8)`, `0x68` bytes.

The recovered ABI is the standard PsyQ form:

```c
int DrawSync(int mode);
```

The mode is passed unchanged to the active driver's method.  `v0` from that
method is returned unchanged; this adapter performs no result normalization.
The conventional mode-0 wait versus mode-1 query distinction is therefore
implemented by the selected driver method rather than branched on here.

## Diagnostic and driver dispatch

Unsigned byte `0x80178F4A` is the active GPU driver/type selector.  Values zero
or one skip diagnostics.  For values two or greater, the routine calls the
function pointer at `0x80178F44` with:

```c
diagnostic("DrawSync(%d)...\n", mode);
```

The format string is present verbatim at `0x8016F088`.  The routine then loads
the active driver object from `0x80178F40`, reads function pointer `+0x3C`, and
calls it with `a0 = mode`.  Neither function pointer is null-checked; installed
driver state is a library invariant.

`DrawSync` itself performs no global store and touches no GPU MMIO register.
Waiting for GP0/DMA completion, polling GPU status, or returning current busy
state belongs to the active driver's `+0x3C` method.  The wrapper's only state
effects are the optional diagnostic callback and that delegated synchronization
operation.

## Exhaustive callers and interior targets

An aligned EXE scan finds exactly 13 direct JAL sites:

```text
800EDD70 800EDD90 800EE274 800EE86C 800EE8B0 800EE954 800EE970
800EE994 800EE9B0 800EEB34 800EED1C 80116274 8011650C
```

Grouped by owner:

- `0x800ED9D8`: two calls;
- `0x800EE22C`: one call;
- `0x800EE818`: two calls;
- `0x800EE8C8`: four calls;
- `0x800EEAEC`: one call;
- confirmed `LoadImage` (`0x800EEC80`): one call;
- `RunRaceSession` (`0x80116128`): two calls.

No external J or JAL targets an interior instruction.  The leading type load
before frame creation is part of this callable owner, not a preceding tail.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact MIPS,
  diagnostic bytes, and exhaustive caller/interior scans;
- `generated/SCES_028.45_full.ranges` and the generated owner;
- `docs/audits/semantic-800eec80.md` for the independently recovered queued
  VRAM-upload path which waits through this synchronization entry.
