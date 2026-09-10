# Semantic audit: `0x8015F594` / `EnableEvent`

## Result and ABI

`0x8015F594` is the canonical PlayStation BIOS B0:0C `EnableEvent` trampoline:

```c
int EnableEvent(int event_handle);
```

It loads `t2 = 0xB0`, jumps to the B0 BIOS vector, and selects service
`t1 = 0x0C` in the delay slot. Incoming `a0` and the caller's `ra` are
preserved, so the handle reaches BIOS directly and the BIOS result propagates
unchanged.

## Boundary and effects

The owner is exactly `0x8015F594..0x8015F5A3` (0x10 bytes). The first three
words execute; `0x8015F5A0` is unreachable padding after the vector jump. The
next independent owner begins at `0x8015F5A4`. No JAL enters the padding or any
other interior word.

The trampoline itself has no stack, memory, MMIO, interrupt, or callback
effects. Enabling the event and validating its handle are BIOS-owned.

## Callers

Six unique entry JALs exist:

- `0x80112C8C` and `0x80112C98`;
- `0x80113D0C`, `0x80113D18`, and `0x80113D24`;
- `0x8016B898`.

There are no overlay duplicates or external interior targets.
