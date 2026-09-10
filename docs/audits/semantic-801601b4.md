# Semantic audit: `0x801601B4` / `SetDrawMode`

## Result

`0x801601B4` is the PsyQ-compatible GPU packet builder `SetDrawMode`:

```c
void SetDrawMode(DR_MODE *packet,
                 int dfe,
                 int dtd,
                 int tpage,
                 const RECT *texture_window);
```

It builds a two-GP0-word `DR_MODE` packet containing the E1 draw-mode command
and either an E2 texture-window command or a zero GPU NOP.

## Exact boundary, leaf, and return

The owner is exactly `[0x801601B4,0x80160248)`, `0x94` bytes, matching generated
metadata. It is frameless, saves no registers, and makes no calls. All paths
join at the sole return at `0x80160240`, with a NOP delay slot at `0x80160244`.
Padding begins at `0x80160248`; the next independent helper starts afterward.

Arguments `a0..a3` and the fifth o32 ABI argument at caller `sp+0x10` are all
consumed. No path deliberately returns the packet pointer or another value;
callers ignore `v0`, so the semantic return is void.

## Packet layout

The destination is the standard 12-byte `DR_MODE`/ordering-table packet:

```text
packet +0x00  24-bit OT link plus length byte at +3
packet +0x04  GP0 E1 draw-mode command
packet +0x08  GP0 E2 texture-window command, or zero
```

The routine writes byte `packet+3 = 2`, preserving the low 24-bit OT link. It
does not link the packet into an ordering table; callers do that separately.

## E1 draw-mode command

The first command word is:

```c
0xE1000000 |
(tpage & 0x09FF) |
(dtd ? 0x0200 : 0) |
(dfe ? 0x0400 : 0)
```

This is the PlayStation GPU draw-mode command. `dtd` enables texture
dithering, `dfe` enables drawing to the display area, and only the low TPAGE
bits accepted by the command are retained. The routine treats both Boolean
arguments by zero/nonzero truth rather than truncating them.

## E2 texture-window command

When `texture_window == NULL`, `packet+8` is zero. GP0 word zero is a NOP, so
the packet retains its fixed two-word length without changing the texture
window.

For a nonnull standard `RECT { int16 x,y,w,h; }`, the second word is packed as:

```c
0xE2000000 |
(((uint8_t)x >> 3) << 10) |
(((uint8_t)y >> 3) << 15) |
((((-w) & 0x00FF) >> 3)) |
((((-h) << 2) & 0x03E0));
```

This expresses the GPU's five-bit X/Y offsets and inverted five-bit width/
height masks in eight-pixel units. X and Y are loaded as low unsigned bytes;
W and H are signed halfwords before negation/masking. Values outside the GPU
field widths wrap through those masks rather than being validated.

The function writes only the three packet words. It touches no GPU registers,
ordering-table globals, or renderer state directly.

## Direct callers and interior targets

An exhaustive aligned scan of the retained EXE finds exactly eleven calls:

```text
80109440 8010998C 80109AF8 8010A1B4 8010AB94 8010D220
8010D7A4 8011083C 8013B104 8013B13C 80156E64
```

The first ten visibly construct or fetch TPAGE through neighboring SDK helper
`0x80160274`, pass `dfe = 1`, supply runtime `dtd`, and pass a null texture
window. The final call also targets a 12-byte packet slot and inherits its
remaining mode arguments from the surrounding setup path. None uses `v0`
afterward.

No external direct `j` or `jal` targets an interior instruction. The only
interior direct jump is from `0x80160234` to the common return block at
`0x80160240`, entirely within this owner.

## Identity proof

The exact PsyQ name is supported by the standard five-argument ABI, `DR_MODE`
packet length/layout, E1 command bit assignments, E2 RECT packing, and the
neighboring TPAGE helper usage. This is not merely a similar game-owned packet
writer.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instructions and exhaustive caller/interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0x94 boundary;
- all eleven direct caller windows and direct comparison with the PlayStation
  GPU E1/E2 packet encoding embodied by the instructions.
