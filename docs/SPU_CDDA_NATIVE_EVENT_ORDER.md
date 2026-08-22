# SPU/CDDA native event order

## Closed defect

The runtime previously published a completed 588-frame CDDA sector to the SPU
FIFO before rendering the SPU sample boundaries accumulated through that same
native cycle. A catch-up advance could therefore consume new CDDA data for
sample times that preceded the sector deadline, advancing audio by almost one
75 Hz sector.

CD-ROM still performs its command, IRQ, media, and sector work at the existing
native-cycle deadline. The runtime bridge now retains only the synchronous
sector callback payload until the SPU has rendered through that deadline, then
publishes it for the following sample interval. The bridge rejects a second
unpublished sector and any partial/overflowed SPU handoff; no host-time queue,
timer approximation, or compatibility path is involved.

## Deterministic boundary

The adversarial integration test proves that the CDDA FIFO is empty one native
cycle before the first sector deadline, contains exactly 588 frames at the
deadline, and contains 587 after the next 768-cycle SPU boundary. An SPU
snapshot taken at publication restores the exact FIFO phase and continuation
digest. A direct 451,584-cycle advance and irregular chunks of 1, 767, 338,688,
100,000, and 12,128 cycles reach the same device digest.

Verification details are machine-readable in
`clean-aot/runtime/evidence/spu-cdda-native-event-order-v1.json`.

## Remaining boundary

This tranche does not alter CD-ROM command semantics, media layout, timers,
SPU synthesis equations, or host audio presentation. Sub-sample SPU MMIO
timing and physical sound-RAM bus arbitration remain explicitly unwaived.
