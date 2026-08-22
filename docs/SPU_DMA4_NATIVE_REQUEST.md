# Native SPU DMA4 request coupling

## Closed defect

The SPU already modeled delayed SPUCNT transfer-mode visibility and the extra
DMA-read warm-up sample. The runtime did not forward that modeled request to
DMA channel 4. DMA4 therefore retained its controller reset level and could
start before SPUSTAT readiness, causing the SPU callback to reject the transfer
instead of waiting for the hardware DRQ boundary.

DMA4 now samples `Device::dma_request()` wherever device interrupt/request
levels are resampled. DMA-write asserts when its delayed mode becomes visible;
DMA-read asserts only after its additional native 44.1 kHz warm-up; stop mode
deasserts the line. The level is device time and is never CPU-retirement scaled.

## Adversarial proof

The integration test starts a request-synchronized DMA4 transfer while SPU is
stopped. It remains blocked at native cycle 767, observes SPUCNT DMA-write mode
at cycle 768 without transferring retroactively, and transfers one 32-bit word
on the following native service cycle. The transferred halfwords and sound-RAM
byte order are checked exactly.

Paired SoC and SPU snapshots at the asserted-request boundary restore and
repeat the same transfer. Standalone SPU tests also pin write assertion, stop
deassertion, and delayed read assertion.

Physical sound-RAM bus-slot duration and global DMA arbitration remain explicit
unwaived boundaries; this tranche closes the request-level coupling only.
