# R3000A semantic reference oracle

This directory contains an independently written C++20 architectural reference
used to test offline compiler semantics. It is not a shipping interpreter,
runtime fallback, or package dependency. CMake marks every target
`R3000A_REFERENCE_ONLY`; a parent build creates no CPU target when tests and
`R3000A_BUILD_REFERENCE_ORACLE` are both disabled. Nothing here is installed or
exported.

## Covered contract

The oracle currently models the scalar, little-endian R3000A behavior needed to
check generated regions:

- 32 GPRs with immutable register zero, HI/LO, explicit PC/next-PC and branch
  delay state.
- Fixed-width integer arithmetic without signed host overflow, signed and
  unsigned comparisons, masked shifts, and directed multiply/divide edges.
- Direct and register jumps, conditional and link branches, BC0F/BC0T sampling
  of an explicit settled BrCond0 input, delay-slot EPC and Cause.BD behavior,
  and the documented distinction between misaligned JALR and JR targets.
- One-instruction delayed CPU loads and moves from a coprocessor, cancellation
  by a younger direct GPR write, merge forwarding for adjacent LWL/LWR, and
  completion of older delayed effects before a younger exception.
- One-instruction delayed writes to CP0 and COP2. Status/Cause interrupt and
  coprocessor-usability effects become active only after two following guest
  instructions, matching the exposed R3000A control hazard.
- Little-endian LWL/LWR/SWL/SWR byte lanes, ordinary scalar loads/stores,
  alignment checks, user/kernel address protection, and KSEG0/KSEG1 physical
  aliases.
- The architectural reset vector and guaranteed kernel/interrupt-disabled/BEV
  state, normal/bootstrap exception vectors, Status mode stack push and RFE
  pop, BadVAddr, EPC, Cause exception/BD/CE/IP fields, software and hardware
  interrupt masks, and interrupt recognition before a branch delay slot.
- SYSCALL, BREAK, reserved encodings, and coprocessor-unusable exceptions.
- A typed COP2 port for MFC2/CFC2/MTC2/CTC2, BC2F/BC2T condition branches,
  command words, LWC2, and SWC2.
  COP1, non-MFC3 direct COP3 operations, and LWC3 fail explicitly as
  unavailable coprocessors. MFC3 and SWC3 follow their R3000A-specific
  Reserved Instruction rules instead of being mislabeled as coprocessor
  unusable.
- Deterministic metadata for one-cycle cached issue, delayed-load visibility,
  12-cycle multiply results, and 35-cycle divide results. Memory/device wait
  states are explicitly external to this CPU oracle. A connected COP2 device
  returns its command result latency through the typed port.

Divide-by-zero results use the selected deterministic hardware profile. The
general MIPS-I architecture describes those results as undefined, so tests do
not present that profile as a portable MIPS-I guarantee.

## Scope boundary

This tranche does **not** claim complete end-to-end R3000A system semantics. It
does not implement a TLB, caches, instruction-fetch bus errors or wait states,
asynchronous input pin synchronizers, CP1/CP3 devices, or COP2/GTE arithmetic.
The flat `Memory` harness maps rejected data reads to DBE so they cannot be
misreported as alignment faults. Rejected writes use the same deterministic
DBE harness policy even though an R30xx write cycle does not produce a precise
architectural bus-error exception. The typed COP2 port is the owned CPU
boundary only. An adapter to the separate GTE component and compiler lowering
for all represented COP2 operation classes remain explicit integration work
outside this directory. Until both are wired and tested, a project receipt must not
claim complete R3000A/GTE coverage.

The default `ResetState` selects the generic IDT R3000A manual profile
(PRId Imp=3, Rev=0). PRId identifies a concrete CP0 implementation rather than
the MIPS-I ISA; a machine integration targeting different silicon must pass
its observed value explicitly. The reset status default includes only fields
the selected full-MMU profile guarantees. Other reset-time status fields are
deliberately not invented.

Control-transfer instructions in a branch delay slot have architecturally
undefined behavior. This oracle deliberately fails them as ReservedInstruction
so differential tests are deterministic; that is an oracle policy, not a
hardware guarantee. External interrupt inputs are sampled at instruction
boundaries; board-level synchronization latency belongs to the machine
scheduler.

## Build isolation

Standalone builds enable the oracle and tests by default:

```powershell
cmake -S clean-aot/cpu -B build/cpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cpu --parallel 4
ctest --test-dir build/cpu --output-on-failure
```

Sanitizers are opt-in and fail without recovery:

```powershell
cmake -S clean-aot/cpu -B build/cpu-sanitize -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DR3000A_ENABLE_SANITIZERS=ON
cmake --build build/cpu-sanitize --parallel 4
ctest --test-dir build/cpu-sanitize --output-on-failure
```

A parent release-oriented configure can prove exclusion with
`-DBUILD_TESTING=OFF -DR3000A_BUILD_REFERENCE_ORACLE=OFF`. Release acceptance
still requires the repository link-map gate; a property or option is not a
substitute for checking the linked artifact.

## Verification design and provenance

Tests use directed edge vectors, exhaustive status-stack states, independent
byte-array lane models, and metamorphic randomized invariants. They do not call
another emulator, load a BIOS or disc, execute generated code, or restate the
implementation's merge formulas as their expected model.

Behavior was derived from the *MIPS R3000 Family Architecture* manual,
particularly the instruction reference and the exception, address-space,
coprocessor-hazard, and reset sections. The manual is used as a behavioral
reference only; no implementation or prose was copied. Console-specific
undefined-result checks remain labeled as selected hardware-profile behavior.

The source-isolation policy intentionally uses no allowlist. Address constants
are readable named compositions of segment selectors, physical boot-ROM
placement, and architectural vector offsets. Numeric sign data is represented
as a named word-sign bit rather than an address-shaped literal.
