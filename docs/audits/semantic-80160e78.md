# Semantic audit: `0x80160E78` / `WaitEvent`

## Result and ABI

`0x80160E78` is the canonical PlayStation BIOS B0:0A `WaitEvent` trampoline:

```c
int WaitEvent(int event_handle);
```

It loads `t2 = 0xB0`, jumps to the B0 BIOS vector, and selects service
`t1 = 0x0A` in the delay slot. Incoming `a0` and the original caller's `ra`
remain untouched, so the handle reaches BIOS directly and the BIOS result in
`v0` propagates unchanged.

## Boundary and effects

The owner is exactly `0x80160E78..0x80160E87` (0x10 bytes). Its first three
words execute; `0x80160E84` is an unreachable padding NOP after the vector
jump. The next independent owner begins at `0x80160E88`, and no JAL enters an
interior or padding address.

The trampoline itself has no stack, guest-memory, MMIO, callback, or COP0
effects. Blocking, event-state transitions, handle validation, and scheduling
are BIOS-owned.

## Callers

Six unique JAL sites target the true entry:

- `0x80112D5C`, `0x80112E44`, `0x80113140`, `0x80113294`, and `0x8011371C`;
- `0x801665B0`.

There are no overlay duplicates or external interior targets.
