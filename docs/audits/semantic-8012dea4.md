# Semantic audit: `0x8012DEA4`

## Result and exact owner

`0x8012DEA4` is the runtime frontend warp renderer now named
`RenderFrontendImageWarp`. Its exact owner is
`[0x8012DEA4, 0x8012E29C)`, `0x3F8` bytes. It creates a 240-byte frame, saves
`ra` and `s0..s6`, and returns through the sole epilogue at
`0x8012E274..0x8012E298`. `0x8012E29C` begins an independent owner.

An exhaustive aligned scan finds exactly one entry caller, no external
interior target, and no duplicate of the complete body.

## ABI and prepared inputs

The recovered ABI is conservatively:

```c
void RenderFrontendImageWarp(
    uint8_t *packet_workspace,
    FrontendWarpState *state);
```

Incoming `a2/a3` are overwritten without use and there is no semantic return.
The state layout used here is:

```text
+0x00  owner/object pointer
+0x08  unsigned phase/scroll halfword (low six bits also used as an angle)
+0x0A  signed angle halfword
+0x0C  signed angle halfword
owner +0x44  pointer to transform block (80 bytes copied/restored)
```

This owner consumes all three artifacts prepared by the sibling audits:

- 26 records of eight warp vertices at `0x801DDE50`;
- a 64-by-eight GPU color-word table at `0x801DE670`;
- the workspace's chain of 175 POLY_G4 and 64 POLY_FT4 packets.

## Transform setup and preservation

The three published image-color bytes at `0x801DEE74/7C/78` are passed to
`SetGlobalPrimitiveRgb`. The routine then follows the pointer at owner `+0x44`
and copies its 80-byte transform to the stack, derives a transform from state angles
through `0x80135990`, and activates/prepares it through `0x80135FE0` before
loading its rotation/translation words into the GTE control registers.

At completion the full saved 80 bytes are copied back. Thus the per-frame warp
transform is temporary; caller-owned transform state is restored exactly.

## Ordering-table and textured-strip selection

The active display/ordering-table context is selected through signed index
`0x80175CB0` and pointer table `0x80194B90`; this routine uses its bucket at
`context+0x10C4`. It preserves that bucket's old tag, replaces the bucket link
with the physical address of the workspace's first G4, and later terminates
the selected packet chain at the previous bucket head.

The signed scroll value is `(state->phase + *(int32_t *)0x801DEE70) >> 6`.
Its low six bits select one of the 64 FT4 strips as the G4 chain's first
textured packet. The wrapped index `(start+24)&63` selects the endpoint, so
exactly 25 cyclic FT4 strips are included. The footer
pointers at workspace `+0x229C/+0x22A0` and the last G4 tag at `+0x1878` are
rewritten so only the selected cyclic FT4 span is appended before the old OT
head. This is packet-chain relinking only; no new packet memory is allocated.

## 25-by-7 projected shaded grid

The main nested loop covers 25 rows by seven columns, exactly the 175
preinitialized POLY_G4 packets. For each cell it reads four corners from two
adjacent rows and columns of the 26-by-eight warp table. Each packed XYZ-like
entry is loaded into GTE V0 and projected with RTPS. The four resulting SXY2
words are stored at the packet's four XY positions:

```text
packet +0x08, +0x10, +0x18, +0x20
```

The scroll-adjusted current and next rows of the 64-by-eight color table supply
the corresponding four `0x38`-prefixed color words at packet
`+0x04/+0x0C/+0x14/+0x1C`. The packet pointer advances by 36 bytes per cell.
Consequently this owner materializes the prepared warp mesh directly into the
G4 chain, while the following FT4 span supplies the textured strip portion of
the effect.

## Caller and callees

The sole direct call is `0x801245FC`. Its owner computes a workspace instance
from an index and base `0x801D7658`, then passes the current frontend warp state
in `a1`.

The three static callees are:

- `SetGlobalPrimitiveRgb` (`0x800EEAD0`);
- transform builder `0x80135990` with the three state angles;
- transform activation/preparation owner `0x80135FE0`.

All projection and packet writes are inline. No caller enters an internal
block.

## Sources checked

- `disc/SCES_028.45`, after its 0x800-byte PS-X EXE header, for exact MIPS,
  GTE operations, caller/interior scanning, and body duplication;
- `generated/SCES_028.45_full_37.c` and `.ranges` for owner continuity;
- audits for both preceding frontend table/packet builders and
  `SetGlobalPrimitiveRgb` for shared-state semantics.
