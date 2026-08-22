# SPU exponential-decrease phase semantics

## Closed defect

The voice-volume sweep treated phase inversion as active during exponential
decrease. A positive current volume therefore received a positive exponential
step and was clamped immediately to zero on the first native sweep edge.

The pinned SPU hardware description states that the phase bit has no effect in
exponential-decrease mode. The core now masks phase inversion only for that
mode. Linear decrease, inverted linear decrease, exponential increase, and the
all-ones WipEout 3 freeze retain their existing signed behavior.

## Native-boundary proof

The adversarial test starts matched looping voices at the same positive fixed
volume, then programs exponential-decrease sweeps that differ only in the
phase bit. Their stereo output remains identical across exactly 441 native
44.1 kHz edges. The test snapshots the inverted form after edge 73, restores
it into a fresh device, and proves identical remaining audio and complete
device digest through edge 441.

The fixed tail-audio FNV-1a digest is
`3042691447776982565`. GCC and Clang sanitizer builds execute the same vector.

## Remaining boundaries

This result does not infer sub-sample MMIO timing, the unresolved third voice
RAM read, physical sound-RAM arbitration, or exact wet-reverb silicon rounding.
