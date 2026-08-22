# CD-ROM Reset reboot timing

Command `0x1C` previously reset state and returned INT3, but the controller
accepted another command immediately after acknowledgement. The hardware
contract requires software to wait through a 1/8-second HC05 reboot interval.

Reset now schedules an exact `33,868,800 / 8 = 4,233,600` native-cycle busy
window. Its INT3 remains unchanged and no fictitious completion interrupt is
generated. Commands are rejected after INT3 acknowledgement, through the last
cycle before the deadline, and become legal exactly at the deadline. The
existing permissive parameter behavior is retained.

The adversarial test checks both sides of the inclusive boundary and uses the
CD controller's native clock, independent of 100 Hz CPU-retirement scaling.
No boot frontier, boot route, GPUREAD, or runtime source was modified.
