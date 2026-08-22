# SPU reverb sound-RAM transfer gate

## Closed defect

Reverb work-area writes previously depended only on `SPUCNT` reverb enable.
They mutated sound RAM even while sound-RAM transfer control remained at its
reset value. The pinned hardware requirements state that reverb remains off
until `1F801DAC` is initialized and prescribe normal transfer type `0004h`.

The reverb unit now writes its SAME/DIFF/APF work-area stages only when both
`SPUCNT` reverb enable and transfer control `0004h` are active. A closed gate
does not freeze device time: COMB/APF reads, wet-tail output, IRQ-visible reads,
alternating left/right cadence, and work-area cursor advancement continue as
specified for disabled writes.

## Native-boundary and replay proof

The adversarial test routes a CD impulse to reverb with `SPUCNT` enabled while
transfer control is zero and proves the target work-area halfword remains
unchanged. It snapshots that closed-gate state, restores it into a fresh SPU,
then programs `0004h` in both instances. Across 40 native samples, covering
both alternating sides and the 39-tap input FIR boundary, both devices perform
the write, produce identical audio, and converge to the same complete
deterministic digest.

Existing enabled-reverb vectors now explicitly initialize normal transfer
control. Disabled-master wet-tail tests continue to prove reads and output
without SAME/DIFF writes.

## Remaining boundaries

Exact silicon reverb evaluation/polyphase rounding, the unresolved third voice
RAM read, and physical sound-RAM slot arbitration remain unclaimed.
