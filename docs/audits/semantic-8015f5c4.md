# Semantic audit: `0x8015F5C4` / `OpenEvent`

## Result and ABI

`0x8015F5C4` is the canonical PlayStation BIOS B0:08 `OpenEvent` trampoline:

```c
int OpenEvent(uint32_t event_class, uint32_t event_spec,
              uint32_t mode, void (*callback)(void));
```

It loads `t2 = 0xB0`, jumps to the B0 BIOS vector, and selects service
`t1 = 8` in the delay slot. Incoming `a0..a3` and the original caller's `ra`
are preserved, so all four event fields reach BIOS directly and its event
handle/result in `v0` returns unchanged.

## Boundary and effects

The owner is exactly `0x8015F5C4..0x8015F5D3` (0x10 bytes). Its first three
words execute; `0x8015F5D0` is an unreachable padding NOP. The next independent
owner begins at `0x8015F5D4`. No JAL enters an interior or padding word.

The stub itself has no stack, memory, MMIO, interrupt, or callback effects.
Event allocation and callback registration are BIOS-owned.

## Callers

Six unique JAL sites target the entry:

- `0x80112C5C` and `0x80112C7C`;
- `0x80113C8C`, `0x80113CAC`, and `0x80113CCC`;
- `0x8016B888`.

There are no overlay duplicates or external interior targets.
