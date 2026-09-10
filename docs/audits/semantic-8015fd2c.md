# Semantic audit: `0x8015FD2C` / `SetPolyG3`

## Result and ABI

`0x8015FD2C` is the canonical Psy-Q `SetPolyG3` primitive initializer:

```c
void SetPolyG3(POLY_G3 *primitive);
```

It consumes only `a0`, the destination primitive pointer. It writes byte 6 to
`primitive + 3`, the packet-tag command-word count, and byte `0x30` to
`primitive + 7`, the GPU polygon opcode. GPU opcode `0x30` is the shaded
three-vertex polygon command, and its six following words give the matching
packet length. This pair uniquely identifies `POLY_G3`.

The command-byte write occurs in the `jr ra` delay slot and therefore always
executes. The routine does not produce a semantic return value.

## Boundary and effects

The owner is exactly `0x8015FD2C..0x8015FD3F` (0x14 bytes, five instructions).
The next owner starts at `0x8015FD4C`; bytes `0x8015FD40..0x8015FD4B` are not
part of this callable. No external target enters an interior instruction.

Only bytes `+3` and `+7` change. The tag's 24-bit ordering-table link, all RGB
bytes, and all three coordinate pairs remain untouched. There are no calls,
global accesses, GPU MMIO writes, synchronization operations, or validation.

## Callers

Six unique JAL sites target the entry:

- `0x800F76BC`, `0x800F76C8`, `0x800F76D4`, and `0x800F76E0`;
- `0x80149804`;
- `0x8014A65C`.

There are no overlay duplicates and no calls to an interior address.
