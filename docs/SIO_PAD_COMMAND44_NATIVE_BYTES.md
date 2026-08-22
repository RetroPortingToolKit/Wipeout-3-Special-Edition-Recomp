# DualShock command 44 native-byte effects

## Closed defect

DualShock command `44h` buffered its LED/mode and lock fields until the final
ninth transfer byte. The protocol assigns those controls to the fourth and
fifth command bytes, and independent controller implementations apply them as
those bytes arrive. Deferring both changes allowed `/CS` release during the
padding tail to discard a mode or lock operation that hardware had accepted.

The LED/mode field now applies when its serial byte completes. Values `00h`
and `01h` select digital/off and analog/on respectively; other PS1 values are
ignored. The following key byte immediately applies the documented masked
`Key AND 03h` lock rule. No padding byte is required to commit either effect.

## Hostile boundary and replay proof

The adversarial packet stops after the LED byte, snapshots the partially
completed command, and restores it into a second pad. Both consume key `07h`
at the same next native byte boundary, proving the masked lock operation and
matching partial protocol state. `/CS` is then released before the remaining
five padding bytes; both pads must retain analog LED mode and the lock. A
forged snapshot that contradicts the already-consumed key is rejected.

## Remaining boundaries

DualShock2 command `4Fh` pressure-response state is outside the PS1 controller
boundary. Revision-specific electrical ACK placement remains trace-gated.
