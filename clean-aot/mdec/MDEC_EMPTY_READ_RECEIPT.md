# MDEC empty-read completion receipt

Scope: CPU and DMA behavior at an empty MDEC output FIFO.

Pinned evidence: `psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d`,
`docs/macroblockdecodermdec.md`, MDEC Data/Response Register. A CPU read with
no output available returns garbage; it is still a completed register read.
DMA1 is separately controlled by Data-Out Request.

Prior defect: `read_data_word` returned `Fault::OutputFifoEmpty`. Runtime MMIO
wiring translated that into a failed device read, allowing an ordinary empty
MDEC0 access to become a guest-visible bus failure.

Correction:

- an empty CPU MDEC0 read consumes one deterministic register cycle, completes
  one word, leaves FIFO-empty status set, and returns zero as the explicit
  clean-room choice for unspecified garbage bits;
- a latched device fault still fails closed;
- DMA1 with no Data-Out Request transfers zero words and continues to report
  `OutputFifoEmpty`.

Proof snapshots the reset-empty boundary, performs the read before and after
restore, and requires exact value, cycle/state digest, FIFO status, and result
equivalence. A separate adversarial DMA assertion proves that CPU completion
does not weaken DMA request gating. Existing randomized empty/nonempty reads,
chunking, snapshots, FIFO pressure, command countdown, and decode vectors all
remain enabled.

Verification on 2026-08-22:

- Windows GCC 16.1.0 strict-warning Debug build and CTest: pass.
- Linux GCC 15.2.0 ASan+UBSan with leak detection: pass.
- Linux Clang 21.1.8 ASan+UBSan with leak detection: pass.
