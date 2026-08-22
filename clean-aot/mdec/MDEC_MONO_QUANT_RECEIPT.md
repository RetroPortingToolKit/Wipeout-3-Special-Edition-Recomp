# MDEC monochrome quantization receipt

Scope: quantization-table selection for 4-bit and 8-bit decode commands.

Pinned evidence: `psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d`,
`docs/macroblockdecodermdec.md`, `decode_monochrome_macroblock` and
`decode_colored_macroblock`. Monochrome invokes `rl_decode_block(Yblk,src,iq_y)`;
colored decode uses `iq_uv` only for Cr/Cb and `iq_y` for Y1..Y4.

Prior defect: table selection used `decode_block < 2` without considering
output depth. A monochrome decode has block index zero, so it incorrectly used
the chroma table for both DC and AC coefficients. Reset initialized both tables
equally, allowing ordinary vectors to miss the defect.

Correction: chroma selection now requires both a colored 15/24-bit command and
Cr/Cb block index. Every monochrome coefficient uses luminance quantization.
No timing, RLE, IDCT, packing, FIFO, or DMA behavior changed.

Deterministic proof uses identical luminance tables with hostile chroma values
1 and 251. Exact 8-bit output must remain identical. A multiword AC stream is
split after its first word, snapshotted mid-block, restored, and completed in
both devices; output and final digests must match the unsplit reference.

Verification on 2026-08-22:

- Windows GCC 16.1.0 strict-warning Debug build and CTest: pass.
- Linux GCC 15.2.0 ASan+UBSan with leak detection: pass.
- Linux Clang 21.1.8 ASan+UBSan with leak detection: pass.
