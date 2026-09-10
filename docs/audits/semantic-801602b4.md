# Semantic audit: `0x801602B4` / `Toupper`

## Result and ABI

`0x801602B4` is the canonical PlayStation BIOS A0/25 `toupper` trampoline,
cataloged as `Toupper` consistently with the recovered `Strcpy`, `Memcpy`, and
`Printf` BIOS exports:

```c
int Toupper(int character);
```

The trampoline leaves `a0` unchanged and preserves the game's caller `ra`
while tail-jumping to BIOS.  The BIOS integer result in `v0` therefore returns
directly to the original caller.

## Boundary and executable targets

The generated range assigns `[0x801602B4, 0x801602C4)`, exactly four words or
`0x10` bytes, to this owner:

```text
801602B4  addiu t2, zero, 0xA0
801602B8  jr    t2
801602BC  addiu t1, zero, 0x25   # vector-jump delay slot
801602C0  nop                     # unreachable padding
```

The preceding `GetTPage` owner ends at `0x801602B0`, leaving the separate NOP
at `0x801602B0` outside both ranges; the next owner begins at `0x801602C4`.

An aligned scan of the authenticated executable finds exactly one direct JAL,
at `0x80113438`, and no calls to `0x801602B8`, `0x801602BC`, or padding word
`0x801602C0`.

## Caller and effects

The sole caller loads its character with `lbu`, so the observed input is in
`[0,255]`, increments the source pointer in the JAL delay slot, and stores the
low result byte into a bounded stack string.  It repeats this while building
an uppercased name/key and NUL-terminates the result afterward.  The caller
does not pass a negative signed `char` or EOF sentinel.

The stub itself has no stack frame, memory access, loop, ordinary call, global
state, or hardware effect.  BIOS A0/25 owns character classification and case
conversion; characters it does not classify as lowercase are returned under
the BIOS service contract, and `v0` is propagated without masking by the
trampoline.  The observed caller applies its own byte truncation with `sb`.

As with the other BIOS thunks, the word after the vector jump is unreachable:
the BIOS returns through the original `ra`, not through `0x801602C0`.

## Confidence

`confirmed`: canonical identity from BIOS vector/selector, exact boundary,
integer ABI and return propagation, sole executable caller, zero interior
targets, observed unsigned-byte use, and absence of direct state effects are
instruction-proven.
