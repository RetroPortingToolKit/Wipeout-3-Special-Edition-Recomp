# Semantic audit: `0x800EEE64`

## Result

`0x800EEE64` is `LeaveNestedInterruptCriticalSection`, the exact counterpart
of `EnterNestedInterruptCriticalSection` at `0x800EEE18`.  Nested leaves only
decrement the shared nesting word; the outer leave restores the interrupt
bits changed by the outer enter.

## Prelude, boundary, and ABI

The first three instructions are a prelude before the conventional frame:
`0x800EEE64` materializes the global base, `0x800EEE68` loads
`gInterruptCriticalNesting` at `0x80173334`, and only `0x800EEE6C` allocates
the 24-byte frame.  This pre-frame load is part of the owner, not a tail of
the preceding function: the preceding enter returns at `0x800EEE5C` and
restores its own stack in the delay slot at `0x800EEE60`.

The leave saves `ra` in the branch delay slot at `0x800EEE74`.  Both paths
join at `0x800EEE90`; `jr ra` is at `0x800EEE98` and its delay slot restores
`sp` at `0x800EEE9C`.  The next independent owner starts at `0x800EEEA0`.
Therefore the exact half-open boundary is `[0x800EEE64,0x800EEEA0)`, 60 bytes,
matching generated `R 800EEE64 3C`.

No argument register is read and no semantic return value is established.

## Nesting and Status behavior

The branch at `0x800EEE70` tests the loaded nesting word for nonzero:

- Nonzero: `0x800EEE88` subtracts one and `0x800EEE8C` writes the result back.
  No helper is called and hardware interrupt state remains disabled.
- Zero: `0x800EEE78` calls `0x8015F764`; the nesting word is not written.

Direct disassembly of `0x8015F764` proves that it reads COP0 Status (`$12`),
ORs it with `0x401`, writes Status back, executes the required hazard NOPs,
and returns.  Thus it sets IEc (bit 0) and interrupt-mask bit 10.

This exactly pairs with `EnterNestedInterruptCriticalSection`: outer enter
sets the word to zero and calls `0x8015F7F4`, which clears Status bits 0 and
10; entries observed while interrupts are disabled increment the word.
Balanced nested leaves count back down, and the leave observing zero restores
both bits.  Neither side validates balance.  In particular, any nonzero value,
including a negative/corrupt value, follows the decrement path.

## Direct callers

An exhaustive aligned JAL scan of the loaded EXE finds exactly 22 sites:

```text
800EE0F0 800EE214 800EE554 800EE624 800EE7F8 800EEBC0 800EED14
800EED30 800EED78 800EEE00 800EEEC0 800EEEE8 80112CA0 80115198
801153D0 8011543C 801208A0 801208D0 80120904 8012093C 80120970
80120AC0
```

The paired enter also has 22 callers.  Call sites are not necessarily adjacent
one-for-one because protected regions contain control flow, but the equal
high-fanout sets and complementary counter/Status transitions establish the
pairing.  No direct call targets an interior block of the leave owner.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` PS-X EXE header, for exact MIPS
  instructions and the exhaustive JAL scan;
- `generated/SCES_028.45_full.ranges` for `R 800EEE64 3C`;
- generated owners for the five-block control flow and caller cross-check;
- `docs/audits/semantic-800eee18.md` for the independently proven enter half.
