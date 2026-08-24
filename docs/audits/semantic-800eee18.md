# Semantic audit: `0x800EEE18`

## Result

`0x800EEE18` is the no-argument nesting adapter now named
`EnterNestedInterruptCriticalSection`. It disables the interrupt state on the
outer entry and records additional entries made while that state is already
disabled. Its paired leave adapter begins at `0x800EEE64`.

## Exact boundary and ABI

- `0x800EEE18` allocates a 24-byte frame and saves `ra`.
- Both paths join at `0x800EEE54`; the sole epilogue restores `ra`, returns at
  `0x800EEE5C`, and restores `sp` in the delay slot at `0x800EEE60`.
- `0x800EEE64` starts independently by loading the shared nesting word and
  then creates its own frame.

The exact owner is therefore `[0x800EEE18, 0x800EEE64)`, `0x4C` bytes, matching
the generated range. It reads no argument registers and does not establish a
semantic return value.

## Behavior and callee

At `0x800EEE20` the routine reads COP0 Status (`$12`) and tests bit 0, the
current interrupt-enable bit (`IEc`). The shared signed nesting word is at
`0x80173334`.

- If `IEc` is clear on entry, interrupts are already disabled. The routine
  increments `gInterruptCriticalNesting` and returns without a call.
- If `IEc` is set, this is the outer entry. The delay slot at `0x800EEE50`
  resets the nesting word to zero and the routine calls `0x8015F7F4`.

Direct disassembly proves that `0x8015F7F4` reads COP0 Status, ANDs it with
`~0x401`, writes it back, and returns. It therefore clears IEc (bit 0) together
with interrupt-mask bit 10. This is a low-level interrupt-disable leaf, not an
opaque game callback.

The paired routine at `0x800EEE64` confirms the nesting interpretation: when
the word is nonzero it decrements it; at zero it calls `0x8015F764`, whose
`Status |= 0x401` restores the two bits. Thus a balanced outer pair performs
the hardware transition, while nested pairs only adjust the counter. The
counter is deliberately based on observed disabled state; this entry does not
validate overflow or distinguish an unrelated pre-disabled context.

## Direct callers

An exhaustive aligned JAL scan of the loaded EXE finds exactly 22 callers:

```text
800EDFE4 800EE124 800EE374 800EE5B4 800EE77C 800EEBA4 800EEC98
800EED24 800EED68 800EEDA4 800EEEB4 800EEEE0 80112C44 80114F10
801151A8 801202F8 80120884 801208C0 801208F8 80120928 8012095C
80120AA4
```

The paired leave entry also has 22 direct callers. The sites span the adjacent
low-level graphics/system routines and higher resource/audio control paths,
which supports a shared synchronization role rather than a subsystem-specific
name. No direct JAL targets an internal block of the entry routine.

## Sources checked

- `disc/SCES_028.45`, respecting the `0x800` PS-X EXE header, for exact MIPS
  instructions and exhaustive caller scans;
- `generated/SCES_028.45_full.ranges` for the independent `0x4C` range;
- direct disassembly of `0x800EEE64`, `0x8015F764`, and `0x8015F7F4` for the
  paired nesting and COP0 transition semantics.
