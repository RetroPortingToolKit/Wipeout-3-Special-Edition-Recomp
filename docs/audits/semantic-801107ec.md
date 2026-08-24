# Semantic audit: `0x801107EC` / `EmitDrawModePacketToOrderingTable`

## Result

`0x801107EC` is the GPU-state packet emitter now named
`EmitDrawModePacketToOrderingTable`:

```c
void EmitDrawModePacketToOrderingTable(int dtd, int abr, int32_t depth);
```

It reserves one 12-byte `DR_MODE`, constructs a draw-mode command with drawing
to display enabled, and prepends it to the current ordering-table bucket at
signed `depth >> 6`.

## Exact boundary and ABI

The complete owner is `[0x801107EC,0x80110894)`, exactly `0xA8` bytes, matching
the generated range table. It opens a 0x28-byte frame, saves `ra/s0/s1/s2`, and
all execution reaches the single restore/return at `0x8011087C..0x80110890`.
The preceding descriptor builder returns at `0x801107E4`; the next independent
resource loader creates its own frame at `0x80110894`.

The ABI is:

- `a0`: DTD flag forwarded to `SetDrawMode`;
- `a1`: ABR value, truncated to eight bits and then canonically masked to two
  bits by `GetTPage`;
- `a2`: signed ordering depth, converted to a bucket index by arithmetic
  right shift six;
- no semantic return. The routine does not establish a purposeful result and
  all eight callers ignore `v0`.

## Packet construction

At entry the function reads `gPrimCursor` at `0x80198620`, saves the old value
as the packet pointer, and publishes `old + 12` immediately. It then calls:

```c
uint16_t tpage = GetTPage(0, abr & 0xFF, 0, 0);
SetDrawMode(packet,
            1,       // DFE: drawing to display area enabled
            dtd,     // DTD: caller-selected dithering
            tpage,
            NULL);   // no texture window
```

With texture depth, x, and y all zero, the proven `GetTPage` formula reduces
to `(abr & 3) << 5`. `SetDrawMode` creates the standard two-word payload:
packet tag length 2, E1 draw-mode command carrying DFE/DTD/TPAGE, and a zero
second command because the texture-window pointer is null. Together with the
four-byte ordering tag this is exactly the reserved 12-byte `DR_MODE`.

The cursor is advanced before either callee. There is no allocation check or
rollback path; packet arena capacity is a caller/render-loop invariant.

## Ordering-table link

The active ordering-table base is selected as:

```text
ot = *(uint32_t **)(0x8018AE88 + *(int16_t *)0x80175CB0 * 4)
bucket = &ot[(int32_t)depth >> 6]
```

The signed `lh`, arithmetic depth shift, and word scaling are exact. The final
`lwl/swl` sequence performs the standard PSX 24-bit ordering-tag splice:

1. copy the bucket's prior 24-bit link into the new packet tag;
2. publish the new packet's low 24-bit address into the bucket link;
3. preserve the tag-length byte written by `SetDrawMode`.

The packet is therefore prepended to that bucket. No GPU command is submitted
directly; later OT traversal consumes it.

## State and side effects

The complete externally visible effects are:

- `gPrimCursor` advances by 12;
- 12 bytes at the old cursor become a linked `DR_MODE` packet;
- one active ordering-table bucket's 24-bit link changes to the packet;
- the packet's link receives the bucket's previous head.

No archive, heap, transform, racer, or hardware register state is touched.
The two callees are pure packet/formula helpers: `GetTPage` and `SetDrawMode`.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly eight direct calls:

```text
800F62BC
8012B78C 8012B7CC 8012B86C 8012B8F4 8012B920 8012B9A8
8013E320
```

All eight target `0x801107EC`. There are no direct `j` or `jal` targets to any
interior address in `[0x801107F0,0x80110894)`.

Callers pass DTD either zero or the shared mode word at `0x8017330C`, ABR values
zero, one, or two, and depth values commonly zero or taken from render state.
They proceed with further packet construction and never consume `v0`, matching
the state-emitter contract.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the exact `0xA8` owner boundary;
- `docs/audits/semantic-80160274.md` for the canonical `GetTPage` formula;
- `docs/audits/semantic-801601b4.md` for `DR_MODE` layout and `SetDrawMode`
  argument/effect semantics;
- existing renderer audits for `gPrimCursor`, active-OT selection, and PSX
  24-bit ordering-tag linking conventions.
