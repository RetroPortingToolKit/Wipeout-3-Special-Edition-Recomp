# Semantic audit: `0x8012E324` / `ReverseFourBytes`

## Result

`0x8012E324` reverses one four-byte field in place:

```c
void ReverseFourBytes(uint8_t *bytes) {
    uint8_t a = bytes[0], b = bytes[1], c = bytes[2], d = bytes[3];
    bytes[0] = d;
    bytes[1] = c;
    bytes[2] = b;
    bytes[3] = a;
}
```

## Boundary and effects

The exact owner is `[0x8012E324, 0x8012E348)`, nine instructions or `0x24`
bytes, in one frameless basic block. `0x8012E348` is the next independent
permutation dispatcher.

It performs four unsigned byte loads before any store, so the in-place reversal
is safe for the single four-byte object. It writes exactly those four offsets.
There are no calls, globals, stack accesses, branches, checks, coprocessor
operations, or hardware effects. The incidental final loaded byte in `v0` is
not a semantic return.

## Complete incoming proof

An aligned executable scan finds exactly nine direct JALs, all to the entry and
none to an interior address:

```text
800E5D5C 800E5E40 800E5ED8 800E5EE8 800E6F74
80141144 80141194 801411A8 801411BC
```

These sites combine the helper with `SwapAdjacentBytes` in larger record and
geometry byte-order transformations.

## Confidence

`confirmed`: range, pointer ABI, complete byte permutation, effect boundary,
all callers, and lack of interior targets are instruction-proven.
