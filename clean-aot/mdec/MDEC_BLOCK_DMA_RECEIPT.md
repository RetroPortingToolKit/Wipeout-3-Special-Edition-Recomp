# MDEC colored-output block/DMA receipt

Scope: colored 15-bit and 24-bit macroblock output ordering.

Pinned evidence: `psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d`,
`docs/macroblockdecodermdec.md`, MDEC Data/Response Register and DMA sections.
The register stream is four 8x8 bitmaps and requires reordering when read by
the CPU; DMA1 performs that reordering for RAM output.

Prior defect: the device emitted a pre-reordered 16x16 raster through MDEC0
and divided that raster into four linear quarters for Current Block. This
made CPU-visible data/status incorrect and conflated register and DMA paths.

Correction:

- MDEC0 now emits complete Y1, Y2, Y3, Y4 8x8 blocks.
- DMA1 converts the same bounded macroblock to a row-major 16x16 RAM stream.
- 15-bit and 24-bit pixel byte packing is unchanged.
- the DMA representation epoch is snapshot state (wire version 3), validated
  fail-closed and cleared exactly when the output macroblock drains.

Deterministic proof: the asymmetric four-luma fixture distinguishes both axes,
checks raw block continuity, checks DMA boundaries at x=8/y=8, and snapshots
after a hostile 17-word DMA prefix. Original/restored tails and final digests
must match exactly. The complete existing randomized, FIFO, RLE, IDCT, color,
cycle, and hostile-snapshot suite remains required.

Verification on 2026-08-22:

- Windows GCC 16.1.0 strict-warning Debug build and CTest: pass.
- Linux GCC 15.2.0 ASan+UBSan (`detect_leaks=1`): pass.
- Linux Clang 21.1.8 ASan+UBSan (`detect_leaks=1`): pass.
