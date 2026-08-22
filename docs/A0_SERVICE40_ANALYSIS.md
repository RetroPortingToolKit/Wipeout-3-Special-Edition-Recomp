# A0 service 0x40 exact analysis

The authenticated SCPH-1001 bytes prove that `0xBFC0D8E0` is a non-returning
fatal service, not an initialization routine. It loads A0 service index `0x40`;
the installed A0 trampoline reaches the dispatcher at `0x5C4`; table slot
`0x300` points straight back to `0xBFC0D8E0`. Continuous execution is therefore
one intentional five-node self-cycle.

The cycle never touches the stack and contains no `jr ra`. It preserves `sp`,
`ra`, argument registers, and saved registers. It uses `t2` for the A0 vector,
mutates `t1` from `0x40` to the byte offset `0x100` (the BIOS stub restores it
on every lap), and uses `t0` for trampoline, table address, and target. The
caller contract is “does not return.” Exact low-RAM code calls it from
`0x1B08` only after classifying an exception as neither interrupt nor syscall.

The live route entered that fatal arm after shell instruction `0x80051054`.
Authenticated resident data binds its pointer to `0x1F801810`: the shell writes
a GP0 command and then reads GPUREAD into register zero as a synchronization
operation. At traced baseline commit `a806544`, runtime converted a GPUREAD
whose transfer latch was not ready into `BusErrorData`. Hardware-visible
GPUREAD is still CPU-readable in this state; transfer readiness governs DMA,
not whether the CPU MMIO register exists. The false bus error explains the
general-exception route and fatal service exactly.

An active concurrent runtime change already removes that readiness gate. This
audit deliberately does not stage or duplicate it, and does not edit boot
manifests or probe control. The generated receipt is fail-closed over exact
BIOS, low-RAM epoch, full-RAM epoch, route receipt, baseline source semantics,
all relevant instructions, the service-table pointer, and the live GPUREAD
pointer.
