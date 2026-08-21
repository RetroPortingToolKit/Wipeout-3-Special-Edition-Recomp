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
address phase, delayed `/ACK` event, original-card
late read ACK, PS1 analog-pad type
byte, command-48 response position, masked mode-lock key, and dynamic digital
reply length implied by the six rumble-map bytes. The old one-motor command is
evaluated from both documented control bytes rather than latching either byte
independently. These are behavioral facts
expressed in original code and adversarial tests, not imported implementation.

The reference documents describe some behavior as measured, uncertain, or
revision-dependent. This tranche therefore records the following explicit
limits rather than inventing compatibility: SIO1 is absent; multitap and
third-party card extensions are absent; undocumented/DualShock2 pressure
commands are bounded; and revision-exact electrical ACK edge placement is
deferred to machine-level traces. The 400-cycle delay, 100-cycle pulse, and
31,000-cycle extra Sony-card read delay are documented nominal values and
remain explicit timing metadata for later trace calibration.

## Implementation boundary

The component owns no disc, BIOS, save file, title constant, host clock,
thread, filesystem, GUI, interpreter, runtime compiler, or dynamic storage.
Callers own card image bytes and choose the machine address mapping. The only
external side effect is an optional caller-supplied durability callback after a
validated card write; the callback itself is outside this core.
