# Clean GTE semantic oracle

This directory is a standalone C++20 model of the PS1 Geometry Transformation
Engine register file and command semantics. It is an instruction-level oracle
for compiler differential tests. It has no media, title, guest-image, host
device, or project-runtime dependency and is not a shipping execution path.

The public API in `gte.hpp` provides:

- written-spec data/control register values, packed signed fields, FIFO
  movement, the H readback quirk, IRGB/ORGB conversion, and LZCS/LZCR;
- decoding of the command word's real opcode, shift, matrix/vector/translation
  selectors, and IR lower-limit bit;
- all 22 documented retail command operations, including triple variants;
- fixed-point MAC/IR arithmetic, saturation and FLAG reporting, the documented
  reciprocal approximation, and command cycle metadata;
- deterministic execution receipts and a stable full-state digest;
- tri-state input-latch metadata and a pure timed-write effect query for the
  measured pipeline boundaries.

Architectural MAC registers and snapshots are signed 32-bit values. Command
math uses the documented signed 44-bit transient before the optional shift and
records overflow from the full accumulator. The color FIFO then divides the
architectural signed MAC value by 16, as specified, rather than retaining a
hidden wide register state.

Register accessors expose completed architectural values. They do not model
CPU load-delay slots or the delayed visibility of IRGB writes described by the
pinned source; those instruction-scheduling effects belong to the CPU layer.

`execute` is intentionally atomic. `input_latch_boundary` and
`timed_write_effect` expose the measured timing facts without pretending that
the timing tables specify output-register writeback races. An unmeasured
instruction/selector/register shape is reported as `Unmeasured` and must be
handled by the caller rather than guessed. Inputs consumed by a command but
missing from the timing table, and RGBC code-field consumers whose rows only
contain a dash, are likewise left unmeasured. MVMVA “max 1” selected-matrix
observations are not promoted to exact per-register boundaries.
`InputLatchBoundary::nop_boundary` and
`TimedWrite::instruction_gap` use the PSX-SPX unit exactly: the smallest
number of intervening NOP/instruction slots before the write has no effect;
they are not GTE-cycle timestamps. `MayAffectInput` means the write is early
enough to be capable of affecting the sampled input, not that its value must
change the result. Because MVMVA boundaries depend on `mx`, `v`, and `cv`, the
command-only timing overload deliberately returns `Unmeasured`; callers must
pass a decoded instruction. Invalid register-bank/index pairs also fail closed.

The latch boundaries are a transcription of the pinned measurements, not a
claim about every console revision. The source measured one SCPH-5501; code
layout moved some boundaries by one or two NOP slots, and other silicon may
drift by one or two cycles. It also describes the bugged MVMVA far-color
selector's FLAG behavior only as “some bits” using the full expression. This
model applies that full expression to the documented MAC overflow probes while
retaining the two-term data result; finer undocumented FLAG behavior remains a
hardware-validation question.

Build and test this component in isolation:

```text
cmake -S clean-aot/gte -B build-gte-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-gte-release --config Release
ctest --test-dir build-gte-release --build-config Release --output-on-failure

cmake -S clean-aot/gte -B build-gte-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-gte-debug --config Debug
ctest --test-dir build-gte-debug --build-config Debug --output-on-failure
```

The tests use fabricated register values and a deterministic generator. They
exercise edge vectors, selector quirks, FIFO ordering, flag boundaries,
projection division boundaries, cycle metadata, and metamorphic invariants.

The written behavioral source and cleanroom constraints are recorded in
`provenance.md`.
