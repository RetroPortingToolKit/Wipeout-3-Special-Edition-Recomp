# Clean AOT runtime

This component is the deterministic machine seam consumed by generated native
regions. It is not an emulator loop and contains no decoder, interpreter, JIT,
window, host audio backend, or title-specific policy.

The central invariant is that raw CPU-retirement time and native device time
are distinct. A multiplier of `N/D` permits `N/D` times as much recompiled CPU
work while timers, CD-ROM, SIO, SPU, and media remain on native time. Every
cycle-sensitive callback receives the exact raw issue cycle by value; the
runtime converts it with an exact carried rational clock before touching MMIO.

Unknown addresses, unsupported widths, time reversal, malformed configuration,
re-entry, and device rejection fail closed. The special PS1 cache-control
register at `0xFFFE0130` is handled before the ordinary direct-segment bus,
because it is outside the RAM/scratchpad/MMIO/BIOS physical window.

This first integration slice owns:

- immutable caller-provided BIOS storage and accepted memory semantics;
- memory-control, interrupt, DMA, root-counter, SIO0, CD-ROM, GPU, MDEC, and SPU
  register routing;
- DMA endpoint wiring for MDEC, GPU, CD-ROM, SPU, OTC, and RAM;
- exact native-time advancement and level-to-latched interrupt propagation;
- cache-control and deterministic SIO1 bootstrap registers;
- exact-identity binding of sorted generated-region entry tables, with unknown
  PCs, unresolved generated edges, malformed tables, and identity mismatches
  rejected before any fallback can occur;
- independent executable SHA-256 verification, bounded RAM loading, selected
  catalog-root validation, and first generated-region dispatch.

Host presentation, media-file parsing, scanout/raster draining, frame pacing,
audio-device queues, and product policy are separate components.
`GeneratedEpochDispatcher` selects one precompiled executable binding by the
already-authenticated active-image SHA-256. Candidate identities must be sorted,
unique, and match their binding identities. Unknown epochs, duplicate identity
choices, and unknown PCs fail closed; the discriminator never decodes guest
instructions.

`GeneratedCfiDispatcher` checks the source identity and PC plus the destination
identity and PC against a sorted finite policy before binding and entering an
already-generated target. It never decodes or compiles guest bytes. Missing
sites, unknown targets, malformed universes, and identity mismatches fail
closed.

`RuntimeTargetTrace` is an optional fixed-capacity recorder shared by generated,
epoch, and CFI dispatch. It records active epoch selection, executed generated
source/destination PCs, accepted and rejected target attempts, dispatch status,
contiguous sequences, overflow drops, and explicit finalization. It stores no
guest bytes and performs no runtime decoding. Canonical
`clean-aot-runtime-target-trace-v1` receipts aggregate only accepted indirect
attempts into closure sites; execution and rejection telemetry cannot create
compiler authority. Overflow or missing finalization marks evidence incomplete.

`GameProfileTrace` emits canonical `clean-aot-game-profile-trace-v1` evidence
for identity-bound candidate simulation regions without assigning gameplay
meaning to an address. Each exact-100-Hz frame records candidate entry/exit
counts and state-hash digests separately from presentation count and unique
presentation states. The frame is accepted only when its machine receipt proves
the configured CPU retirement multiplier while SPU (441 samples) and CD
(native 0/1/1/1 sector cadence) remain unmultiplied. Synthetic captures may
show repeatable once-per-VBlank observations but always leave
`verified_once_per_vblank` false; only complete authenticated-game evidence can
set it true. Unknown identities/PCs, malformed candidates, timing drift,
overflow, and unfinished frames fail closed.
