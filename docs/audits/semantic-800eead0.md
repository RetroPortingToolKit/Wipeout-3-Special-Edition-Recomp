# Semantic audit: `0x800EEAD0` / `SetGlobalPrimitiveRgb`

## Result and ABI

`0x800EEAD0` is the global three-byte color setter conservatively named
`SetGlobalPrimitiveRgb`:

```c
void SetGlobalPrimitiveRgb(uint8_t red, uint8_t green, uint8_t blue);
```

The routine consumes `a0`, `a1`, and `a2`, truncates each to its low byte, and
stores them at `0x80173308`, `0x80173309`, and `0x8017330A`, respectively. The
blue store executes in the `jr ra` delay slot. No semantic result is returned.

The name is based on the only reader of these globals, owner `0x800EE6EC`:
that routine copies the three bytes into consecutive RGB fields of a generated
render primitive. The conservative `GlobalPrimitive` wording avoids assigning
an unproven scene-light or material identity.

## Boundary and effects

The owner is exactly `0x800EEAD0..0x800EEAEB` (0x1C bytes, seven instructions).
The independent framed owner at `0x800EEAEC` is not part of it. There are no
branches, calls, padding words, or external interior targets.

Only the three global bytes change. The function performs no reads, range
checks, memory allocation, GPU MMIO, synchronization, or callback dispatch.
Values outside 0..255 wrap by low-byte truncation.

## Callers

Five unique JAL sites target the entry:

- `0x8012459C` and `0x80124620`;
- `0x8012DF04`;
- `0x8014B75C` and `0x8014B7D4`.

There are no overlay duplicates and no calls to an interior address.
