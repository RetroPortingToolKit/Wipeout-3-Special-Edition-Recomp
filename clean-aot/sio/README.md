# Clean-room SIO0

This directory contains a standalone C++20 model of the synchronous
PlayStation SIO0 controller/memory-card bus. It is a generic machine component;
it has no game policy, title addresses, host controller API, filesystem, or
dynamic allocation on the byte-transfer path.

## Machine-layer contract

The machine layer maps the component at its own SIO0 base address and passes
the following offsets to `Sio0::read` and `Sio0::write`:

| Offset | Register | Width | Behavior |
| ---: | --- | --- | --- |
| `0x00` | data | 8/16/32-bit | TX low byte / RX FIFO preview and removal |
| `0x04` | status | 32-bit | FIFO, DSR/ACK, IRQ, and baud timer |
| `0x08` | mode | 16-bit | reload factor and documented SIO0 mode bits |
| `0x0A` | control | 16-bit | TX/RX enable, `/CS`, IRQ enables, reset/ack strobes |
| `0x0E` | baud | 16-bit | SIO0 reload value |

Unaligned accesses, unknown offsets, and wrong widths on the other registers
fail closed. An 8-bit or 16-bit data read removes one FIFO entry; the 16-bit
form previews the second entry. A 32-bit data read previews four entries and
removes four. Missing preview bytes use SIO's documented physical mirror of
the newest byte, while a logically empty read still fails closed. Data writes
of any supported width transmit only bits 0-7. Device transfers additionally
require SIO0's supported 8-bit, no-parity,
high-when-idle mode; an incompatible mode is retained in the register but a
data write fails with `UnsupportedMode`. SIO0-only unused mode bits 6-7 read
back as zero. The reload factor is `1`, `16`, or
`64` for factor values `0/1`, `2`, and `3` respectively. A bit takes
`max((reload * factor) & ~1, 1)` CPU cycles, so standard mode `0x000D` and
reload `0x0088` take 1,088 CPU cycles per byte at the 33,868,800 Hz PS1 clock.
Status bits 11-31 expose the free-running decrementing half-bit baud timer
rather than a whole-byte countdown. A baud write reloads that timer; a mode
change affects later reloads without retroactively changing its current
count. An in-flight byte retains the bit period with which it started.

The TX side has one holding register plus the byte currently shifting. Repeated
writes while TX is disabled replace the held byte; once shifting starts, one
additional byte can wait and further writes replace that waiting byte. Every
replacement returns `overwritten=true`. The RX side has eight bounded entries
and retains an internal overrun receipt.

All event timestamps reserve `UINT64_MAX` as the no-event sentinel. A transfer
that could exhaust the cycle domain is rejected atomically with
`CycleOverflow`; queued bytes and MMIO state are left unchanged.

Acknowledgements are scheduled events rather than being asserted at transfer
completion. The documented nominal profile begins `/ACK` 400 CPU cycles after
the byte and holds it for 100 cycles; an original Sony-card read adds the
documented approximate 31,000-cycle delay at its late-ACK point. These values
are explicit `ByteTransfer` metadata, and `next_event_cycle()` lets the machine
scheduler advance without consulting host time. A caller that clocks another
acknowledged byte while an ACK is pending gets a sticky `ack_collision()`
receipt and the overlapping pulse is dropped fail-closed. Hardware-trace
calibration can replace the nominal profile without changing protocol code.

## Typed devices

Attach one `PadDevice` and one `MemoryCard` to each of the two SIO0 ports.
When `/CS` is asserted, both devices see the first address byte. `0x01`
addresses the controller and `0x81` addresses the memory card; exactly one
acknowledging device becomes active. Multiple acknowledgements are reported as
a fail-closed bus conflict. The address phase is single-shot: an unrecognized
or conflicting first byte leaves that `/CS` transaction inert, and a new
address is considered only after `/CS` is toggled.

`PadDevice` supports the digital poll, analog poll, command-43 configuration
entry/exit, LED/mode command 44, controller status queries, and command-4D
rumble mapping used by DualShock-compatible PS1 controllers. Input values,
LED state, motor mapping, and motor values are plain data; no host API is
called. Pressure-sensitive DualShock2 extensions and undocumented commands are
outside this tranche and return bounded zero responses or fail closed.

`MemoryCard` accepts exactly one caller-owned 128 KiB raw image. It implements
Sony read (`0x52`), write (`0x57`), and card-ID (`0x53`) framing, address
confirmation, 128-byte sectors, XOR checksums, status bytes, invalid-sector
handling, and a write durability callback. The callback runs only after a
valid checksum, valid sector, and final good-status transfer. It is a
post-commit notification containing the sector, generation, checksum, and new
image fingerprint; the protocol state is already final when callback code
runs. Durable host storage remains the caller's responsibility. The callback
must not re-enter the card or mutate its image; it may copy the committed bytes
to caller-owned persistence.
No card image is created, loaded, or persisted by this component.

`MemoryCard::format_new_image` creates a deterministic blank raw card with the
`MC` header, free directory frames, broken-sector frames, and frame checksums.
It is deliberately not a game-save creator; save-file directory entries remain
the caller's responsibility.

Both devices and SIO0 expose fixed-size value snapshots. A memory-card
snapshot includes an image fingerprint and protocol fingerprint, so restoring
against a changed caller image fails closed. Restore device snapshots after a
bus snapshot when restoring a transaction that was selected at the time of the
capture. SIO snapshots also retain the free-running baud timer and all pending
transfer/ACK event times. They are C++ value-state snapshots, not a stable
on-disk serialization ABI.

The SIO reset strobe resets only SIO registers, FIFOs, timing, and IRQ state.
It deliberately does not power-cycle attached caller-owned pads or cards;
machine power-on code may reset those devices separately. Snapshot restore
validates FIFO bounds, selected-port encoding, active-device state, transfer
and ACK timestamps, mode/control masks, and IRQ consistency before mutating the
bus. Restoring an unchanged IRQ level does not synthesize callback edges.

## Explicit boundary

SIO1, asynchronous UART framing, multitap arbitration, non-Sony card
extensions, card filesystem policy, host persistence, and title-specific input
mapping are not implemented here. The component does not claim exact silicon
behavior for those cases. Sub-cycle electrical edge placement and
revision-specific ACK latency still require hardware traces during machine
integration; the constants above are documented nominal values, not a claim of
revision-exact electrical edges.
