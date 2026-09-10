# Semantic audit: `0x801106D0` / `AllocatePolyF4Packet`

## Result

`0x801106D0` is the frontend flat-quad packet allocator now named
`AllocatePolyF4Packet`:

```c
POLY_F4 *AllocatePolyF4Packet(
    int x0, int y0, int x1, int y1,
    int x2, int y2, int x3, int y3,
    int r, int g, int b, int semitrans);
```

It reserves, initializes, and completely populates one 24-byte `POLY_F4`, but
deliberately leaves ordering-table linkage to its caller.

## Exact boundary, ABI, and return

Generated metadata assigns the exact range `[0x801106D0,0x801107EC)`, `0x11C`
bytes. The routine opens a `0x48`-byte frame, has two sequential calls and one
epilogue, returns at `0x801107E4`, and restores the stack in the delay slot at
`0x801107E8`. The next independent owner begins at `0x801107EC`.

The first four arguments are the first two XY pairs. Incoming stack arguments
`+0x10..+0x1C` are the remaining two pairs, `+0x20..+0x28` are RGB, and
`+0x2C` is the semitransparency flag. Coordinates are explicitly truncated to
signed halfwords before their stores; colors are loaded as bytes. The returned
`v0` is the newly allocated packet pointer and every caller uses it.

## Allocation and packet construction

The routine reads `gPrimCursor` at `0x80198620`, retains that old pointer, and
immediately publishes `old + 0x18`. There is no capacity check or rollback.
It then performs:

```c
SetPolyF4(packet);
SetSemiTrans(packet, semitrans);

packet->x0 = (int16_t)x0; packet->y0 = (int16_t)y0;
packet->x1 = (int16_t)x1; packet->y1 = (int16_t)y1;
packet->x2 = (int16_t)x2; packet->y2 = (int16_t)y2;
packet->x3 = (int16_t)x3; packet->y3 = (int16_t)y3;
packet->r0 = (uint8_t)r;
packet->g0 = (uint8_t)g;
packet->b0 = (uint8_t)b;
return packet;
```

Confirmed `SetPolyF4` writes packet length 5 and GP0 command `0x28`;
confirmed `SetSemiTrans` sets or clears command bit 1 according to whether its
second argument is nonzero. Therefore the resulting command is `0x28` or
`0x2A`. The tag's 24-bit link remains untouched, which is why callers splice
the returned packet into an ordering-table bucket afterward.

This packet identity and the exact 24-byte cursor advance support the Psy-Q
type-specific name. It is not a generic coordinate transform like the nearby
quad permutation helpers.

## Complete callers and interior targets

An exhaustive aligned J/JAL scan finds exactly four direct entry calls:

```text
8010043C
8012B774
8012B8B0
8012B964
```

All four construct full-screen or frontend overlay rectangles, retain the
returned packet, and perform a 24-bit ordering-tag splice. They use coordinate
corners from zero through `0x200`, grayscale RGB values, and enable
semitransparency. The `0x8012B7xx..0x8012B9xx` sites also emit adjacent draw
mode packets through `EmitDrawModePacketToOrderingTable`, corroborating their
frontend overlay role.

No external J or JAL targets an interior address in the owner. The generated
corpus emits the same four calls exactly once, so there is no overlay duplicate.
The only outgoing calls are `SetPolyF4` at `0x8011075C` and `SetSemiTrans` at
`0x80110768`.

## Sources checked

- `disc/SCES_028.45`, excluding its PS-X EXE header, for exact instructions,
  caller arguments, returned-pointer use, and exhaustive target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated call emissions for the no-duplication result;
- `docs/audits/semantic-80160254.md`,
  `docs/audits/semantic-8015fd4c.md`, and
  `docs/audits/semantic-801107ec.md` for independently proven packet-helper,
  semitransparency, cursor, and ordering-table semantics.
