# Semantic audit: `0x80168D18` / `FlushCache`

## Result and ABI

`0x80168D18` is the canonical PlayStation BIOS A0:44 `FlushCache` trampoline:

```c
void FlushCache(void);
```

It loads `t2 = 0xA0`, jumps to the A0 BIOS vector, and selects service
`t1 = 0x44` in the delay slot. The original caller's `ra` is preserved, so the
BIOS returns directly. No argument is constructed or consumed by the wrapper.

## Boundary and effects

The owner is exactly `0x80168D18..0x80168D27` (0x10 bytes). Its first three
words execute; `0x80168D24` is an unreachable padding NOP. The adjacent
independent owner begins at `0x80168D28`. No JAL enters an interior word.

The trampoline itself has no stack, guest-memory, MMIO, callback, or COP0
effects. Cache invalidation and architectural synchronization are BIOS-owned.

## Callers

Six unique JAL sites target the entry:

- `0x800D2F34`;
- `0x80167028`, `0x801673BC`, `0x801674E8`, and `0x80167558`;
- `0x8017BACC`.

There are no overlay duplicates or external interior targets.
