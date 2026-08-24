# Semantic audit: `0x8015CF38` / `Memcpy`

## Result

`0x8015CF38` is the PlayStation BIOS A0/2A `memcpy` trampoline, cataloged as
`Memcpy` to match exported symbol capitalization:

```c
void *Memcpy(void *destination, const void *source, size_t length);
```

It loads `t2 = 0xA0`, jumps to that BIOS vector, and loads function selector
`t1 = 0x2A` in the delay slot. Registers `a0`, `a1`, and `a2` therefore reach
BIOS unchanged, as does the caller's return address. The BIOS destination
result in `v0` returns unchanged.

## Boundary and effects

The exact range is `[0x8015CF38, 0x8015CF48)`, four instructions or `0x10`
bytes. The final word is unreachable padding after the indirect vector jump.
The adjacent `Printf` thunk starts at `0x8015CF48`.

The thunk itself has no stack frame, RAM access, ordinary calls, or hardware
operations. Copy semantics, overlap limitations, faults, cache behavior, and
the return value belong wholly to BIOS A0/2A.

## Complete caller proof

An aligned executable scan finds exactly 34 direct JALs, all to the entry and
none to the padding:

```text
800E6F0C 800E7130 800F04A0 800F4888 800F48A0 800FF714 800FF740 800FF768
800FF780 80102964 801029A0 80102D60 80102D74 801049C8 801049DC 801122E0
8011591C 801159D4 80117A6C 8012280C 80123FDC 8012CEDC 8012E49C 801316C4
80131D4C 8014112C 80144B30 80144B78 80144BB4 8014B92C 8015D728 8015DC20
801642E4 80164610
```

Recovered archive, object-instance, texture, and draw-environment owners all
independently invoke this address with destination/source/byte-count arguments.

## Confidence

`confirmed`: BIOS vector and selector, ABI, exact range, return propagation,
all callers, and absence of interior entries are instruction-proven.
