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
| `0x00` | data | 8-bit | TX write / oldest RX byte read |
| `0x04` | status | 32-bit | FIFO, DSR/ACK, IRQ, and baud timer |
| `0x08` | mode | 16-bit | reload factor and documented SIO0 mode bits |
| `0x0C` | control | 16-bit | TX/RX enable, `/CS`, IRQ enables, reset/ack strobes |
| `0x0E` | baud | 16-bit | SIO0 reload value |

Other widths, unaligned accesses, and unknown offsets fail closed. SIO0 uses
eight synchronous bits per byte. The reload factor is `1`, `16`, or `64`
for mode values `0/1`, `2`, and `3` respectively; the period is
`max((reload * factor) & ~1, 1)` CPU cycles per bit. The standard `0x0088`
reload therefore takes 1,088 CPU cycles per byte at the 33,868,800 Hz PS1
clock.

The TX side has two bounded slots, including the byte currently shifting. A
third write deterministically replaces the waiting slot, matching the
documented SIO behavior and returning `overwritten=true`. The RX side has
eight bounded entries and retains an internal overrun receipt. ACK/DSR pulses
are 68 CPU cycles by default (the documented two-microsecond minimum at the
PS1 clock); the callback and status bit are deterministic and host-time-free.

## Typed devices

Attach one `PadDevice` and one `MemoryCard` to each of the two SIO0 ports.
When `/CS` is asserted, both devices see the first address byte. `0x01`
addresses the controller and `0x81` addresses the memory card; exactly one
acknowledging device becomes active. Multiple acknowledgements are reported as
a fail-closed bus conflict.

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
valid checksum, valid sector, and final good-status transfer. No card image is
created, loaded, or persisted by this component.

`MemoryCard::format_new_image` creates a deterministic blank raw card with the
`MC` header, free directory frames, broken-sector frames, and frame checksums.
It is deliberately not a game-save creator; save-file directory entries remain
the caller's responsibility.

Both devices and SIO0 expose fixed-size value snapshots. A memory-card
snapshot includes an image fingerprint and protocol fingerprint, so restoring
against a changed caller image fails closed. Restore device snapshots after a
bus snapshot when restoring a transaction that was selected at the time of the
capture.

## Explicit boundary

SIO1, asynchronous UART framing, multitap arbitration, non-Sony card
extensions, card filesystem policy, host persistence, and title-specific input
mapping are not implemented here. The component does not claim exact silicon
behavior for those cases. The SIO0 timer is exposed as deterministic remaining
byte-transfer cycles; sub-cycle electrical edge placement and revision-specific
ACK latency still require hardware traces during machine integration.
