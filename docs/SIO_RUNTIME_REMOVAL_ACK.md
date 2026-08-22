# SIO runtime removal and ACK release

## Closed defect

Controller and memory-card detachment was rejected whenever `/CS` was low.
That host restriction let an electrically removed peripheral remain active
through the rest of its protocol and ACK timing window. In particular, a card
could continue a command after removal instead of releasing the shared serial
lines.

Detachment now terminates the selected device's protocol state immediately.
If it was the addressed device, pending or asserted open-collector `/ACK` is
released, the active-device binding is cleared, and a queued byte begins at
that exact native CPU-cycle boundary. A byte already in the SIO shifter still
finishes, but receives the undriven `FFh` value and cannot schedule another
device ACK. An IRQ already raised by the `/ACK` falling edge remains latched
until the guest uses the normal SIO control acknowledge.

The same tranche fixes snapshot validation for the reachable state where one
enabled TX byte waits behind pending or asserted ACK. Its overflow proof now
uses `ack_until` as the queued byte's start boundary; the old validator
incorrectly required a byte to be shifting already.

## Adversarial timing and replay proof

The test addresses a memory card, queues the read command behind its address
ACK, and snapshots both bus and card. It removes the card once one cycle before
the pending ACK path and again after replay exactly when ACK becomes asserted.
Both removals release the line and complete the queued command as `FFh` with no
follow-up ACK. The asserted-edge case additionally proves that removal does
not erase the SIO IRQ latch and that the guest acknowledge clears it.

## Remaining boundaries

Insertion while `/CS` is already low remains rejected so a newly attached
device cannot fabricate an address edge it did not observe. Contact bounce and
sub-cycle electrical transitions require hardware traces and remain outside
the cycle-level model.
