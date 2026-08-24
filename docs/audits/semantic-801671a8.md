# Semantic audit: `0x801671A8`

## Result

`0x801671A8` is the standard Psy-Q/BIOS `Puts` thunk, not a critical-section
syscall companion. It selects service `0x3F` at the B0 BIOS vector and forwards
the caller's string pointer unchanged.

## Boundary and ABI

The generated manifest assigns owner range `0x801671A8..0x801671BF` (0x18
bytes), with the next independent owner at `0x801671C0`. The actual executable
thunk is the first 0x0C bytes:

```text
801671A8  addiu t2, zero, 0xB0
801671AC  jr    t2
801671B0  addiu t1, zero, 0x3F   # delay slot
```

Bytes `0x801671B4..0x801671BF` are three NOPs after the unconditional jump and
are unreachable alignment padding. The generator exposes `0x801671B4` only as
an artificial continuation/resume label; no guest branch or call targets it.

The semantic prototype is conservatively:

```c
int Puts(const char *string);
```

`$a0` remains untouched and is the NUL-terminated string pointer. The wrapper
does not synthesize its own return: `$v0` is whatever the BIOS B0:3F service
returns.

## Selector and effects

The fixed vector address `0xB0` plus selector `$t1 = 0x3F` is the BIOS string
output service convention used by Psy-Q `puts`. Every caller prepares `$a0` as
an address in the executable's string-data region before calling it.

The wrapper itself has no stack, RAM, MMIO, or COP0 access. Its only semantic
effect is the delegated BIOS/TTY string output. In particular, it executes no
`syscall` instruction and does not change COP0 Status or interrupt-enable state;
that behavior belongs to `EnterCriticalSection` at `0x801671D0`.

## Executable caller census

There are exactly ten generated instances and ten unique executable `jal`
sites, with no overlay duplication:

`0x8015FAC8`, `0x8016060C`, `0x80160900`, `0x80160948`, `0x80167B08`,
`0x80167C08`, `0x80167E88`, `0x801682E4`, `0x801686E8`, and `0x80168968`.

All target the true entry `0x801671A8`. No executable call or branch targets
the padding at `0x801671B4..0x801671BF`, and there are no data-derived false
callers.
