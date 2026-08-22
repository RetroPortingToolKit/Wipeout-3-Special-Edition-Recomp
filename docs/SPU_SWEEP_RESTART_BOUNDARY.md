# SPU sweep restart boundary

## Closed defect

Writing new voice or main-volume sweep control retained the previous sweep's
internal divider. The pinned hardware description defines the write as starting
a sweep from the current volume. Retaining a half-consumed divider could make a
replacement pan or fade step one native sample too early.

Every sweep-mode write now starts a fresh divider phase while preserving the
current signed volume. Fixed-volume writes retain their documented deferred
44.1 kHz application, and a later sweep still cancels an unapplied fixed write.

## Native-boundary proof

The adversarial vector uses a two-tick linear sweep. It consumes exactly half a
divider period, rewrites the same sweep, and snapshots before another native
edge. Original and restored devices hold the current volume at `0x2000` on the
first edge and produce identical audio and complete device digests. The second
edge performs the first step, proving that the rewritten sweep receives a full
new divider period.

The same `write_volume` path serves both channels of every voice and both main
volume channels, so the boundary is shared without a compatibility special
case.

## Remaining boundaries

Sub-sample MMIO ordering, the unresolved third voice RAM read, physical
sound-RAM arbitration, and exact wet-reverb silicon rounding remain unclaimed.
