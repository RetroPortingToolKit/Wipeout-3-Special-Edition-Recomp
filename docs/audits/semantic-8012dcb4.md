# Semantic audit: `0x8012DCB4`

## Result and exact owner

`0x8012DCB4` is the packet-pool initializer now named
`InitializeFrontendImageWarpPacketChains`. It owns exactly
`[0x8012DCB4, 0x8012DEA4)`, `0x1F0` bytes, matching the generated range. The
function creates a 64-byte frame, saves `ra`, `fp`, and `s0..s7`, and returns
through the sole epilogue at `0x8012DE58..0x8012DEA0`. `0x8012DEA4` begins an
independent owner.

An exhaustive aligned scan finds one entry caller, no external interior
target, and only one copy of the complete body.

## ABI

The recovered ABI is:

```c
void InitializeFrontendImageWarpPacketChains(
    uint8_t *workspace,
    const TextureDescriptor *texture,
    uint32_t ignored_a2,
    uint32_t ignored_a3);
```

The texture fields used are the halfwords at `+2` and `+4`, independently
proved as TPAGE and CLUT in `LoadFrontendTexture`. Incoming `a2/a3` are never
read. The routine has no semantic return value.

It also snapshots signed layout halfword `0x801D74A2` to word `0x801DEE70` for
the adjacent warp system; that value does not affect packet initialization in
this owner.

## Shaded-quad chain

The first nested loop creates 25 rows of seven packets: **175 `POLY_G4`
packets** total. Each packet is 36 bytes. The helper at `0x80166E90` sets tag
length byte `+3 = 8` and command byte `+7 = 0x38`, the canonical shaded-quad
shape. This routine then writes the tag word as:

```text
0x08000000 | physical_address(next_packet)
```

Packets occupy `workspace+0x0000..workspace+0x189B` and form one sequential
chain. Color and XY payload fields are deliberately left for later warp-table
population.

## Textured strip chain

The second loop creates **64 `POLY_FT4` packets**, 40 bytes each, beginning at
`workspace+0x189C`. Each is initialized through canonical `SetPolyFT4`, linked
to the following 40-byte slot with a nine-word tag, and assigned neutral
`RGB=(0x80,0x80,0x80)`. Descriptor TPAGE is stored at packet `+0x16` and CLUT
at `+0x0E`; XY halfwords are not set here.

The packet index is transformed through `(63-index)<<4`. Its low byte supplies
the two V boundaries, separated by 15. U boundaries are selected from byte
`0x801D74AB` and the high atlas bits:

- mode `1`: `u_left=0`, `u_right=127`;
- mode `2`: normally `u_left=63`, `u_right=0`, with high-page strips falling
  through to the generic selection;
- other/generic: `u_right=63`, while `u_left` is `0` or `64` according to bit
  `0x100` of the transformed index.

The four packed UV halfwords at `+0x0C/+0x14/+0x1C/+0x24` describe the strip's
top/bottom and left/right corners. The complete FT4 pool occupies
`workspace+0x189C..workspace+0x229B`.

The last G4 tag is explicitly made to point to the first FT4, so both pools
form one chain. Footer words publish:

```text
workspace + 0x229C = workspace + 0x2274  // last FT4
workspace + 0x22A0 = workspace + 0x189C  // first FT4
```

The final FT4's initially written next link points at the footer boundary;
later runtime code owns termination/submission policy.

## Caller and callees

The sole direct caller is `0x80110988` in `PrepareFrontendImageResource`, which
passes fixed workspace `0x801D7658` and the texture descriptor built earlier
in that pipeline. Its option byte is passed in ignored `a2`.

There are two static callees: the four-instruction `SetPolyG4`-shaped helper at
`0x80166E90`, invoked by the 175-packet loop, and confirmed Psy-Q `SetPolyFT4`
at `0x8015F7A4`, invoked by the 64-packet loop. No call or jump enters an
internal block.

## Sources checked

- `disc/SCES_028.45`, after the 0x800-byte PS-X EXE header, for exact MIPS,
  exhaustive caller/interior scanning, and body duplication;
- `generated/SCES_028.45_full_37.c` and `.ranges` for owner continuity;
- audits for `PrepareFrontendImageResource`, `LoadFrontendTexture`,
  `PrepareFrontendImageWarpTables`, and `SetPolyFT4` for surrounding layout and
  descriptor semantics.
