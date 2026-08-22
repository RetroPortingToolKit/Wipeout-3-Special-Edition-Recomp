# SIO0 provenance and clean-room boundary

The implementation was written independently from behavioral documentation.
No emulator, recompiler, generated game code, or prior project implementation
is a build input or source template.

## Behavioral reference

- PSX-SPX documentation at
  `psx-spx/psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d`.
  The inspected chapters were `serialinterfacessio.md` and
  `controllersandmemorycards.md`, used for register widths, synchronous
  eight-bit transfers, SIO0 baud-factor arithmetic, FIFO/IRQ descriptions,
  `/CS`/address selection, controller command framing, DualShock configuration
  commands, rumble mapping, memory-card framing, raw-card geometry, and frame
  checksums. Its prose and code are not copied into this component.

The independent review rechecked the register table and protocol byte tables
against that pinned revision. In particular, the implementation now uses the
documented `0x0A` control offset, 16/32-bit RX preview/removal rules,
free-running half-bit baud timer, disabled-TX overwrite rule, single-shot
address phase, delayed `/ACK` event, original-card late read ACK, PS1
analog-pad type byte, command-48 response position, masked mode-lock key, and
dynamic digital reply length implied by the six rumble-map bytes. All mapping
values `00h..FEh` are legal protocol bytes; only the two documented motor
values have effects in this model. The old one-motor command is evaluated from
both documented control bytes rather than latching either byte independently.
These are behavioral facts expressed in original code and adversarial tests,
not imported implementation.

The timing profile is deliberately evidence-labelled. The pinned controller
signal notes require a response after the first roughly 100 CPU cycles, give a
6.8--13.7 microsecond controller delay window, and require a low ACK pulse of
at least about 2 microseconds. The reviewed measured profile uses about 450
cycles for a controller, about 170 cycles for a memory card, and about 96
cycles for the pulse. The same notes document an additional approximately
31,000 cycles before the late ACK in an original Sony-card read. The SIO bus
waits for that ACK window before clocking a queued byte, matching the
peripheral-request nature of `/ACK`; these values remain replaceable until a
revision-specific hardware trace is available.

Configuration-mode communication refreshes the documented approximately
one-second watchdog. Expiry and the physical Analog-button boundary reset
rumble/mode state as described by the controller notes, including the
post-configuration `00h` replacement for the usual `5Ah` ID-high byte. The
watchdog remains active after command 43 returns to normal mode, is advanced
by the SIO bus clock even between byte events, and the
watchdog and button clock fields are part of the in-memory snapshot so a
restored transaction cannot silently change mode because host time elapsed.

The reference documents describe some behavior as measured, uncertain, or
revision-dependent. This tranche therefore records the following explicit
limits rather than inventing compatibility: SIO1 is absent; multitap and
third-party card extensions are absent; undocumented/DualShock2 pressure
commands are bounded; and revision-exact electrical ACK edge placement is
deferred to machine-level traces. The 450-cycle controller delay, 170-cycle
memory-card delay, approximately 96-cycle pulse, and 31,000-cycle extra
Sony-card read delay remain explicit timing metadata for later trace
calibration.

## Implementation boundary

The component owns no disc, BIOS, save file, title constant, host clock,
thread, filesystem, GUI, interpreter, runtime compiler, or dynamic storage.
Callers own card image bytes and choose the machine address mapping. The only
external side effect is an optional caller-supplied durability callback after a
validated card write; the callback itself is outside this core. No allocations
or filesystem calls occur on the byte-transfer, watchdog, or snapshot paths.
