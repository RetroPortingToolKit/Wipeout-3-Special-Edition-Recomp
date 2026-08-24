# Semantic audit: `0x80168D28` / `SysDeqIntRP`

## Result and effective ABI

`0x80168D28` is the canonical PlayStation BIOS C0:03 `SysDeqIntRP`
trampoline. Its effective register interface is:

```c
int SysDeqIntRP(int priority_or_index, void *interrupt_record);
```

It loads `t2 = 0xC0`, jumps to the C0 BIOS vector, and selects service
`t1 = 3` in the delay slot. Incoming `a0`, `a1`, and caller `ra` are preserved,
so the two BIOS arguments and its return value pass through unchanged. The
generic pointer spelling records the proven register contract without guessing
the private BIOS interrupt-record structure.

## Boundary and effects

The owner is exactly `0x80168D28..0x80168D37` (0x10 bytes). Three words execute
and `0x80168D34` is an unreachable padding NOP. The next independent owner
begins at `0x80168D38`; no JAL targets the padding or an interior instruction.

The wrapper itself has no stack, memory, MMIO, callback, or COP0 effects.
Interrupt-chain removal and validation are BIOS-owned.

## Callers

Six unique JAL sites target the entry:

- `0x80162248` and `0x80162328`;
- `0x80167270` and `0x801672B8`;
- `0x80169398` and `0x80169464`.

There are no overlay duplicates or external interior targets.
