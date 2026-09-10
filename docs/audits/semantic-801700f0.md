# Semantic audit: guest `0x801700F0`

## Result

`0x801700F0` is the fast, unclipped `POLY_FT4` packet-emission continuation of
the block beginning at `0x801700C4`. It is **not a normal ABI function entry**.
The only statically demonstrated predecessor is the taken branch at
`0x801700E0`; no direct `j`/`jal` instruction or aligned absolute code pointer
to `0x801700F0` exists in the main executable.

Recommended semantic label, if block labels become first-class in the symbol
format:

```text
EmitTexturedQuadFastPath  // confidence: high (block), not a callable function
```

No `symbols.toml` function was added because its current `[[func]]` model would
misrepresent this internal branch target as an independently callable entry.

## Effective register contract

This contract is reconstructed across the predecessor delay slot and the body;
it is not the o32 MIPS calling convention.

| Register | Meaning at entry | Evidence |
|---|---|---|
| `$a0` | vertex 0 unsigned depth | `lhu 12($t6)` in the predecessor branch delay slot |
| `$t6..$t9` | four projected-vertex cursors | depth/flags read at `+12/+14`; packed screen XY read at `+8`; each cursor replaced by its `+8` link/value |
| `$v0` | packed texture-coordinate/CLUT/TPAGE template | three words read at `+0/+4/+8` and distributed into FT4 attribute halfwords |
| `$t4` | face/material record | unaligned word sourced at `+18` supplies the packet color/command field |
| `$s2` | ordering-table base | indexed by the computed depth bucket |
| `$s3` | output primitive cursor | receives one 40-byte packet and advances by `0x28` |
| `$ra` | enclosing renderer continuation | returned to directly on success or trivial rejection |

The routine has no scalar return value. On the fast visible path it writes one
packet, links it into the ordering table, advances `$s3` by 40, and advances the
four vertex cursors. Scratch registers, LO, and HI are clobbered. If near-plane
handling is required it transfers to `0x80170B5C`, which saves the live custom
register context before performing the slower clip path.

## Control flow

1. `0x801700C4` loads the signed clip/outcode halfword at `vertex + 14` for all
   four vertices and ANDs them together.
2. If the common mask is nonzero, all four vertices share an outside plane and
   the predecessor returns immediately (trivial reject).
3. Otherwise its delay slot loads vertex 0 depth and branches to `0x801700F0`.
4. `0x801700F0` loads the other three unsigned depths, sums all four, and in the
   branch delay slot computes `sum >> 7`. This is the ordering-table bucket
   index after the later `<< 2` byte scaling.
5. Each depth is tested against `125`. If any is below the threshold, control
   transfers to the shared slow clipper at `0x80170B5C`.
6. Otherwise the block constructs and OT-links a 40-byte textured quad, then
   returns through the inherited `$ra`.

The branch at step 5 is sometimes described as a clip-code test in generated
profiling notes, but the instructions at `0x80170108..0x80170114` operate on the
four `+12` depth halfwords. The separate common outcode test is in the
`0x801700C4` predecessor. Keeping those two predicates distinct matters for a
faithful rewrite.

## Packet reconstruction

The destination stride (`0x28`) and field stores match the standard 40-byte
PlayStation `POLY_FT4` layout:

- projected XY words go to packet offsets `+8`, `+16`, `+24`, and `+32`;
- packed UV/CLUT/TPAGE halfwords go to `+12`, `+14`, `+20`, `+22`, `+28`, and
  `+36`;
- the color/GPU-command word is copied through the unaligned load/store pair
  involving `$t4 + 18` and packet `+6`;
- the unaligned stores at packet/OT offset `+2`, together with `$s3 << 8`,
  update the 24-bit ordering-table links while preserving the packet-length
  byte.

This is also consistent with the four projected vertices, the FT4-sized cursor
increment, and the shared near-clip fallback.

## Confidence and remaining uncertainty

- **High confidence:** internal continuation rather than normal function;
  trivial-reject predecessor; four depths and threshold; OT bucket calculation;
  40-byte textured-quad emission; output cursor advance; slow-clip transfer.
- **Medium confidence:** source-level type names for the projected-vertex and
  face/material records. Their accessed fields are clear, but canonical game
  typedef names are not present in the current corpus.
- **Not justified:** assigning a standalone C signature or a `[[func]]` symbol
  to `0x801700F0`. A faithful decompilation should merge `0x801700C4` and
  `0x801700F0`, or represent the latter as a block label inside a larger custom-
  calling-convention renderer primitive.

## Authoritative evidence inspected

- `generated/SCES_028.45_full_140.c`, guest instructions
  `0x801700C4..0x801701B0` and slow target `0x80170B5C`.
- `generated/SCES_028.45_dispatch.c`, which shows the generated split dispatch
  entry but does not prove a source-level function boundary.
- `generated/SCES_028.45_full.ranges`, whose independent `F` entries explain
  the current generated split but likewise do not establish ABI callability.
- `disc/SCES_028.45`, scanned for direct `j`/`jal` targets and aligned absolute
  pointers to `0x801700F0`; none were found.
