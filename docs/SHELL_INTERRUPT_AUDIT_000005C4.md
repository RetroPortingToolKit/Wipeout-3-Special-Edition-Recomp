# Independent interrupt audit at continuous PC 0x000005C4

## Result

No interrupt storm or missing interrupt return is evidenced at the committed
route terminal. Exact authenticated bytes prove that `0x000005C4` is the A0
BIOS service-table dispatcher, not an interrupt handler. The prior handoff
description calling it a low-RAM interrupt handler was incorrect.

The deterministic receipt is
`clean-aot/catalog/evidence/bios-shell-interrupt-audit-000005c4.json`, SHA-256
`4780941ecb88cb635095e85a31afcd03d1886073e6e75948948e2bc45a5e1999`.
It binds the verified retail BIOS, exact terminal RAM epoch, committed route
receipt, runtime interrupt implementation, SoC interrupt controller, and
generated COP0 implementation.

## Exact route classification

The authenticated path is:

1. BIOS `0xBFC0D8E0` loads `t2=0xA0`.
2. `0xBFC0D8E4` executes `JR t2`; its delay slot loads `t1=0x40`.
3. Installed low-RAM trampoline `0x000000A0` jumps to `0x000005C4`.
4. `0x000005C4` computes table address `0x200 + t1*4` and jumps through it.
5. Exact table word `0x00000300`, service entry 0x40, contains
   `0xBFC0D8E0`.

This is the ordinary A0/40 service cycle selected by verified BIOS code. The
three-million-dispatch receipt ends immediately after entering the dispatcher;
it does not demonstrate repeated exception entry at that PC.

The real installed general-exception vector is separately
`0x00000080 -> 0x00000C80`. The terminal route does not traverse either
address, and its committed receipt contains no I_STAT, I_MASK, COP0
Status/Cause/EPC, or stack samples. It therefore cannot support attribution to
VBlank or any other interrupt source, nesting, or a failed exception return.

## CRTC, I_STAT, and COP0 comparison

- Exact CRTC timing raises VBlank once when the scanline reaches the programmed
  vertical end. It latches I_STAT bit 0; VBlank is not a continuously resampled
  device level.
- I_STAT writes are zero-to-clear. Runtime then resamples genuine level sources:
  DMA, CD-ROM, GPU IRQ1, SIO, and SPU can reassert only while their underlying
  device line remains active. Root-counter lines are resampled by their own
  latched/toggle semantics.
- `(I_STAT & I_MASK) != 0` drives COP0 hardware IP2. Generated code samples
  `Status.IE`, `Status.IM`, and `Cause.IP` at instruction boundaries.
- Exception entry pushes all six low Status mode bits, disabling current IE;
  RFE pops the mode stack. This prevents ordinary same-level nesting until the
  handler deliberately reenables interrupts.

Those semantics and their existing adversarial tests are consistent. Nothing
in the authenticated A0/40 path independently proves a runtime or SoC defect,
so no production source was changed.

## Reproduction and fail-closed limits

`tools/analysis/audit_shell_interrupt_path.py` regenerates the receipt from the
ignored verified BIOS and exact low-RAM epoch. It rejects any identity,
instruction, dispatch-table, terminal-PC, or implementation-marker mutation.
The tool does not infer unavailable interrupt state. A future live diagnosis
must explicitly capture I_STAT, I_MASK, COP0 Status/Cause/EPC, `t1`, `$ra`, and
the exception-vector visit count before claiming an interrupt storm.
