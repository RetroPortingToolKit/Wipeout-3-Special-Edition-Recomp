# Semantic audit: `0x8016B5AC`

## Result

`0x8016B5AC` is `NormalizeAndRegisterSpuAddress`, a leaf helper used by the
adjacent SPU library to normalize a byte address, convert it to the library's
configured SPU-address unit, and optionally publish that unit in its voice
address table.

## Boundary and ABI

The generated range table gives the exact owner as
`[0x8016B5AC, 0x8016B650)` (`0xA4` bytes). Its blocks are `0x8016B5AC`,
`B5C0`, `B5D8`, `B5DC`, `B5E8`, `B5FC`, `B618`, `B624`, `B62C`, `B634`, and
`B648`; the last block is `jr ra` with its delay slot at `0x8016B64C`, and the
next owner starts at `0x8016B650`. It has no frame and makes no calls.

The recovered ABI is:

```c
uint32_t NormalizeAndRegisterSpuAddress(int32_t selector, uint32_t address);
```

If the flag at `0x8017B540` is set, `address` is rounded upward using the
configured divisor/alignment at `0x8017B548` and mask at `0x8017B54C` (the
unsigned divide retains the architectural divide-by-zero `break`). The helper
then computes `unit = address >> *(uint32_t *)0x8017B544`.

- `selector == -2`: return `unit & 0xFFFF`.
- `selector == -1`: return the normalized byte address.
- otherwise: store `unit & 0xFFFF` at
  `*(uint32_t *)0x8017B51C + 2 * selector`, then return the normalized address.

Thus the only mutation is the ordinary-selector halfword-table write; the
sentinel forms are conversion/query operations.

## Callers and target proof

Five generated call instances collapse to three unique guest call sites:

| call PC | arguments/context |
|---|---|
| `0x801656A0` | delay slot sets `selector = -1`, requesting only the normalized byte address |
| `0x80166B1C` | passes an object halfword-derived address and `selector = s3 | 3` |
| `0x80166B3C` | passes the companion address and `selector = s3 | 7` |

The latter two appear twice because the generator emits overlapping copies of
the same `0x801669AC` owner, not because there are additional guest callers.
All callers are in the neighboring SPU control/voice setup region and the
shared table at `0x8017B51C` is subsequently read there as halfword SPU address
units. An exhaustive generated-source search finds no direct `jal` or `j` to
any interior block, so none of the range splits is an alias or separate entry.

The name is deliberately behavioral: the SPU context and address-unit table
are proven, while no unsupported Psy-Q public API identity is asserted.
