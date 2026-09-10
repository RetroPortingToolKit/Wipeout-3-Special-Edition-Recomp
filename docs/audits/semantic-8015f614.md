# Semantic audit: `0x8015F614` / `Strcpy`

## Result and ABI

`0x8015F614` is the canonical PlayStation BIOS A0/19 `strcpy` trampoline,
cataloged as `Strcpy` to match the capitalization used for the recovered BIOS
`Memcpy` and `Printf` exports:

```c
char *Strcpy(char *destination, const char *source);
```

The trampoline does not alter `a0` or `a1`, and it preserves the caller's `ra`
while jumping to the BIOS vector.  BIOS therefore receives the two arguments
unchanged and returns its destination pointer in `v0` directly to the original
caller.

## Exact boundary and target census

The generated range assigns `[0x8015F614, 0x8015F624)`, exactly four words or
`0x10` bytes, to this owner:

```text
8015F614  addiu t2, zero, 0xA0
8015F618  jr    t2
8015F61C  addiu t1, zero, 0x19   # vector-jump delay slot
8015F620  nop                     # unreachable padding
```

The next owner starts at `0x8015F624`.  An aligned scan of the authenticated
executable finds exactly twenty direct JAL sites:

```text
800FAB80 800FC090 800FDE88 800FE0B0 800FE610 800FE894
80101A48 80101A5C 80101A94 80101AA8 80102038
80111CE8 80111D00 80113420
801178DC 801178F0 80117EB4 80117EC0
80125F74 80145A40
```

There are no direct calls to `0x8015F618`, `0x8015F61C`, or padding word
`0x8015F620`.  Caller data flow consistently supplies a writable destination
in `a0` and a static or record-owned NUL-terminated source in `a1`; uses include
names, labels, and fixed-size record string fields.

## Formula and effects

The stub's complete formula is a BIOS tail dispatch:

```c
t2 = 0x000000A0; // BIOS A-vector
t1 = 0x19;       // strcpy service
goto t2;         // ra remains the game's caller
```

It has no stack frame, RAM load/store, loop, bounds check, ordinary call, or
hardware access of its own.  The selected BIOS service supplies conventional
`strcpy` behavior: copy source bytes through and including the first NUL into
destination and return the original destination pointer.  Source/destination
overlap is not made safe, destination capacity is not known, and invalid
pointers can fault; those semantics and all actual writes belong to BIOS
A0/19 rather than instructions in the trampoline.

Because this is a tail jump, a recompiler or replacement must preserve the
BIOS return value and original caller return address.  Modeling it as a normal
nested call with a second return through `0x8015F620` would be incorrect; that
padding is unreachable in native execution.

## Confidence

`confirmed`: exact boundary, BIOS vector and selector, two-argument ABI,
destination return contract, exhaustive executable caller/interior-target
census, caller pointer roles, and absence of direct state effects are
instruction-proven.
