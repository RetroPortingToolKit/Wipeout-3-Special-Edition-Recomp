# Disabled SPU reverb IRQ visibility

## Closed defect

When reverb writes were disabled, the core still evaluated all four COMB taps
and both all-pass stages. The pinned hardware description specifies a pruned
read graph instead:

- `vAPF2=0` reads only `[mLAPF2-dAPF2]` or its right-channel counterpart.
- nonzero `vAPF2` with `vAPF1=0` adds the APF1 delayed read but not COMB.
- COMB reads occur only when both APF coefficients are nonzero.

The extra reads did not necessarily change zero-valued wet audio, but each was
visible to the sound-RAM IRQ comparator and could assert IRQ9 from an address
the hardware did not access.

## Native-edge and replay proof

The adversarial vector assigns distinct IRQ-addressable RAM units to COMB,
APF1, and APF2. Across complete alternating left/right native pairs it proves
that zero `vAPF2` suppresses APF1/COMB IRQs and zero `vAPF1` suppresses COMB.
It then enables both coefficients, snapshots before the next pair, and proves
that original and restored devices each expose exactly one COMB IRQ, identical
audio, identical pending state, and identical complete deterministic digest.

The fixed two-sample stereo FNV-1a digest is `12161962213042174405`.

## Remaining boundaries

The exact enabled-reverb silicon evaluation order, unknown reverb IRQ source
subset, third voice RAM read, and physical sound-RAM arbitration remain
unclaimed.
