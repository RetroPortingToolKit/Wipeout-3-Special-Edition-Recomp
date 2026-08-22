# Clean AOT runtime

This component is the deterministic machine seam consumed by generated native
regions. It is not an emulator loop and contains no decoder, interpreter, JIT,
window, host audio backend, or title-specific policy.

The central invariant is that raw CPU-retirement time and native device time
are distinct. A multiplier of `N/D` permits `N/D` times as much recompiled CPU
work while timers, CD-ROM, SIO, SPU, and media remain on native time. Every
cycle-sensitive callback receives the exact raw issue cycle by value; the
runtime converts it with an exact carried rational clock before touching MMIO.

Unknown addresses, unsupported widths, time reversal, malformed configuration,
re-entry, and device rejection fail closed. The special PS1 cache-control
register at `0xFFFE0130` is handled before the ordinary direct-segment bus,
because it is outside the RAM/scratchpad/MMIO/BIOS physical window.

This first integration slice owns:

- immutable caller-provided BIOS storage and accepted memory semantics;
- memory-control, interrupt, DMA, root-counter, SIO0, CD-ROM, GPU, MDEC, and SPU
  register routing;
- DMA endpoint wiring for MDEC, GPU, CD-ROM, SPU, OTC, and RAM;
- exact native-time advancement and level-to-latched interrupt propagation;
- cache-control and deterministic SIO1 bootstrap registers.

Host presentation, media-file parsing, scanout/raster draining, frame pacing,
audio-device queues, and product policy are separate components.
