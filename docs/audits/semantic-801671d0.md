# Semantic audit: `0x801671D0`

## Result

`0x801671D0` is the standard Psy-Q/BIOS `EnterCriticalSection` syscall thunk.
It selects syscall function 1, disables the current interrupt-enable bit, and
returns whether that bit had been enabled.

## Boundary and ABI

The generated range manifest gives the exact independent range
`0x801671D0..0x801671DF` (0x10 bytes). The next owner begins at `0x801671E0`.
The preceding `0x801671C0` owner jumps through `$t2`; its separately emitted
`0x801671CC` resume NOP is not a control-flow entry into this thunk. No branch,
jump, or call targets an address inside the four-instruction range.

The semantic prototype is:

```c
uint32_t EnterCriticalSection(void);
```

The instructions are `addiu $a0,$zero,1`, `syscall 0`, `jr $ra`, and a NOP
delay slot. Thus it consumes no caller arguments and returns the syscall result
in `$v0`.

## Selector, return, and effects

The fixed `$a0 = 1` identifies the PS1 BIOS `EnterCriticalSection` service.
The runtime's syscall implementation confirms the hardware-visible contract:
it reads COP0 Status, clears IEc (bit 0), and returns the old IEc bit in `$v0`.
The result is therefore exactly zero or one, not the complete old Status word.
The thunk has no stack or RAM access and makes no library call; its only state
effect is the syscall's interrupt-disable operation (plus the runtime's
diagnostic bookkeeping, which is not guest state).

The adjacent balancing syscall-2 thunk is elsewhere at `0x80165550`, as also
observed in the audited SIO1 lifecycle paths.

## Executable caller census

There are 19 generated call instances but 16 unique guest call sites. Each
listed address is the `jal` itself (the generated evidence displays its delay
slot four bytes later):

`0x8015FDC0`, `0x8015FE34`, `0x80161388`, `0x80161C2C`, `0x80161CB0`,
`0x80161CE0`, `0x80162ABC`, `0x80163C80`, `0x80166FA8`, `0x80167230`,
`0x801672A8`, `0x80167478`, `0x8016750C`, `0x80169384`, `0x80169448`, and
`0x8016B864`.

The three SIO1 sites `0x80161C2C`, `0x80161CB0`, and `0x80161CE0` each occur
in two generated overlay variants, accounting exactly for the three extra
instances. All 16 unique sites are executable `jal` instructions; there are no
data-derived references and no interior targets.
