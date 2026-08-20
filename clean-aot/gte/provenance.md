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
`docs/gtepipelinetimings.md` section `Result tables`. The model itself does
not encode the measured latch offsets; it reports the documented total command
cycle count for compiler-side metadata and keeps register semantics
deterministic.

No implementation source was copied from a recompiler, emulator, debugger, or
other execution engine. The reciprocal path follows the written normalized
Newton-Raphson steps and generated arithmetic table expression in the pinned
specification. The tests are independent directed and deterministic
property-style vectors.
