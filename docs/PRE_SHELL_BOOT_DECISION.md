# Pre-shell boot decision evidence

Date: 2026-08-21

## Result

The zero-CD-access run has not taken a disc/no-disc boot branch. It is still in
unconditional shell hardware initialization, specifically the SPU-RAM clear
invoked before the first disc-status query. No independently proven reset-state,
MMIO-routing, lid, SIO, expansion, or interrupt-controller defect was found.

This conclusion uses the authenticated SCPH1001 image with SHA-256
`71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3`
and the deterministic receipt
`clean-aot/catalog/evidence/bios-shell-cd-execution-09473267.json`.
BIOS manifests and probe control were not modified.

## Exact executed path

- Copied shell entry `0x80030000` is reached as the saved return target after
  copied-RAM routine `0x00001508`; there is no hardware-visible boot predicate
  at that transition.
- Shell entry executes a fixed initializer chain. At `0x80030068`, it
  unconditionally calls `0x8003FDB0` with `a0=1`.
- `0x8003FDB0` selects SPU initialization mode 6 through `0x8003FEB4`.
- `0x8003FFB0` builds a zero buffer and loops from `s0=0xE000` to `0x10000` in
  `0x200` steps. Every iteration calls `0x80054428` with `a1=0x1000`, clearing
  64 KiB of SPU RAM in total.
- `0x800544E0` divides each 4 KiB request into 64-byte transfers.
  `0x800545B8` reads each source halfword; its branch delay slot writes that
  halfword to SPU transfer FIFO `1F801DA8`. It then enables manual transfer and
  polls SPUSTAT bit 10 until the FIFO transfer completes.
- The receipt's sampled PC `0x800545B8`, 22,951 device writes, and zero accesses
  to `1F801800-1F801803` are therefore a coherent mid-SPU-clear state, not a
  shell-selection outcome. A complete clear requires 1,024 64-byte batches and
  32,768 FIFO halfword writes, in addition to control/status traffic.

## First relevant boot predicates, not yet executed

After the initializer returns, shell entry eventually calls `0x800359C4` at
`0x80030120` with `a0=1`. That routine performs the disc-status exchange and
tests cached status byte `0x80079EB4` bit `0x10`, the CD ShellOpen flag, at
`0x80035A24` and again at `0x80035A84`.

The subsequent shell decision sequence is:

1. `0x80030128`: branch to fallback `0x800301E8` if `0x800359C4` returns a
   negative result.
2. `0x8003013C`: branch to fallback if shell state word `0x80079DFC` is zero.
3. Optionally call `0x80056FF0` when `0x80079DF0` is nonzero.
4. Repeat `0x800359C4`; `0x80030168` and `0x8003017C` apply the same failure and
   state-word checks.
5. Only after those checks does execution continue through `0x8003FB24` and
   `0x80030684`; success proceeds at `0x800301C4`, while failure enters the
   shell/fallback setup at `0x800301E8`.

None of these branches can explain the current zero-CD-access receipt because
none has executed yet.

## Reset and mapping audit

- CD-ROM construction retains the mounted source/layout across reset and resets
  command, response, IRQ, stream, seek, motor, and host-register state to idle.
  `status_value()` exposes Playing/Seeking/Reading/MotorOn only; it does not
  fabricate ShellOpen for mounted media.
- Runtime routes the complete physical byte window `1F801800-1F801803` to the
  CD controller. Writes are byte-only as on the device interface; reads preserve
  the controller's explicit byte/halfword/word behavior.
- SIO0 resets with control/mode/baud zero, empty FIFOs, DSR and IRQ low, port 1
  selected but no active device transaction. No reset pad input is sampled by
  the path above.
- I_STAT and I_MASK reset to zero. Device IRQ sampling raises only asserted
  source levels; CD and SIO are idle, so neither can select this path.
- Expansion-one reads return open-bus all ones in generated execution. The
  authenticated path above does not branch on expansion state before entering
  the SPU initializer.
- Memory-control defaults and RAM-size/mirroring setup occur earlier and do not
  participate in the listed shell predicates.

## Handoff

Advance the existing deterministic run through completion of the bounded SPU
clear before diagnosing disc policy. At the first call to `0x800359C4`, record
the command/status exchange and the values of `v0`, `0x80079EB4`, `0x80079DFC`,
and `0x80079DF0` at the exact branch PCs above. A failure before that point is
SPU initialization/timing; a failure after it can be attributed to the finite
disc-status decision. Do not widen BIOS authorization or force a boot flag to
cross this boundary.
