# GTE provenance

Implementation authority: the pinned local checkout of PSX-SPX at commit
`b791ca25b7833b356b28917383f231078762a75d`.

The implementation was derived from the written sections in
`docs/geometrytransformationenginegte.md`:

- `GTE Registers`
- `GTE Saturation`
- `GTE Opcode Summary`
- `GTE Coordinate Calculation Commands`
- `GTE General Purpose Calculation Commands`
- `GTE Color Calculation Commands`
- `GTE Division Inaccuracy`

Cycle metadata and input-latch context were taken from the written
`docs/gtepipelinetimings.md` sections `Overview`, `Result tables`, and
`Patterns`. The model reports documented total command cycle counts and
exposes the measured input-effect boundaries as tri-state metadata. It keeps
`execute` atomic: the timing table does not specify output-register writeback
ordering, so the oracle does not invent in-flight state mutation semantics.
Timing-table omissions and register-level dashes that conflict with a command
consumer (notably IR0 for NCDS and RGBC's CODE byte) are exposed as unmeasured,
and MVMVA selected-matrix observations described only as “max 1” are not
represented as exact per-register timings.

The pinned timing source reports one SCPH-5501 and warns of code-layout and
silicon-revision movement, so these boundaries are evidence metadata rather
than a cross-hardware guarantee. Its bugged MVMVA far-color description does
not enumerate which FLAG bits use the full expression; the model limits that
interpretation to the documented MAC overflow probes.

The register API exposes completed architectural values and does not model CPU
load-delay slots or IRGB read-after-write latency. Those effects are outside
this atomic semantic oracle and must be represented by the CPU scheduling
layer.

No implementation source was copied from a recompiler, emulator, debugger, or
other execution engine. The reciprocal path follows the written normalized
Newton-Raphson steps and generated arithmetic table expression in the pinned
specification. The tests are independent directed and deterministic
property-style vectors.
