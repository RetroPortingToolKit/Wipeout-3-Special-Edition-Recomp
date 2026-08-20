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

The reference documents describe some behavior as measured, uncertain, or
revision-dependent. This tranche therefore records the following explicit
limits rather than inventing compatibility: SIO1 is absent; multitap and
third-party card extensions are absent; undocumented/DualShock2 pressure
commands are bounded; and exact electrical ACK edge placement is deferred to
machine-level traces.

## Implementation boundary

The component owns no disc, BIOS, save file, title constant, host clock,
thread, filesystem, GUI, interpreter, runtime compiler, or dynamic storage.
Callers own card image bytes and choose the machine address mapping. The only
external side effect is an optional caller-supplied durability callback after a
validated card write; the callback itself is outside this core.
