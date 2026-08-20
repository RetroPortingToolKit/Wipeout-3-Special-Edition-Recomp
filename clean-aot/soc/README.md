# Deterministic SoC slice

This directory is a standalone C++20 implementation of the architectural
interrupt controller, seven-channel DMA engine, and three root counters used
by a static-recompiled target. It is an original clean-room implementation.
Behavioral references were used as documentation only; no external execution
engine, dynamic code generation, or compatibility execution path is present
here.

The public header exposes bounded function-pointer callbacks for RAM and
devices. DMA never knows what a graphics, disc, sound, or decompression device
does. A callback receives one masked, aligned 24-bit word address and must
return an explicit result. Null callbacks, callback rejection, and malformed
results become deterministic channel errors rather than undefined behavior.

The interrupt controller implements latched status, writable mask, explicit
acknowledge, zero-to-clear status writes, and the CPU-line predicate. DMA uses
the architectural seven-channel priority-control nibbles and interrupt
control/flag fields, including masked completion flags, write-one-to-clear
acknowledgement, and a latched bus-error condition. Manual, request, and
linked-list synchronization are separate paths. A linked list has bounded node
and word counts, aligned addresses, a visited-node loop guard, an end marker,
and no host pointer traversal. Channel six provides the ordering-table pointer
writer and never requires a fake device callback.

Request-mode channels expose an explicit request line. It is asserted by
default for a deterministic standalone setup and can be deasserted by a later
device model without changing DMA ownership or callback types.

Root counters keep fractional native SoC/system-clock time as reduced integer
rationals. System-clock and system-divided-by-eight sources consume `CycleDelta`
values exactly; dot-clock and blanking sources consume explicit edge events.
Target and overflow transitions are observable through typed callbacks and can
raise the corresponding interrupt source. Repeat, one-shot, and toggle modes
are state in the snapshot, not wall-clock behavior. A mode write resets the
count and re-arms the counter IRQ state. Synchronization modes provide
deterministic compressed transitions for pause during the gate, reset at the
gate, reset and pause outside the gate, or pause until the first gate and then
free-run. The reached-target and reached-overflow status bits are
read-to-clear.

## Integration and accuracy boundary

- This slice has no MMIO address decoder, partial-width access, byte-lane, or
  unaligned-access behavior. Its typed API accepts 32-bit interrupt and DMA
  payloads and 16-bit counter payloads. Integration must route supported
  accesses and reject unsupported ones before calling this API.
- `DmaRunReport::cpu_cycles` is deterministic scheduler accounting: one
  baseline unit for each transferred word or linked-list header, plus modeled
  chopping windows. It is not a measurement or claim of exact bus or device
  latency. Graphics, disc, sound, and decompression readiness and latency
  belong to later device integration.
- PAL/NTSC CRTC cadence and CPU retirement rate are independent integration
  domains. This slice has no CPU-retirement multiplier and does not select a
  historical 900% value. Integration must advance `RootCounters::advance_cpu`
  with the native SoC/system-clock delta for the current device-time step, not
  with the amount of CPU work retired during that step. Likewise,
  `DmaRunBudget::max_cpu_cycles` is a native scheduler service quota, not a
  multiplied CPU-retirement quota. A CPU multiplier may change how much
  translated guest work is retired inside a native interval, but must not make
  timer, DMA, SPU, CD, or other media deadlines run 9x faster; SPU/CD/media
  real-time pacing remains integration-owned and native.
- Counter mode/current-write holds, target/overflow reset visibility delays,
  and interrupt pulse width are not represented cycle-exactly. This slice uses
  compressed event transitions; hardware-oracle validation and integration of
  those delays remain release-blocking work.
- Callback bindings and external RAM/device state are not part of these
  snapshots. The integration that owns those objects must snapshot them in the
  same transaction.
- DICR slice behavior is modeled at request-block and linked-list-node
  boundaries. Exact device request cadence still requires hardware-oracle
  validation.
- All DPCR bits, including the CPU-priority field, are retained for readback.
  Arbitration between the CPU and DMA is owned by the scheduler integration;
  this slice only arbitrates the seven DMA channel nibbles.
- Release provenance is established by the repository-level
  `docs/REFERENCE_PROVENANCE.md`. That file is absent from this isolated
  revision, so this directory alone cannot substantiate a release-provenance
  claim.

Build and verify this slice independently:

```text
cmake -S clean-aot/soc -B build-clean-soc -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-soc --config Release
ctest --test-dir build-clean-soc --build-config Release --output-on-failure
cmake -S clean-aot/soc -B build-clean-soc-asan -DCLEAN_AOT_SOC_SANITIZERS=ON
cmake --build build-clean-soc-asan --config Release
ctest --test-dir build-clean-soc-asan --build-config Release --output-on-failure
```

The tests cover register masks and all channels, priority arbitration and
master enables, zero encodings, both address directions and wrap, chopping,
completion IRQs and fail-closed error classification, linked-list
termination/alignment/loop/limit guards, ordering-table output, malformed
callbacks, all counter clock inputs, sync transitions, target/overflow
repeat/one-shot/toggle behavior, rational chunking invariance, atomic malformed
snapshot rejection, reset replay, and snapshot replay.
