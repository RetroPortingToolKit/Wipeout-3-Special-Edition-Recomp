# Semantic audit: `0x8015F5D4` / `Strlen`

## Result and ABI

`0x8015F5D4` is the canonical PlayStation BIOS A0/1B `strlen` trampoline,
cataloged as `Strlen` consistently with the recovered `Strcpy`, `Toupper`,
`Memcpy`, and `Printf` BIOS exports:

```c
size_t Strlen(const char *string);
```

It preserves incoming `a0` and the game's caller `ra`, then returns the BIOS
byte count in `v0` directly to the original caller.

## Exact boundary and padding

The generated range assigns `[0x8015F5D4, 0x8015F5E4)`, exactly four words or
`0x10` bytes, to this owner:

```text
8015F5D4  addiu t2, zero, 0xA0
8015F5D8  jr    t2
8015F5DC  addiu t1, zero, 0x1B   # vector-jump delay slot
8015F5E0  nop                     # unreachable padding
```

The next owner begins at `0x8015F5E4`.  BIOS returns through the untouched
original `ra`, so execution never resumes at padding word `0x8015F5E0`.

## Complete caller proof

An aligned scan of the authenticated executable finds exactly eight direct
entry JALs:

```text
8010F774 80112F70 80113508 80131C24
80131E0C 8014089C 80140960 8015CDBC
```

There are no direct calls to `0x8015F5D8`, `0x8015F5DC`, or padding word
`0x8015F5E0`.  Caller uses consume `v0` as a length for comparison, formatting,
copy/transform bounds, or retained local length state.  They pass static,
stack, or record-owned NUL-terminated byte strings in `a0`.

## Formula and effects

The trampoline's complete behavior is:

```c
t2 = 0x000000A0; // BIOS A-vector
t1 = 0x1B;       // strlen service
goto t2;         // original caller ra remains live
```

It has no stack frame, RAM access, loop, ordinary call, global access, or
hardware effect of its own.  BIOS A0/1B scans bytes from `string` until the
first NUL and returns the number preceding it, excluding the terminator.  No
maximum length or null-pointer guard is supplied; scanning and any invalid
pointer fault belong wholly to BIOS.

The thunk does not narrow or sign-adjust the BIOS result.  A native replacement
must preserve the full `v0` count and must not include the terminating NUL.

## Confidence

`confirmed`: canonical identity from BIOS vector/selector, exact range and
padding ownership, one-argument ABI, return propagation, exhaustive executable
caller/interior-target census, caller length consumption, and absence of direct
state effects are instruction-proven.
