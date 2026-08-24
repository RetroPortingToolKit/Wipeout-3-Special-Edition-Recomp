# Semantic audit: `0x801671C0` / `ChangeClearPad`

## Result and ABI

`0x801671C0` is the canonical PlayStation BIOS C0:0A `ChangeClearPad`
trampoline:

```c
int ChangeClearPad(int mode);
```

It loads `t2 = 0xC0`, jumps to the C0 BIOS vector, and selects service
`t1 = 0x0A` in the jump delay slot. Incoming `a0` and the original caller's
`ra` are preserved, so the BIOS receives `mode` directly and returns straight
to the caller. Any result in `v0` is propagated unchanged.

This identifies the wrapper itself exactly. The mutation of the BIOS pad-clear
policy, validation of `mode`, and precise old/new return convention remain
BIOS-owned and are not reimplemented by the stub.

## Boundary and effects

The generated owner is exactly `0x801671C0..0x801671CF` (0x10 bytes). Its first
three instructions are executable. `0x801671CC` is an unreachable padding NOP
after the vector jump; the independent confirmed `EnterCriticalSection` owner
starts at `0x801671D0`. No direct control-flow target enters the padding.

The trampoline itself has no stack, guest-memory, MMIO, callback, or COP0
effects. All pad-policy state changes occur within BIOS service C0:0A.

## Callers

Seven unique direct JAL sites target the true entry:

- `0x8015FADC`;
- `0x80162A18`, `0x80162A30`, `0x80162A48`, and `0x80162A5C`;
- `0x801693CC` and `0x80169454`.

There are no overlay duplicates and no calls to an interior or padding address.
The concentration in pad/interrupt initialization code is consistent with the
canonical service identity.
