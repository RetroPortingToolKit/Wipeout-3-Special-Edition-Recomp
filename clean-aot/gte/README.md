# Clean GTE semantic oracle

This directory is a standalone C++20 model of the PS1 Geometry Transformation
Engine register file and command semantics. It is an instruction-level oracle
for compiler differential tests. It has no media, title, guest-image, host
device, or project-runtime dependency and is not a shipping execution path.

The public API in `gte.hpp` provides:

- exact data/control register transfers, packed signed fields, FIFO movement,
  the H readback quirk, IRGB/ORGB conversion, and LZCS/LZCR;
- decoding of the command word's real opcode, shift, matrix/vector/translation
  selectors, and IR lower-limit bit;
- all 22 documented retail command operations, including triple variants;
- fixed-point MAC/IR arithmetic, saturation and FLAG reporting, the documented
  reciprocal approximation, and command cycle metadata;
- deterministic execution receipts and a stable full-state digest.

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
