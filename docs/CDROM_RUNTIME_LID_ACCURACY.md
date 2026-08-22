# CD-ROM runtime lid accuracy tranche

The controller defined the ShellOpen status bit but had no state or host event
capable of asserting it. BIOS/game Getstat and GetID polling therefore could
not observe a runtime lid transition, while active reads, CDDA, and latched DMA
data could survive external media removal.

Lid state is now independent of mounted source/layout identity. Opening stops
read/play/seek immediately, cancels media-phase and sector events, clears
sector and DMA buffers, drops motor state, and invalidates cached locations.
Getstat reports bit `0x10`; disc-dependent commands reject the open state via
their normal INT5 error path. Closing clears the bit but never invents or
replaces media.

The adversarial test opens the lid during active ReadN, proves stale data cannot
escape, checks Getstat and GetID, then closes it and proves the mounted disc is
queryable again. No boot frontier, route tool, or runtime boot file changed.
