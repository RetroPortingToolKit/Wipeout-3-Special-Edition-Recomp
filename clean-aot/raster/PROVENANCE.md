# Raster provenance and behavioral evidence

This component contains an independently written implementation. It does not
vendor third-party emulator source, headers, generated tables, screenshots, or
binary assets. The implementation uses the clean GPU event API and the C++
standard library only.

This is a source-isolation record, not a legal opinion or a claim that the
reviewer observed every prior authoring session. The pre-review tranche arrived
without an authorship log. During the final review, the coverage, interpolation,
line, texture, mask, and transfer paths were either rewritten or checked against
behavioral evidence, and a source scan found no third-party code identifiers,
copyright notices, or non-project include dependencies in implementation files.

## Behavioral evidence

Evidence was consulted for behavior, not as implementation text:

- `JaCzekanski/ps1-tests`, commit
  `f727802fead11f1daa7549285548392ef87749cb` (MIT), including the real-console
  `vram.png` captures and command-generating test programs under `gpu/lines`,
  `gpu/triangle`, `gpu/uv-interpolation`, and
  `gpu/vram-to-vram-overlap`.
- `psx-spx/psx-spx.github.io`, commit
  `b791ca25b7833b356b28917383f231078762a75d`, for public descriptions of GP0
  command fields and edge cases. No prose, diagrams, or source were copied.
- `stenzek/duckstation`, commit
  `5f3db8ed0f101beea3f27063e5005faf3f065cb6`, was consulted during review only
  as a second behavioral oracle for candidate line and overlapping-copy edge
  cases. No implementation text, names, comments, constants, or control-flow
  structure were copied. Accepted behavior was independently reconstructed and
  checked against the hardware captures above.

The PSX-SPX repository itself warns that its historical source material was not
produced by a formal legal clean-room process. Consequently, this component can
accurately claim independently authored source and no copied emulator code, but
must not be represented as having a formally isolated legal clean-room pedigree
without a separate provenance/legal review.

## Hardware-derived regression contracts

The tests encode small numeric facts derived from the pinned real-console VRAM
captures; the images themselves are not copied into this repository:

- the complete Gouraud triangle/dither scene hashes to
  `0xd8867d7d262ae5bf` after converting displayed RGB5 values back to VRAM words;
- the complete UV/color interpolation and quad-seam scene hashes to
  `0xc8124b3a9ec213e6`;
- the non-label interiors of all 147 overlap-copy cases hash to
  `0x06080cd77ae85263`;
- selected shallow, reversed, zero-length, Gouraud, and semi-transparent
  polyline pixels lock the observed fixed-point line behavior.

Those contracts are deterministic FNV-1a hashes over little-endian 16-bit VRAM
words. They establish output equivalence for the covered commands; they do not
establish GPU timing equivalence.
