# Semantic audit: `0x80168CF8`

## Result

`0x80168CF8` is the standard Psy-Q/BIOS `DeliverEvent` thunk. It selects service
`0x07` at the B0 BIOS vector and forwards the caller's event class and specifier.

## Boundary and ABI

The generated manifest assigns exact owner range `0x80168CF8..0x80168D07`
(0x10 bytes); the next independent owner begins at `0x80168D08`. The executable
thunk is only the first 0x0C bytes:

```text
80168CF8  addiu t2, zero, 0xB0
80168CFC  jr    t2
80168D00  addiu t1, zero, 0x07   # delay slot
```

`0x80168D04` is an unreachable padding NOP after the unconditional jump. The
generator exposes it as a continuation/resume label, but no guest branch or
call targets it.

The canonical libapi prototype is:

```c
void DeliverEvent(uint32_t event_class, uint32_t event_spec);
```

The thunk preserves `$a0` and `$a1`, supplying only vector `$t2 = 0xB0` and
selector `$t1 = 7`. Although the underlying kernel leaves comparison scratch in
`$v0`, libapi types the operation as void and callers must not consume a return.

## Event effects

The B0:07 identity is independently confirmed by the repository's BIOS HLE and
SCPH1001 event-kernel analysis. For every enabled event-control block matching
the supplied class and spec, delivery either marks a mode-`0x2000` event READY
or invokes the callback of a mode-`0x1000` event. Other entries are skipped.

The three-instruction thunk itself has no stack, RAM, MMIO, or COP0 access. Its
guest-visible effects are entirely delegated to the BIOS event kernel and any
matching callback it invokes.

## Executable caller census

There are exactly eleven generated instances and eleven unique executable
`jal` sites, so there is no overlay duplication:

`0x80160E04`, `0x80160E2C`, `0x80160E54`, `0x801614AC`, `0x801614D8`,
`0x8016171C`, `0x80161734`, `0x80161F18`, `0x801620B4`, `0x80162140`, and
`0x8016B24C`.

All target the true entry `0x80168CF8`. No executable reference targets the
padding NOP at `0x80168D04`, and there are no data-derived false callers.
