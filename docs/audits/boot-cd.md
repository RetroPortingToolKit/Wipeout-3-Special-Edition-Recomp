# BIOS, disc, and loading-path audit

Audit date: 2026-08-22. This is a read-only audit of the launch contract and
the current boot/load evidence. No launcher, runtime, generator, CMake, game
config, active mod, or existing equivalence-harness file was changed.

## Decision summary

| Question | Decision | Confidence |
| --- | --- | --- |
| Can the BIOS splash/intro be bypassed? | The mechanism is structurally safe for this framework: it is a one-shot BIOS shell return, while real kernel initialization and `SYSTEM.CNF`/EXE loading continue. WipEout has only a boot smoke receipt, not a normal-vs-skip architectural-state proof. | Mechanism: high. Title equivalence: open. |
| Why do disc launches fail? | The build-time EXE/disc paths, runtime-selected path, cue-relative `FILE` names, multi-track TOC, exact disc identity, and exact retail BIOS identity are separate gates. A valid Track 01 binary is not a substitute for this title's 14-track cue. | High. |
| Can Fast Loading default on? | Not yet. The host-pacing design is the right candidate, but the retained evidence does not pass the race-load and audio gates. Keep the default decision off until the gates below pass repeatedly. | High. |
| What does GooseStation imply? | Fast Boot and async CD read-ahead are convenience/host-I/O features, not proof that a title's guest initialization or CD timing is equivalent. Treat GooseStation skip rules as title-specific experiments. | High for the boundary; title-specific behavior still needs testing. |

## 1. What is actually being launched

The project has two different path consumers:

1. `game.toml` is used by generation/build. Its `[game]` block names
   `disc/SCES_028.45` and a relative `.cue` (`game.toml:4-13`). The README's
   development flow also passes a project-relative cue to generation
   (`README.md:68-76`). These files are not bundled; the README explicitly
   says disc images are gitignored and retail BIOS dumps are not redistributed
   (`README.md:56-60`).
2. The runtime/launcher mounts the user-selected disc and BIOS. This is the
   path that must be tested after moving the executable, changing the working
   directory, or selecting a `.bin` instead of the `.cue`. A hard-coded probe
   path is not portable: `disc_probe.json:2` contains an `/home/alex/...`
   absolute path.

The current title configuration deliberately requires the `SCPH1001` retail
BIOS (`game.toml:39-45`). The supplied project evidence records a direct launch
with the user-provided retail BIOS and PAL cue reaching `game_started=1`, with
neither disc nor BIOS bundled (`docs/HANDOFF.md:8-10`). That is valid smoke
evidence for the selected inputs, not proof of all load routes.

The disc identity is more than “a file opened”: the probe records serial
`SCES-02845`, boot EXE `SCES_028.45`, a 14-track layout, Track 01 size/digests,
and a TOC fingerprint (`disc_probe.json:3-26`). The corresponding network
mount gate also requires a cue, 14 tracks, and that TOC fingerprint
(`game.toml:426-429`). A raw Track 01 can contain the ISO filesystem while still
being insufficient for CD-DA tracks, pregaps, or TOC validation.

### Failure classes

| Symptom | Likely cause | Why it matters |
| --- | --- | --- |
| Generation cannot find the EXE or cue | `game.toml` relative path is evaluated from the wrong project/root, or the extracted `SCES_028.45` is absent. | Generation input and runtime mount input are separate; a successful build does not prove the selected runtime disc is the same one. |
| “No disc”, black screen, or BIOS CD menu | The selected path is empty, stale, inaccessible, or cannot be opened. | BIOS/HLE skip does not manufacture a disc. A failed mount must remain a visible launch failure, not silently become an empty drive. |
| Game starts but CD audio/track-dependent behavior is wrong | Track 01 `.bin` was mounted without its owning multi-track cue, or the cue points at missing/renamed files. | The data sector stream may work while CD-DA, pregap, or track numbering does not. |
| Cue works on one machine but not another | A `FILE` entry is relative to the cue's directory, has a case/backslash mismatch, or names WAVE/MP3 instead of a fixed-geometry BINARY payload. | A cue is a table of contents, not merely a filename alias. |
| Mods are ignored/rejected | The mounted data or EXE hash is not the package target. | A path can be readable yet still be the wrong revision for guarded patches/overlays. |
| Launch succeeds, later load hangs | The initial data track is correct but a later streamed overlay/XA/CD-DA path or CD timing assumption is not. | Boot success covers only the prefix; it does not validate menu, memory-card, race, or FMV routes. |

The framework's path resolver is intentionally forgiving when present: a cue
whose BINARY payload is missing may fall back to a sibling raw image, and a
raw image that belongs to a cue may be upgraded to that cue. That improves
“picked the wrong file” usability, but a cue fallback loses the cue's audio
track layout and must not be treated as equivalent to mounting the full cue.
The authoritative rule for this title remains: keep the cue and every file it
names together, and verify the 14-track identity before using enhancement
packages.

## 2. BIOS intro skip versus game initialization

These are separate controls:

- `bios_hle`/the deprecated `fast_boot` alias skips the BIOS shell animation.
- `auto_skip_fmv`/the Skip FMV mod skips game-owned opening or in-game movies.
- Fast Loading changes host pacing during recognized load windows.

They must not be enabled or accepted as one combined “fast boot” result.

The framework's HLE plan has two independent axes. The shell skip needs only the
selected BIOS's `shell_entry_phys` and a loaded game entry; kernel-call HLE
additionally needs a `DeliverEvent` return anchor. The implementation's one-shot
shell route returns with `$v0 = 0` at the shell entry, then lets the real
recompiled BIOS continue with kernel initialization and game EXE loading. In
other words, it models “the shell returned immediately,” not “jump to a guessed
game state.” The framework documents and unit-tests this separation in
[`BIOS_SELECTION.md`](https://github.com/mstan/psxrecomp/blob/master/docs/BIOS_SELECTION.md)
and the `bios_hle_plan` decision matrix.

That makes bypassing the BIOS intro a sound *mechanism* for this product, but
not yet a proven WipEout equivalence claim. The required title gate is:

1. Run clean processes with the same exact retail BIOS, cue, EXE/build, card
   image, and enhancement selection.
2. Compare BIOS-HLE-off (real BIOS intro) and BIOS-HLE-on at the same first
   interactive game state.
3. Compare guest RAM, CPU/GTE state, active overlay identity, VRAM, SPU RAM and
   registers, CD controller/FIFO/IRQ state, card bytes, and pending events.
   Exclude only an explicit allowlist of independently justified clocks.
4. Continue through the first card read and one representative game load. A
   matching title screenshot or `game_started=1` is only smoke evidence.

Until that comparison exists, the defensible statement is: “the BIOS shell can
be skipped without intentionally bypassing game initialization; WipEout's
post-skip state equivalence is not proven.”

## 3. Fast Loading and CD loading

The framework distinguishes the candidate host-pacing mod from emulated CD
speed:

| Feature | What changes | Correctness boundary |
| --- | --- | --- |
| Fast Loading (host pacing) | Runs the normal guest work faster in wall time during a recognized data-load window; it is intended to preserve guest frames, CD IRQs, callbacks, and sector order. | Host pacing/audio/presentation must not cause observable damage; XA/CD-DA and FMV paths must remain protected. |
| CD Speed / instant | Delivers CD sectors earlier in guest time by shortening sector deadlines or using a bounded burst. | This changes when the game observes CD interrupts and is a separate, riskier feature. It must not be folded into the Fast Loading verdict. |
| Async read-ahead | Reads host sectors in the background so a later request may hit a ready buffer. | It is safe only if the guest still waits for the same emulated sector deadline and receives the same FIFO/IRQ/DMA sequence. |

The existing harness is shaped correctly for the first row: it restores an
identical savestate, waits for the completion receipt, clears post-restore
rings, advances the same number of guest frames, ignores only process-local
sequence counters, and compares CD/card/frame/audio evidence
(`tools/fast_loading_equivalence.py:2-28`, `:215-260`, `:313-350`). Its unit
tests passing proves the comparator behavior only; it is not a live equivalence
receipt.

The durable handoff evidence is still a fail for a default-on decision:

- The anchored startup-card comparison passes.
- The race-load artifacts still need an equal route length and overlapping
  endpoint window (`docs/HANDOFF.md:254-266`).
- A synchronized probe found a RAM-write fingerprint difference at frame 2,105
  after a matching prefix, while its retained CD rings were from different
  frame windows; that localizes a synchronization/proof problem, not a clean
  pass (`docs/HANDOFF.md:241-250`).
- The first accelerated route produced 155,084 host bridge overflow drops. The
  discard-only turbo sink removed the overflow drops, but the route still had
  materially worse cumulative output underruns and 54 SPU trace underruns
  (`docs/HANDOFF.md:160-169`).

There is also a configuration inconsistency that must not be mistaken for
validation: the current untracked `mods/state.toml` selects Fast Loading at 4×,
while the handoff and release state now select the feature by default, and this
worktree's project-owned preloaded catalog contains only the two WipEout
title-specific package directories. Until the catalog, persisted state, and
handoff all agree and the live gates pass, keep the release selected but treat
runtime equivalence as unproven.

### Default-on gate

Fast Loading may become default-on only after repeated clean runs show all of
the following:

- identical BIOS/disc/EXE/config identities and separate writable card roots;
- matching startup-card command/response/card-data traces;
- matching race-load frame fingerprints plus CD command/sector/IRQ/DMA order
  and a short continuation for deferred XA/CD effects;
- matching guest SPU/CDDA production and no new host overflow/underrun or
  audible transition defect;
- cold-card, warm-card, retry/error, missing-card, and unrecognized-load cases
  fail closed to normal timing;
- repeated runs are deterministic, and the package/manifest selection is the
  same in both branches.

The existing focused gates are the right shape: `startup-card` uses the card
trace, and `race-load` uses fingerprints plus CD traces. Do not use a generic
boot screenshot, a shorter wall-clock load, a synthetic comparator test, or a
non-overlapping CD ring as a default-on receipt.

## 4. GooseStation implications

GooseStation is a DuckStation-derived libretro core. The relevant upstream
semantics are useful as boundaries, not as WipEout evidence:

- DuckStation's `-fastboot` is a BIOS boot-mode shortcut; SwanStation documents
  Fast Boot as skipping the BIOS splash/intro and notes that fastboot can bypass
  the normal region check. That is broader than a proof that a title sees the
  same BIOS-initialized state. This project should retain its narrower
  “return from the BIOS shell, then run the real BIOS load path” contract.
- DuckStation's async reader fills a finite host-side sector buffer on a worker
  thread. A requested LBA is consumed on a buffer hit; on a miss, the reader
  discards the readahead and seeks to the requested LBA, and
  `WaitForReadToComplete()` waits for the buffer or a seek error. That is
  asynchronous host I/O, not asynchronous guest CD delivery. It does not by
  itself justify early guest IRQs, early DMA, or skipping CD deadlines.
- The public GooseStation skip-engine-rules repository says its per-game rules
  may not be extensively tested. Such rules are useful hypotheses for a title,
  but cannot serve as a generic equivalence oracle or a reason to default this
  game's CD acceleration on.

The practical translation for WipEout is: borrow the separation of host
read-ahead from guest-visible CD behavior; do not borrow a global fast-boot or
skip rule. Keep BIOS shell skip, game FMV skip, host pacing, and emulated CD
speed as independently gated features.

References: [PSXRecomp BIOS selection](https://github.com/mstan/psxrecomp/blob/master/docs/BIOS_SELECTION.md),
[PSXRecomp load schema](https://github.com/mstan/psxrecomp/blob/master/docs/config_schema.md),
[SwanStation region/fast-boot notes](https://github.com/libretro/swanstation#region-detection-and-bios-images),
[DuckStation async reader](https://github.com/stenzek/duckstation/blob/master/src/core/cdrom_async_reader.cpp),
[DuckStation command-line fastboot](https://github.com/stenzek/duckstation/wiki/Command-Line-Arguments),
and [GooseStation skip rules](https://github.com/gtaforever00/goosestation-skip-engine-rules).
