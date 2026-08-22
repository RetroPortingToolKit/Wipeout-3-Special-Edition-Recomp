# SPU IRQ acknowledge latch

## Closed defect

The SPU core exposed a public out-of-band acknowledge operation that cleared
its internal IRQ9 latch while SPUCNT bit 6 remained enabled. The pinned SPU
register description defines the device-side acknowledge as SPUCNT bit 6 equal
to zero (`Disabled/Acknowledge`). A host interrupt-controller acknowledge must
not silently mutate the SPU latch; otherwise I_STAT can lose a still-asserted
device level.

The invalid operation has been removed. IRQ9 now remains asserted until an
SPUCNT write disables its IRQ enable (or disables the SPU), using the existing
MMIO path shared by the guest.

## Native-edge and replay proof

The adversarial vector asserts IRQ9 from an ADPCM sound-RAM read, snapshots the
asserted latch, restores it without synthesizing a callback, moves the compare
address away, and rewrites an armed SPUCNT. Both machines retain the latch and
produce identical audio and complete deterministic state.

Both then acknowledge through SPUCNT bit 6, verify SPUSTAT bit 6 clears,
re-arm, and retrigger exactly once at the next native 44,100 Hz RAM-access
edge. Original and replay audio/state remain identical. The fixed two-sample
stereo FNV-1a digest is `7685790907612315261`.

## Remaining boundaries

Physical sound-RAM bus-slot arbitration, the undocumented third voice RAM
read, and the system interrupt controller's external level adapter remain
separate acceptance boundaries.
