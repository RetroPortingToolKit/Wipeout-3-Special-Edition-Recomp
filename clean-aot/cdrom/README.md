# Deterministic CD-ROM/CDDA controller slice

This directory contains a standalone C++20 device contract for a statically
recompiled program. It is not an emulator, disc-image parser, filesystem
adapter, or complete PlayStation CD controller. The caller owns the raw-sector
source, normalized disc layout, machine scheduler, DMA channel, and audio
sinks. No disc, console region, license response, or product identity is
assumed by default.

## Supported host contract

The four byte-wide register offsets implement the four host banks. The
parameter and result FIFOs are 16 bytes. There is one pending command register;
parameters are snapshotted when the command is written, so bytes for a later
command cannot bleed into it. Result interrupts are reason codes (`1` through
`5`), not independent flag bits. Each visible reason owns one result packet;
later packets remain hidden until the current reason is acknowledged. The
deterministic pending-packet queue is bounded and stops streaming on overflow.

`HSTS`, `HINTMSK`, `HINTSTS`, write-one-to-clear acknowledgement, parameter
clear, decoder reset, and staged audio-matrix registers are exposed.
The matrix changes only when the apply bit is written. Byte reads are supported
on every readable host port. The explicit 16- and 32-bit read methods support
repeated `HSTS` reads and little-endian sequential `RDDATA`; unsupported wide
ports fail closed.

Read commands first produce an acknowledge packet, then one `DataReady` packet
per delivered CPU sector. A sector is not exposed at `RDDATA` or to the DMA
adapter until the caller asserts the sector-buffer read request. Data-only mode
delivers 2,048 bytes. Whole-sector mode delivers 2,340 bytes beginning at the
raw minute/second/frame/mode header. Overreads use the controller padding rule
instead of inventing an underflow interrupt. The DMA adapter is a deterministic
push boundary invoked only after a requested transfer has been latched.
Up to eight arrived sector payloads are retained in FIFO order alongside their
INT1 epochs. Delayed service therefore cannot pair an older interrupt with a
newer overwritten payload; a ninth unconsumed payload stops the stream with an
explicit data-overflow error before any buffered sector is mutated.

## Timing and media

All events use the 33,868,800 Hz CPU timeline. One-speed sectors are exactly
451,584 cycles apart and double-speed sectors are exactly 225,792 cycles apart.
CDDA sectors contain 588 stereo frames, so 75 sectors equal 44,100 output
frames. The constants also prove the 25/50/100 Hz system cadence; exactly three
one-speed CDDA sectors span four 100 Hz updates. `advance_to` processes every
event at or before its horizon and rejects time reversal. Reset preserves the
machine timeline.

Raw data-sector validation covers Mode 1, Mode 2 Form 1, and Mode 2 Form 2. It
checks sync, packed-BCD address, mode, repeated Mode 2 subheader, and EDC. Mode
1 EDC includes the sync/header/data range. A zero Form 2 EDC is accepted because
that checksum is optional. CPU data delivery is separate from the decoded
2,324-byte Form 2 payload and always follows the selected 2,048/2,340-byte host
transfer size.

`DiscLayout` describes normalized index-1 starts, optional stored index-0
ownership boundaries, track type, and lead-out. `GetTD` and track-select Play
use index 1, while lookup and `GetlocP` attribute pregap sectors to index 0 of
the following track.
Runtime lid state is independent of the mounted source/layout. Opening stops
read, CDDA, and seek activity, cancels media phases, invalidates sector/DMA
buffers, drops motor status, and exposes the ShellOpen bit. Disc-dependent
commands fail while open; Getstat remains available, and closing never silently
replaces caller-owned media.
CDDA playback uses track type, emits silence on data tracks, applies the active
four-coefficient matrix, preserves muted timing, stops at lead-out, and honors
track-boundary autopause. Report mode emits the alternating absolute/relative
INT1 position and pre-mute peak packet at each tenth absolute frame; reports
are dropped while another controller interrupt is unacknowledged. XA
audio/realtime Form 2 sectors can be filtered and
routed as 2,304 compressed bytes to `XaSectorSink`; this component does not
decode or resample XA ADPCM.

## Commands

The slice implements the boot/read/play-oriented behavior of:

- `Nop`, `Setloc`, `Play`, `Forward`, `Backward`, `ReadN`, and `ReadS`;
- `Standby`, `Stop`, `Pause`, `Init`, `SetSession`, `SeekL`, `SeekP`, `GetID`,
  and `ReadTOC`, including distinct acknowledge and completion packets;
- `Mute`, `Demute`, `Setfilter`, `Setmode`, `Getparam`, `GetlocL`, `GetlocP`,
  `GetTN`, and `GetTD`;
- firmware-version test subcommand `0x20` and decoder reset.

Parameter counts and packed-BCD values are checked before state changes.
Unsupported commands return an explicit error packet. Disc type and four-byte
license identity in `GetID` come only from the supplied layout.

## Accuracy boundaries

This tranche intentionally does not claim full controller behavior. It does
not parse BIN/CUE files, synthesize pregaps or index 0, parse subchannel Q,
model multiple sessions beyond session 1, perform SCEx
authentication, descramble sectors, correct ECC, retry damaged reads, decode XA
ADPCM, or implement sound-map playback. `GetQ`, CDDA report packets, most test
subcommands, analog drive variation, and firmware quirks are unsupported.
Fast-forward/backward movement and command/seek/TOC delays are deterministic
integration values rather than optical-mechanical accuracy claims. The caller
must normalize CUE track starts and types and must replace media only while the
device is idle.

PSX-SPX at the provenance-pinned revision was used only as a behavioral
reference. No external implementation code or prose is included here.

## Build and test

```text
cmake -S clean-aot/cdrom -B build-clean-cdrom -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-cdrom
ctest --test-dir build-clean-cdrom --output-on-failure
```

For a GCC or Clang AddressSanitizer/UndefinedBehaviorSanitizer build, add
`-DCLEAN_AOT_CDROM_ENABLE_SANITIZERS=ON` and use a separate build directory.
Both the library and tests compile with conversion, sign-conversion, shadow,
pedantic, and general warnings promoted to errors.
