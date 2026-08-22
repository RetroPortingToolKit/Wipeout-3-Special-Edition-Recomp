# SPU noise native-edge phase

## Closed defect

The noise divider and LFSR were clocked before the current native sample read
the generator level. That exposed the newly generated level one 44,100 Hz
interval too early to every noise-routed voice.

The pinned SPU noise recurrence defines one wait cycle followed by the timer
subtraction, optional LFSR shift, and up to two timer reloads. Independent
Mednafen and DuckStation implementations provide matching ordering
cross-evidence: voices consume the held level first and the global generator
advances afterward. The core now returns the held level for the current sample
and clocks the documented recurrence for the next sample.

## Native-edge and replay proof

The adversarial vector runs shift 15/step 7, where every native interval needs
both documented timer reloads, for one exact 441-sample 100 Hz update. A second
machine snapshots after sample 73, changes from shift 15/step 7 to shift
14/step 4 exactly at the next native boundary, and is replayed from that
snapshot. Tail audio and the complete deterministic device digest must match.

The fixed FNV-1a stereo audio digests are `3278948218615534505` for the
maximum-rate path and `17969122608217034753` for the boundary rate-change path.

## Remaining boundaries

The generator's power-on seed is not specified by the pinned behavioral text
and remains unchanged. Sub-sample MMIO ordering and physical SPU clock phase
relative to CPU writes remain separate acceptance boundaries.
