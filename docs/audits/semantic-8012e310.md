# Semantic audit: `0x8012E310` / `SwapAdjacentBytes`

## Result

`0x8012E310` is a minimal in-place byte-pair swap:

```c
void SwapAdjacentBytes(uint8_t *pair) {
    uint8_t first = pair[0];
    pair[0] = pair[1];
    pair[1] = first;
}
```

The original second byte remains incidentally in `v0`, but callers use the
memory mutation rather than a scalar result; the semantic contract is void.

## Boundary and effects

The exact owner is `[0x8012E310, 0x8012E324)`, five instructions or `0x14`
bytes, in one frameless basic block. `0x8012E324` is the next independent
byte-permutation helper.

The function reads offsets zero and one, writes the same two bytes in swapped
order, and touches nothing else. There are no checks, calls, globals, stack
accesses, coprocessor operations, or hardware effects. Aliasing is intrinsic
because both byte accesses use the same `a0` base.

## Complete incoming proof

An aligned executable scan finds exactly 25 direct JALs, all to the entry and
none to an interior address:

```text
800E5E38 800E5EA0 800E5EB4 800E5F4C 800E5F9C
800E5FA8 800E5FB4 800E5FC0 800E5FCC 800E5FD8
8012D244 8012D254 8012D264 8012D26C 8012D274 8012D27C
8012E38C 8012E39C 8012E3AC 8012E3B4 8012E3BC 8012E3C4
80141200 80141214 80141228
```

The concentrated callers invoke it as one operation within larger byte-order
and geometry-record permutation sequences. No stronger domain-specific field
name is needed for the exact behavior.

## Confidence

`confirmed`: range, pointer ABI, both loads/stores, effect boundary, all callers,
and absence of interior targets are direct instruction-level facts.
