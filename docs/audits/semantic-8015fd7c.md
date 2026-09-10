# Semantic audit: `0x8015FD7C` / `Exit`

## Result and canonical ABI

`0x8015FD7C` is the canonical PlayStation BIOS `B0:38` exit trampoline,
cataloged as `Exit` consistently with this project's capitalized BIOS export
names:

```c
_Noreturn void Exit(int status);
```

The stub preserves `a0` and the game's caller `ra`, loads BIOS vector `B0`
into `t2`, and selects service `0x38` in the vector-jump delay slot. Under the
BIOS contract the service terminates the program and does not return.

## Exact owner and targets

The generated range `[0x8015FD7C,0x8015FD8C)` is exactly `0x10` bytes:

```text
8015FD7C  addiu  t2,zero,0xB0
8015FD80  jr     t2
8015FD84  addiu  t1,zero,0x38
8015FD88  nop                         # unreachable padding
```

The preceding canonical `SetSemiTrans` helper ends before this range. The next
independent BIOS trampoline starts at `0x8015FD8C` and selects `B0:45`.
Because BIOS returns, if at all, through the original caller's `ra`, the NOP
at `0x8015FD88` is unreachable alignment padding rather than a continuation.

An exhaustive aligned scan finds no direct jump or JAL to `0x8015FD80`,
`0x8015FD84`, or `0x8015FD88`. There is no overlay duplicate in the retained
executable and no split-owner hazard.

## Complete caller census

Exactly seven direct calls target the entry:

```text
800F8314
801138FC 80118044 801189F0 80118B8C 80118E8C 80118EEC
```

The first is the fatal branch of `HandleErrorSeverity`, and passes status zero.
All six remaining calls pass `-1`. They cover an explicit fatal-report path,
three-arena allocation exhaustion, resource bounds failures, and shutdown or
unrecoverable loader paths. None consumes a return value. Instructions placed
after several calls are alternate/nonfatal joins or compiler continuation
code and are unreachable when the BIOS honors `exit`.

## Effects and limits

The trampoline itself has no stack frame, memory access, global access,
ordinary call, or hardware-register access. All termination and environment
cleanup effects belong to BIOS service `B0:38`. It forwards the status in
`a0` unchanged; the observed statuses are exactly `0` and `0xFFFFFFFF`.

This identity is stronger than a descriptive fatal-error name: the vector and
selector establish the canonical BIOS service, while the independently
audited allocator at `0x80117FD0` and error adapter at `0x800F82EC` establish
the observed non-returning control-flow role.

## Sources checked

- `disc/SCES_028.45`, excluding the `0x800` PS-X EXE header, for exact words,
  all caller arguments, duplicate search, and exhaustive interior targets;
- `generated/SCES_028.45_full.ranges` for the exact four-word range;
- `docs/audits/semantic-800f82ec.md` and
  `docs/audits/semantic-80117fd0.md` for independent fatal-call behavior.
