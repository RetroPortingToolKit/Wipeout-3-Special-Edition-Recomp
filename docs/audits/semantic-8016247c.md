# Semantic audit: `0x8016247C`

## Result

`0x8016247C` is the game's non-returning CRT continuation.  The recommended
symbol is `PsxCrtStartup` with `status = "known"` and `emit = false`.
It is a genuine control-flow entry boundary, but it is not an ordinary o32
callable function: the expansion-RAM loader reaches it with `j`, it has no
prologue or return sequence, and it ultimately calls the non-returning race
session.

The generated range is exactly `0x8016247C..0x8016251B` (160 bytes).  Its five
blocks are the zero-fill loop, post-loop runtime setup, the return continuation
from the BIOS thunk, the call to `RunRaceSession`, and a terminal trap.

## Initialization responsibilities

The loader supplies `$v0 = 0x8017BEA0` on both of its join paths.  Startup
constructs the exclusive zero-fill limit `0x801EEBCC` and clears one word at a
time from `$v0` through `0x801EEBC8`.  This is the executable's BSS clear; the
same first address is reused after the loop as temporary storage for the
incoming `$ra`.

It then derives the runtime registers and heap description from linker-owned
words:

- load `0x80174940`, subtract eight, and map it into KSEG0 to establish `$sp`;
- load `0x80174944` and use it with the stack value and BSS limit to calculate
  the available heap length;
- publish the KSEG0 heap start `0x801EEBCC` and calculated length at globals
  `0x801793EC` and `0x801793F0`;
- set `$gp = 0x8017BE3C` and `$fp = $sp`, and save the incoming `$ra` at
  `0x8017BEA0`.

The delay slot at `0x80162500` advances the heap start by four before the call
to `0x80168D48`.  That four-instruction thunk loads `$t2 = 0xA0`, jumps to the
BIOS A0 vector, and loads `$t1 = 0x39` in its delay slot.  PSX BIOS A0 service
39 is `InitHeap(a0, a1)`, matching the two values computed immediately before
the call.  On return, startup reloads the preserved `$ra` and calls
`RunRaceSession` at `0x80116128`.

## Control-flow termination

`RunRaceSession` owns a closed frame loop and has no reachable epilogue in the
retained executable.  Its nominal return address is `0x80162518`, which is a
`break 1` instruction.  Thus `PsxCrtStartup` has no successful return path;
the trap prevents accidental continuation if the game loop unexpectedly
returns.

This also explains why the preserved incoming `$ra` does not make the range a
normal callable wrapper.  It is restored before the game entry call to retain
the platform startup convention, but no `jr $ra` exists in this range.

## Boundary and confidence

The boundary is supported independently by the loader's explicit jumps, the
generated `F 8016247C` range record, and the distinct two-instruction PS-X EXE
trampoline immediately before it.  Continuations at `0x80162484`,
`0x80162498`, `0x80162504`, and `0x80162518` remain internal blocks owned by
this range.

`known` is preferable to `confirmed`: the boundary, instructions, BIOS call,
and non-returning behavior are established, while `PsxCrtStartup` is a
recovered semantic name rather than an original debug symbol.  `emit = false`
prevents host code from treating this register-convention startup boundary as
a normal ABI call.

## Evidence

- `generated/SCES_028.45_full.ranges`: `F 8016247C`, `R 8016247C A0`.
- `generated/SCES_028.45_full_136.c`: all five startup blocks, heap setup,
  BIOS-thunk call, `RunRaceSession` call, and `break 1` return guard.
- `generated/SCES_028.45_full_138.c`: the A0/39 BIOS thunk at `0x80168D48`.
- `docs/audits/semantic-80162474.md`: both loader join paths and the preceding
  PS-X EXE entry boundary.
- `docs/audits/semantic-80116128.md`: sole startup caller and proof that
  `RunRaceSession` does not return in the retained graph.
