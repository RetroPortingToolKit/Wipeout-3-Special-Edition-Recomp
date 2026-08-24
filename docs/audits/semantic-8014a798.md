# Semantic audit: `0x8014A798` / `EmitGouraudQuadPacket`

## Result and exact boundary

`0x8014A798` is `EmitGouraudQuadPacket`, a 36-byte PlayStation `POLY_G4`
allocator/filler. The generated range is `[0x8014A798,0x8014A8DC)`, exactly
`0x144` bytes and three blocks. It returns at `0x8014A8D4`, releases its
32-byte save area in the delay slot at `0x8014A8D8`, and the next independent
owner begins at `0x8014A8DC`.

## Full ABI

The first four signed coordinates arrive in `a0..a3`; all remaining values
are 32-bit ABI stack slots, narrowed by signed-halfword or byte loads:

```c
void *EmitGouraudQuadPacket(
    int16_t x0, int16_t y0, int16_t x1, int16_t y1,
    int16_t x2, int16_t y2, int16_t x3, int16_t y3,
    uint8_t r0, uint8_t g0, uint8_t b0,
    uint8_t r1, uint8_t g1, uint8_t b1,
    uint8_t r2, uint8_t g2, uint8_t b2,
    uint8_t r3, uint8_t g3, uint8_t b3,
    int16_t semitransparent);
```

The returned pointer is the old `gPrimCursor`, hence the newly constructed
packet. The routine does not link the packet into an ordering table.

## Allocation and packet layout

The owner reads the old packet cursors at `0x80198620` (`gPrimCursor`) and
`0x80176348`, advances both by exactly 36 bytes, and fills the packet selected
by `gPrimCursor`:

| Offset | Field |
|---:|---|
| `+0..+3` | caller/allocator tag storage; payload length byte set to 8 |
| `+4..+6` | `r0,g0,b0` |
| `+7` | GP0 command `0x38`, or `0x3A` when `semitransparent != 0` |
| `+8,+10` | `x0,y0` |
| `+12..+14` | `r1,g1,b1` |
| `+16,+18` | `x1,y1` |
| `+20..+22` | `r2,g2,b2` |
| `+24,+26` | `x2,y2` |
| `+28..+30` | `r3,g3,b3` |
| `+32,+34` | `x3,y3` |

This is the canonical untextured Gouraud-shaded quad packet layout. The final
flag changes only the semitransparency command bit; it does not affect geometry
or color values.

## Retained callers, overlay duplicates, and interior labels

The executable contains exactly four unique direct calls, all in owner
`0x80148128`, at `0x801483A8`, `0x80148490`, `0x80148578`, and `0x80148660`.
They construct four quad packets through the same ABI. The generator reports
942 `jal` emissions because those same retained call shapes are replicated
through generated overlay variants; they are not 942 distinct executable
call sites.

`0x8014A858` merely changes the command from `0x38` to `0x3A` on the true
semitransparent branch, and `0x8014A85C` is the common packet-fill continuation.
An exhaustive executable encoding scan finds no call or jump to either
interior address. They are not callable aliases or independent owners, and
there is no duplicate of the function body.
