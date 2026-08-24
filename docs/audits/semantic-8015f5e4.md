# Semantic audit: `0x8015F5E4` / `Strcmp`

## Result and ABI

`0x8015F5E4` is the canonical PlayStation BIOS A0:17 `strcmp` trampoline:

```c
int Strcmp(const char *left, const char *right);
```

It loads `t2 = 0xA0`, jumps to the A0 BIOS vector, and selects service
`t1 = 0x17` in the jump delay slot. Incoming `a0` and `a1` and the original
caller's `ra` remain untouched, so the BIOS receives both string pointers and
returns its signed comparison result directly to the caller.

The stub does not normalize the return. Callers may rely on its sign and zero
status; the exact negative or positive magnitude remains BIOS-defined.

## Boundary and effects

The owner is exactly `0x8015F5E4..0x8015F5F3` (0x10 bytes). The first three
words execute, while `0x8015F5F0` is an unreachable padding NOP after the BIOS
vector jump. The next independent owner begins at `0x8015F5F4`. No direct
control-flow target enters the padding word.

The trampoline itself has no stack, global, MMIO, callback, or COP0 effects.
String reads, null termination, fault behavior, and comparison are BIOS-owned.

## Callers

Seven unique JAL sites target the true entry:

- `0x800FDE68`, `0x800FE090`, and `0x800FE874`;
- `0x8015FFC0`, `0x80160068`, and `0x80160148`;
- `0x80164448`.

There are no overlay duplicates and no calls to an interior or padding address.
All uses occur in string/file-name comparison paths, consistent with the
canonical service identity.
