# WipEout 3 SE GooseStation skip-engine applicability audit

Status: audit only. This document does not enable a skip rule, change the
runtime, or change the existing Fast Loading harness.

## Decision

WipEout 3 Special Edition has two plausible generic fast-forward surfaces:
memory-card transfers and pure data CD loads. They should remain separate
policies. A race load is a pure data CD-load case, not a third unconditional
rule. A BIOS shell is governed by BIOS boot options, not by the skip engine.
Game intro rules are not safe to enable from the current evidence, and FMV,
CDDA, and XA activity are hard exclusions from load skipping.

The default decision for this checkout is therefore **keep Fast Loading and
Skip Engine default-off**. The existing startup-card result is encouraging,
but it is not enough to enable default behavior: the paired race-load window,
audio-resume behavior, and skip-specific predicate telemetry are incomplete.

| Surface | Correct GooseStation shape | WipEout 3 SE decision now |
| --- | --- | --- |
| BIOS shell / boot animation | BIOS `Fast Boot`, optionally BIOS `Fast Forward Boot`; not a YAML skip rule | Keep outside Skip Engine. Fast Boot needs a separate retail-BIOS A/B; Fast Forward Boot remains unproven for WipEout. |
| Game intro that is not FMV | A per-game rcheevos rule only after stable RAM evidence | Candidate only; no default rule. |
| Intro FMV / any FMV | Never a load skip; preserve MDEC and audio content | Hard exclude. |
| Memory-card access | Generic `Memory card` detector; no per-game rule | Semantically applicable, but default-on only after the card read and write gates below pass. |
| Pure data CD load | Generic `CD load` detector; no per-game rule | Semantically applicable with `niceness=1`; default-on only after an isolated pure-data trace passes. |
| Race loading | Generic CD detector while the race transition is pure data; optional per-game rule only if a stable game signal is found | Treat as a focused `race-load` gate, not an unconditional rule. Current paired proof is incomplete. |

This distinction matters because GooseStation's Skip Engine and the current
WipEout Fast Loading feature are not the same mechanism. GooseStation advances
hidden emulated frames with SPU output muted and presentation suppressed. The
current WipEout Fast Loading path is guarded as host-pacing-only in the project
handoff. A host-pacing pass cannot by itself prove that a hidden-frame skip is
safe, and a true skip-engine pass should not be judged by requiring audio from
frames that the engine intentionally mutes.

## Primary reference: checked-out GooseStation builder

The source reference used here is the local
`wipeout3se-sources/goosestation-builder` checkout. Its `UPSTREAM_COMMIT` is
`a233ec1fb7f46181e77e6d2a52446ad6f0a7c54f`, and the generated upstream source
tree is checked out at that same revision. The relevant files and line ranges
are listed so a future source refresh can re-audit the predicates:

- `README.md` describes the Skip Engine as a hidden, muted, deterministic
  fast-forward for sequences with no meaningful input. It says the engine is
  disabled by default and that per-game rules are keyed by disc serial.
- `SKIP_TRAINER.md` defines the rule-training contract: positive in-sequence
  marks, negative normal-play marks, unmarked fuzzy ramps, and pointer/bit/value
  detectors. It explicitly allows several rules for one game, but does not
  make a candidate rule safe without varied positive and negative samples.
- `src/core/skip_engine.h:17-32` separates generic CD and memory-card toggles
  from per-game rcheevos rules, and documents the no-input/no-audio/no-FMV
  intent of CD skipping.
- `src/core/skip_engine.cpp:112-176` implements the generic predicates and
  their debounce, grace, cap, and backoff values.
- `src/core/skip_engine.cpp:589-715` establishes priority and termination:
  per-game rule rising edges are checked before generic detectors; a rule ends
  on its stop condition or cap; a generic detector starts only after its
  sustain count and ends after its grace count or cap.
- `src/core/cdrom.cpp:963-982` implements `IsPureDataLoadActive()`.
- `src/core/system.cpp:4074-4081` and `5597-5600`, plus
  `src/core/memory_card.cpp:158-163` and `193-199`, implement the actual
  memory-card access window.
- `src/goosestation-libretro/main.cpp:1957-1983` wires the core options and
  turns on the underlying `MemoryCards/FastForwardAccess` setting when the
  Skip Engine memory-card toggle is selected.
- `src/goosestation-libretro/main.cpp:4402-4413` keeps BIOS Fast Boot and Fast
  Forward Boot in the BIOS category, separate from the Skip Engine options at
  `4490-4532`.
- `src/goosestation-libretro/main.cpp:6606-6644` shows the execution effect:
  skipped frames are executed in a loop, SPU output is muted, frames are not
  presented, and the run-ahead history is discarded afterward.

### Exact generic predicates

For a pure data CD load, all of the following must be true for every frame in
the accepted skip interval:

```text
CDROM::IsPureDataLoadActive() == true
MDEC::IsActive() == false
AnyControllerInput() == false
drawn_primitives_this_frame <= cd_load_niceness  (when niceness > 0)
```

`IsPureDataLoadActive()` itself requires `cdda == false`, `xa_enable == false`,
and one of these drive states:

```text
Reading
SeekingLogical
SeekingPhysical
SeekingImplicit
SpinningUp
ChangingSpeedOrTOCRead
```

The source detector then requires ten consecutive active frames, tolerates six
inactive frames while skipping, and has a 3,600-frame cap. `niceness=1` is the
conservative default. `niceness=0` bypasses the primitive filter and must not
be used for a WipEout default until the negative FMV/scene tests prove it safe.

For a memory-card transfer, the source predicate is:

```text
System::IsMemoryCardFastForwarding() == true
AnyControllerInput() == false
```

The access window is refreshed by real card data-sector reads and writes, not
ordinary presence polling. Each access opens a 30-frame window. The generic
detector requires two active frames, tolerates six inactive frames, caps one
region at 240 frames, and backs off after a cap until the signal has been idle
for 30 frames.

The memory-card predicate does not independently veto CDDA, XA, or MDEC. That
is acceptable only if WipEout telemetry proves that its card wait never overlaps
audio or FMV content. If that overlap exists, memory-card skipping must remain
off for the title until a runtime-level veto is available; a YAML rule is not a
safe substitute.

For a custom per-game rule, the minimum shape is:

```text
start: a WipEout RAM condition whose false->true edge occurs inside the
       non-interactive, non-FMV sequence
stop:  a condition that becomes true before control/content resumes
max_frames: a finite cap sized from repeated traces; no accepted run ends by cap
```

The engine detects the start edge (`start_now && !prev_start`), then checks only
that rule's stop condition or cap. A condition that stays high across repeated
loads, spans a memory-card operation and a CD load, or fires during FMV is not a
valid rule. Rule priority is before generic detectors, so a false positive
custom rule can hide the safer generic detector and must be removed rather than
worked around with an additional rule.

## WipEout 3 SE evidence

The project evidence is from `docs/HANDOFF.md`, the current checked-out
configuration, and the existing debug traces/harness work. The important facts
are:

- The audited launch path uses the user-supplied retail `SCPH1001.BIN` and PAL
  `SCES-02845`; direct launch reaches `game_started=1` through the recompiled
  BIOS path (`docs/HANDOFF.md:7-10`). This establishes a BIOS boundary, not a
  Skip Engine rule.
- A normal CD timing probe delivered 3,840 exact data sectors with no early,
  late, or lost interrupts in its observation window
  (`docs/HANDOFF.md:20-21`). This is a baseline for CD correctness, not proof
  that hidden-frame skipping is safe.
- The deterministic menu-to-race route reaches title, memory-card UI, main
  menu, vehicle class, ship, and an on-track race. One accelerated route
  recorded 32 sequential card reads, sectors 0-31, with `data_idx=128`, and
  CD/XA activity through approximately frame 8,055
  (`docs/HANDOFF.md:177-189`). The mixed CD/XA observation must not be used as
  evidence for a pure-data race skip.
- The anchored startup-card comparison is recorded as passing, but the paired
  race-load artifacts still need an equal route length and overlapping endpoint
  window (`docs/HANDOFF.md:254-266`).
- A synchronized probe matched the memory-card traces and the fingerprint
  prefix, but diverged at frame 2,105 in a RAM-write hash while the routes had
  different lengths and non-overlapping CD rings
  (`docs/HANDOFF.md:241-248`). This is a synchronization/evidence boundary,
  not a declared equivalence failure or pass.
- Fast Loading has already exposed host-audio failure modes: an earlier 4x
  route produced 155,084 bridge overflow drops; the overflow-safe sink removed
  drops but the accelerated route still had substantially higher output
  underruns and 54 SPU trace underruns (`docs/HANDOFF.md:160-170`). Any default
  gate must check audio after the skipped interval, not only guest-clocked
  production during it.
- PAL x2 was enabled in one earlier paired route and explicitly confounded the
  fingerprint count; the handoff requires Fast-Loading-only runs with PAL-100
  off before drawing a timing conclusion (`docs/HANDOFF.md:223-230`). All tests
  below hold PAL-100 and unrelated presentation changes fixed.

These facts support applicability, but not default enablement. In particular,
the current evidence does not prove that WipEout's race transition is pure data
for its whole candidate interval, nor that the accelerated endpoint and the
post-load continuation have identical CD/IRQ/DMA, card, guest-state, and audio
behavior.

## Comparison with the current equivalence harness

The current checked-out harness is `tools/fast_loading_equivalence.py`, with
`tools/compare_guest_load_traces.py` providing the focused `startup-card` and
`race-load` semantic gate. It already provides useful foundations:

- `savestate_status` generation is used as a committed restore receipt before
  capture begins.
- Each capture resets CD/card trace channels, records an anchor frame, advances
  a bounded guest-frame window, and captures card reads, card writes, card
  transactions, CD sectors, CD trace/IRQs, frame fingerprints, audio counters,
  and audio events.
- Process-local sequence counters are ignored while entry order and guest
  visible fields remain strict. The focused comparator accepts card evidence
  only for `startup-card`, and fingerprint/CD evidence only for `race-load`.

It is not yet a skip-engine applicability harness:

1. It does not capture Skip Engine `Started`, `Stopped`, or `StoppedCap` events,
   the source (`Memory card`, `CD load`, or named rule), the active rule name,
   the cap, or the number of hidden frames.
2. It does not capture the predicate inputs that justify a skip: drive state,
   `cdda`, `xa_enable`, MDEC activity, controller button mask, primitive delta,
   memory-card access kind/slot/sector, or the per-frame start/stop state.
3. `fast_loading_equivalence.py` compares a fixed post-restore frame window and
   requires exact audio deltas/events. That is appropriate for the current
   host-pacing Fast Loading proof, but a true GooseStation skip intentionally
   mutes the hidden interval. The skip-engine gate must compare state and media
   after the interval, while separately asserting that no CDDA/XA/MDEC content
   was hidden.
4. The fixed-window capture can retain a valid-looking equal count without an
   equal semantic endpoint. The handoff already records this failure mode:
   different anchors/route lengths and non-overlapping CD rings made one
   fingerprint boundary non-decisive. A race gate needs a committed endpoint
   receipt, not only `frames=N`.
5. The focused comparator checks selected card fields for startup and selected
   fingerprint/CD fields for race; it does not by itself prove skip-source
   correctness, card writes plus card-file state, audio recovery, or the FMV
   negative case. The richer capture fields in `fast_loading_equivalence.py`
   should be retained, but the audit metadata below is also required.

### Required audit telemetry

The existing runtime/harness must expose this as an audit artifact before a
default toggle is considered. This is a telemetry requirement, not a request to
modify the runtime in this audit commit.

Per skip interval:

```text
source: rule | cd_load | memory_card
rule_name: string (empty for generic detectors)
event: started | stopped | stopped_cap
start_guest_frame, end_guest_frame, skipped_frames
cap, min_sustain, grace, cd_load_niceness
```

Per frame from `start_guest_frame` through `end_guest_frame`:

```text
controller_mask_all_ports
drive_state
cdda
xa_enable
mdec_active
drawn_primitives_delta
memory_card_access: none | read | write, plus slot/sector/data index when present
CD sector/command/IRQ/DMA identity and order
```

The pass/fail rules for that artifact are exact:

- `source=cd_load` requires `cdda=false`, `xa_enable=false`, `mdec_active=false`,
  `controller_mask_all_ports=0`, a whitelisted drive state, and primitive delta
  at or below niceness on every skipped frame. No `StoppedCap` is accepted.
- `source=memory_card` requires a real card read/write before the detector
  window, no controller input, no CDDA/XA/MDEC overlap, and exact card
  transaction identity/order. No `StoppedCap` is accepted.
- Any `cdda=true`, `xa_enable=true`, `mdec_active=true`, or XA/CDDA delivery
  during a candidate interval vetoes load skipping for that interval. This is
  true even if the game is showing a static-looking loading screen.
- A custom rule requires a rising start edge, a stop before interactive/content
  frames resume, a finite cap, zero cap terminations across repetitions, and
  stable WipEout RAM semantics across varied routes. The rule must never be
  the first detector to fire during the FMV negative test.
- After every accepted skip, compare the endpoint fingerprint and a short
  continuation. Audio from hidden frames may be muted by design, but audio
  must resume at the expected rate with no new SPU/bridge underruns or
  overflows, and all non-hidden CDDA/XA/FMV content must remain present.

## Proposed test matrix

All rows use the same authenticated PAL disc and retail BIOS, separate writable
save roots for normal and accelerated branches, a single pre-operation
savestate, PAL-100 off, and identical input. Run each row at least three times
from a fresh restore; a proposed default requires zero mismatches and zero cap
terminations in every repetition. Repeat the load rows across representative
tracks, ships, classes, and save states rather than treating one route as game
coverage.

| ID | Start / operation | Isolated toggles | Required evidence | Acceptance |
| --- | --- | --- | --- | --- |
| BIOS-SHELL | BIOS shell to `game_started`, with and without the BIOS boot optimization | Skip Engine off; A/B BIOS Fast Boot; keep Fast Forward Boot separate | BIOS/boot mode, first game-start receipt, CD/IRQ order, no Skip Engine event | Fast Boot may be separately accepted for the supplied retail BIOS if both branches start identically. No BIOS YAML rule; Fast Forward Boot stays off until its own A/B passes. |
| INTRO-STATIC | Any non-FMV, non-interactive game intro or title transition | Master on, generic toggles off, candidate rule isolated by name | Trainer positives/negatives, RAM candidate, rule start/stop edges, MDEC/CDDA/XA veto fields | Candidate only after varied convergence, no cap end, no audio/content overlap. No current default rule. |
| INTRO-FMV | Every intro/movie path, including XA audio and 24-bit/MDEC presentation | Master and generic toggles on to exercise the negative guard | `mdec_active`, `xa_enable`, CDDA/XA delivery, presentation cadence, skip events | Zero skip events while FMV/CDDA/XA is active; content/audio continues. Any skip is an immediate veto. |
| CARD-READ | Existing save-card startup route, checkpointed before the first card command | Master on, memory-card on, CD-load off, no custom rule | Card reads, card transactions, card IRQs, endpoint fingerprint, 300-frame continuation, audio-after counters | Exact card command/sector/checksum/data fields and order; equal endpoint/continuation; no card write; no cap; no audio recovery error. |
| CARD-WRITE | Save/update route with an isolated writable card image | Same as CARD-READ | Card writes and transaction bytes, final card-image hash/changed sectors, endpoint fingerprint, continuation | Exact write addresses/values/sector order and final card state; normal and accelerated images must match; no cap. |
| CD-DATA | Menu or content load known to request only data sectors | Master on, CD-load on, niceness=1, memory-card off, no custom rule | Per-frame pure-data predicate, drive state, primitive delta, CD sectors/commands/IRQs/DMA, endpoint fingerprint | All guard fields pass; no CDDA/XA/MDEC/input; exact data-sector and IRQ/DMA sequence; stop normally before cap. |
| RACE-LOAD | Pre-race checkpoint through track load and into a fixed on-track endpoint | Same as CD-DATA; race is a gate label, not a new detector | Equal committed route endpoint; fingerprint, CD sector/command/IRQ/DMA, audio after resume, card side effects | Exact loaded-race endpoint plus short continuation. Any XA/FMV before the endpoint splits the route and is not skipped. |
| INTERLEAVED | Card wait -> data load -> menu/intro -> race, with a separate FMV branch | Exercise one generic toggle at a time, then both; reset between branches | Detector source/edge timeline and all veto fields | No detector carries state into the next phase; FMV branch never skips; each accepted region ends normally. |
| CAP-BACKOFF | Continuous card-browser reads, repeated seeks, timeout/retry, and aborted load | Generic toggles on, normal input at the boundary | `StoppedCap`, backoff/idle telemetry, input and media traces | No default if cap is reached for a normal WipEout route. After a cap, detector stays out until its idle-clear condition; user input remains interactive. |

### Endpoint comparison shape

For `CARD-READ` and `CARD-WRITE`, the endpoint is the first frame after the
card transaction completes and the game has returned to its known menu state.
Compare card reads, writes, transactions, SIO/card IRQs, a state fingerprint,
and 300 subsequent guest frames. The startup-card gate from
`tools/compare_guest_load_traces.py` is a useful subset, but it must not replace
the write and continuation checks.

For `RACE-LOAD`, the endpoint is the first stable on-track frame after the
loading transition, not the end of an arbitrary fixed-frame capture. Capture a
short continuation long enough to expose deferred CD/IRQ/XA effects. If the
route contains an intro FMV or CDDA/XA stream, split before that content and
test the data-load portion separately; do not mark the entire route as a race
load.

## Default-enablement gate

No default change is justified until all of the following are true:

1. BIOS-SHELL has its own retail-BIOS A/B result. It is not represented as a
   Skip Engine rule.
2. INTRO-FMV has a negative result: no skip event under all skip toggles, while
   MDEC, CDDA/XA, and presentation/audio telemetry remain continuous.
3. CARD-READ and CARD-WRITE pass with exact card state and transaction
   evidence, no cap, no media-content overlap, and clean post-skip audio.
4. CD-DATA passes with `niceness=1` and the full pure-data predicate on every
   skipped frame.
5. RACE-LOAD passes at equal committed endpoints and in the short continuation
   across representative race content. A route that only reaches the race is
   not enough; its deferred CD/IRQ/DMA effects must match.
6. The same runs have PAL-100, FMV-skip, PGXP/GPU, texture, audio, and other
   unrelated changes held constant. No result confounded by a frame-rate or
   presentation mod counts as Fast Loading evidence.
7. Every accepted interval ends by its stop/grace condition, never by cap, and
   no run reports new SPU or host bridge underruns/overflows after the interval.

If the gate passes, the least-surprising WipEout policy is to enable the two
generic surfaces independently (memory-card access and pure-data CD loads at
niceness 1), keep FMV/CDDA/XA excluded, and add no custom BIOS or race YAML
rule unless later trainer evidence proves one is needed. Until then, keep the
existing Fast Loading selection default-off and treat this document as the
acceptance contract rather than as evidence that the feature is ready.

## Reference files

- GooseStation builder source checkout: `wipeout3se-sources/goosestation-builder`
  (`UPSTREAM_COMMIT=a233ec1fb7f46181e77e6d2a52446ad6f0a7c54f`)
- WipEout project evidence: `docs/HANDOFF.md`
- Retail BIOS boundary: `CMakeLists.txt:27-31`
- Current fixed-window harness: `tools/fast_loading_equivalence.py`
- Current focused gate comparator: `tools/compare_guest_load_traces.py`
- Current harness regression tests: `tools/test_fast_loading_equivalence.py` and
  `tools/test_compare_guest_load_traces.py`
