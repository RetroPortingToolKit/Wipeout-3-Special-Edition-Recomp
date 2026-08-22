# MDEC behavioral provenance

The implementation is independently written from the pinned public
PSX-SPX documentation checkout:

`psx-spx/psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d`

The consulted document is `docs/macroblockdecodermdec.md`, with the register
map cross-check in `docs/iomap.md`, the MDEC DMA boundary cross-check in
`docs/dmachannels.md`, and the packed direct-display byte contract in
`docs/graphicsprocessingunitgpu.md`. Those documents supplied behavioral vocabulary,
register bit positions, command shapes (including opcodes 4..7 behaving as
opcode 0), table sizes, RLE overflow termination, the distinct normal
inverse-zig-zag and `q_scale=0` natural-order paths, macroblock block order, the
published scale matrix, the 0x20-word MDEC DMA boundary, and the known hardware
uncertainty around exact IDCT/YUV rounding and decompression latency.

The documented decompression routines identify monochrome blocks as Y using
`iq_y`, and colored blocks as Cr/Cb using `iq_uv` followed by Y1..Y4 using
`iq_y`. Table selection in this model follows those roles explicitly.

The same pinned MDEC register description explicitly distinguishes colored
MDEC0 reads (four sequential 8x8 bitmaps requiring software reordering) from
DMA1 output (the DMA path performs that reordering). This is the basis for the
separate CPU-read and DMA-read layouts; no observed emulator output was used.

No emulator, recompiler, GooseStation, DuckStation, title source, generated
code, media, BIOS, or external implementation file was inspected or copied.
The pinned PSX-SPX repository records that its documentation is not itself a
legal clean-room source and does not grant republication rights; this file
records it as a behavioral reference only. The implementation, fixtures, and
tests are new work under the repository clean-room policy.

The fixed Q12 YUV coefficients, signed integer rounding rule, RGB byte order,
bounded output FIFO size, fail-closed empty-read/malformed-input policy,
level-triggered Data-Out request, reset table contents, and deterministic cycle
costs are explicit clean-room contract choices where the reference leaves
behavior unmeasured. They must remain subject to later native-console
validation rather than being treated as title-specific truth. Snapshot wire
encoding and validation are original host-runtime mechanics, not hardware
claims.
