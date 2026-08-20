# R3000A semantic reference oracle

This is an independent, original C++20 reference tranche for validating a
region compiler. It is deliberately a test oracle, not a shipping interpreter
or runtime dependency. The directory is standalone and the target is marked
with the `R3000A_REFERENCE_ONLY` CMake property and the
`R3000A_SEMANTIC_REFERENCE_ONLY=1` compile definition so release configuration
can reject or exclude it.

The implementation models a little-endian, flat test memory and the
architectural state needed by the covered MIPS-I/R3000A operations:

- 32 GPRs with an immutable zero register, HI/LO, explicit current/next PC,
  branch-delay state, and a one-instruction delayed-write lane.
- CP0 state for the status, cause, EPC, BadVAddr, PRId, and basic control
  registers used by the tranche.
- Transactional instruction retirement: an older delayed load commits before
  the current instruction's write, while a faulting instruction contributes no
  younger side effects.
- Branch delay behavior for direct and register jumps, ordinary conditional
  branches, and REGIMM link branches. `BLTZAL` and `BGEZAL` write `PC+8` even
  when their predicate is false.
- Delayed byte/halfword/word loads and CP0 moves, plus the little-endian
  merge-forwarding behavior needed by adjacent `LWL`/`LWR` pairs.
- Little-endian `LWL`, `LWR`, `SWL`, and `SWR` lane merges; aligned `LB`, `LH`,
  `LW`, `SB`, `SH`, and `SW`; integer overflow and address errors; reserved
  instruction handling; exception metadata; exception vectors; and RFE's
  status-stack pop.

Branch/control-transfer instructions placed in a delay slot are classified as
reserved by this deterministic oracle policy because their hardware behavior
is not architecturally restartable. A misaligned `JALR` faults before its
delay slot; a misaligned `JR` reaches its delay slot and faults when the target
instruction is fetched.

## Build and test

From the repository root:

```powershell
cmake -S clean-aot/cpu -B clean-aot/cpu/build -G Ninja
cmake --build clean-aot/cpu/build --parallel 4
ctest --test-dir clean-aot/cpu/build --output-on-failure
```

Sanitizers are opt-in and remain local to this reference project:

```powershell
cmake -S clean-aot/cpu -B clean-aot/cpu/build-asan -G Ninja `
  -DR3000A_ENABLE_SANITIZERS=ON
cmake --build clean-aot/cpu/build-asan --parallel 4
ctest --test-dir clean-aot/cpu/build-asan --output-on-failure
```

The test executable contains directed architectural checks and deterministic
randomized invariants for integer wraparound, zero-register immunity, branch
delay/continuation, delayed loads, unaligned load/store pairs, and exception
state. It uses fixed-width arithmetic and a fixed seed; no external emulator,
disc, BIOS, network input, generated code, or title data is involved.

No parent build target links this directory, and this standalone CMake project
does not install or export the reference library.
