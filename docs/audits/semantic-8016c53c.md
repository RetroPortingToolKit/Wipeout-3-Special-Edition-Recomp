# Semantic audit: `0x8016C53C` / `HasTimer2TimeoutExpired`

## Result and ABI

`0x8016C53C` polls the shared timeout established by `ArmTimer2Timeout` and
returns whether its unsigned elapsed count has reached the saved threshold:

```c
bool HasTimer2TimeoutExpired(void); // exactly 0 or 1
```

No argument register is read.  `v0` is formed by an unsigned comparison and
final XOR, so both return values are canonical integers `0` and `1`.

## Exact boundary and target census

The generated range assigns `[0x8016C53C, 0x8016C5DC)`, exactly `0xA0` bytes,
to this frameless leaf.  It returns at `0x8016C5D4` and produces the final
boolean in the return delay slot at `0x8016C5D8`.  The next owner starts at
`0x8016C5DC`.

An aligned scan of the authenticated executable finds exactly seven entry
JALs:

```text
80169624 80169680 801696FC 80169928 801699FC 80169C74 80169D64
```

There are no direct JALs to any interior instruction.  Callers either loop
while the result is zero to implement a bounded wait/delay or branch on one to
take a device-operation timeout path.

## Counter and wrap calculation

The poller reads current Timer 2 count as an unsigned halfword from
`0x1F801120`, then loads the saved zero-extended start count from `0x801EEBC8`.
When `current >= start`, the adjusted current count is simply `current`.

When `current < start`, it treats this as one wrap and reads Timer 2 target at
`0x1F801128`:

- if the target halfword is nonzero, it adds that target value to `current`;
- if the target is zero, it adds `0x10000`.

The target register is deliberately read twice on its nonzero path (test, then
add).  The implementation does not add one to a nonzero target and cannot
account for more than one wrap between arm and poll.  Both behaviors are part
of the exact original contract rather than generalized timer arithmetic.

## Mode scaling and predicate

The function then reads Timer 2 mode at `0x1F801124` and tests bit `0x200`:

```c
uint32_t elapsed = adjusted_current - saved_start;
if ((timer2_mode & 0x200) == 0)
    elapsed >>= 3;
return elapsed >= saved_threshold;
```

The threshold is the word at `0x801EEBC4`.  Both comparison operands are
unsigned.  Equality is expired (`1`), not still waiting.  The mode read occurs
after wrap adjustment, and scaling never modifies the saved shared state.

## State and pairing

The poller writes no RAM or hardware register.  Its only effects are Timer 2
counter/target/mode reads and the returned predicate.  `ArmTimer2Timeout`
overwrites the shared threshold and start snapshot, so concurrent or nested
users are not independently represented; all seven callers observe the most
recent arm operation.

## Confidence

`confirmed`: exact boundary and boolean ABI, exhaustive caller/interior-target
census, one-wrap branches, target-zero fallback, mode-bit scaling, unsigned
threshold comparison including equality, and absence of persistent writes are
instruction-proven.
