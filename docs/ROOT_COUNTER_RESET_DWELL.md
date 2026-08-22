# Root-counter reset dwell accuracy

Root counters previously counted immediately after target reset or `FFFFh`
overflow. Hardware keeps zero visible for two source-clock ticks after a target
reset and one tick after ordinary overflow. Omitting those ticks advances every
subsequent target/IRQ and accumulates phase error in gameplay timing.

The SoC now consumes the dwell in the selected source-clock domain, including
system/8, dotclock, and HBlank clocks. Quiet arbitrary-precision bulk advances
and callback-driven one-tick advances have identical state. The remaining
dwell is serialized in counter snapshots, validated to the finite range 0..2,
and restored without synthesizing events or IRQs.

Adversarial tests cover target reset, ordinary overflow, deadline-minus-one,
chunk equivalence, snapshot continuation, malformed snapshot rejection, and a
full-width `uint64_t` bulk advance. Mode/current-register write dwell and timer
IRQ pulse width remain explicitly unwaived; this tranche does not guess them.
