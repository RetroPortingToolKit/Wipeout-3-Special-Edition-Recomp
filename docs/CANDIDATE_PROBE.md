# Candidate visual and cadence probe

`tools/run_candidate_probe.ps1` launches one exact build from the repository
root, skips the intro/logo sequence with targeted `X` key pulses, captures the
exact composed post-aspect renderer output, and records runtime cadence diagnostics. Each run gets a
new ignored `artifacts/candidate-probe/<timestamp>/isolated-state` directory.
It never reads or writes the player's cards, option persistence, or savestates.

Run the current direct-stock candidate from PowerShell 7:

```powershell
pwsh -File tools/run_candidate_probe.ps1
```

To probe another executable without changing the defaults:

```powershell
pwsh -File tools/run_candidate_probe.ps1 `
  -Executable "build-other/Wipeout_3_Special_Edition__Recompiled.exe" `
  -DurationSeconds 36
```

The harness refuses to run if that exact executable path is already active. It
requires all 14 cue track files to exist, then passes the game config, cue,
retail BIOS, and isolated writable-state
directory as separate process arguments, so spaces in media paths cannot be
split by Windows quoting. The source repository is the working directory so
the source-side HD texture and translation assets are resolved consistently.

Every run records:

- `run.json`: exact paths, cue track inventory, SHA-256 hashes, argument array,
  environment, bounds, final verdicts, capture HWND/PID/title/class/client
  rectangle provenance, the unique debug-port/PID binding, and separate hashes
  for the reviewed source `mods/preloaded/state.toml` and the state the runtime
  actually selects at `<resolved executable directory>/mods/state.toml`;
- `runtime.stdout.log` and `runtime.stderr.log`: fresh diagnostic receipts;
- `cadence.json`: median guest, SPU, and active-CDDA rates after warm-up, plus
  underrun/overflow deltas and individual checks;
- `progression.json`: complete 100-VBlank windows containing both the
  fixed-selector scheduler's logical-clock/VBlank/wall ratios and the busiest
  stable ship object's update ratio;
- `race-clock.json`: authoritative title centiseconds versus monotonic wall
  time, including Hueponik's exact alternating `0,2` store-delta contract;
- `captures/frame-*.png` and `motion.json`: visible-window evidence and sampled
  frame-difference measurements.

Capture does not depend on foreground focus and never copies pixels from the
desktop. `-CaptureMode` makes the evidence level explicit:

- `Auto` (default) uses `PresentShot` when the candidate exposes an owned debug
  listener and otherwise falls back to `Window`, preserving Release probes
  compiled with `PSX_DEBUG_TOOLS=OFF`;
- `PresentShot` requires the exact composed renderer surface and fails closed if
  the listener/capture is unavailable; this mode is mandatory for final visual,
  HUD, camera, and aspect-ratio acceptance;
- `Window` explicitly requests the PID-owned `PrintWindow` fallback, useful for
  ordinary Release smoke/motion checks but not final aspect evidence;
- `None` performs a cadence-only run without visual capture or a motion gate.

For debug-enabled candidates the harness allocates one unused dynamic loopback port in the bounded
`49152..65535` range, passes it to this candidate with `--debug-port`, and proves
the resulting listener is uniquely owned by the launched PID. It also proves
that the HWND belongs to that PID and its title begins with the unique per-run
title; ownership and client geometry are checked around every capture.

Each frame uses the runtime debug server's `present_shot` command, which is
fulfilled inside the renderer after the display buffer has been fitted to the
window. This is the exact composed surface the player sees, including
widescreen/letterbox fitting and OSD, rather than a pre-aspect VRAM buffer or a
Windows window-manager approximation. The harness samples `present_shot_seq`
before staging and polls for exactly the next successful completion. Connection
and completion waits are bounded; a wrong sequence, `wrote=0`, missing or
unreadable PNG, changed window identity/geometry, or black/effectively empty
image fails closed. Each frame records the port owner, sequence values,
monotonic request/completion times, size, and SHA-256.

This matters because desktop capture could record an overlapping application,
while `PrintWindow` can return a pre-compositor or stale surface depending on
the active backend. The former is never used; the latter is labeled as fallback
evidence and is never accepted for final aspect-ratio verification.

The default cadence gate expects 100 Hz guest presentation, 44.1 kHz SPU/CDDA,
at least two post-warm-up samples, active non-silent CDDA, and zero audio fault
deltas. The separate timing gate requires at least two complete windows with
exact fixed-selector state: 100 scheduler calls, 25 logical title ticks, and
 100 race centiseconds per 100 VBlanks, with raw rate 1 and coefficient `0x0862`.
It reports VBlanks, logical ticks, and race centiseconds per wall second, so a
selector-50/rate-2 hybrid is visible even when the FPS overlay says 100. The progression
gate additionally requires one stable ship object to advance once per guest
 VBlank (ratio `0.90..1.10`). The authoritative gameplay-speed gate separately
 observes the stock total-race-clock store at `0x8010DECC`: each active-race
 window must contain only the exact `0,2` centisecond deltas, average
 `0.99..1.01` centiseconds per store, and advance game time within `0.95..1.05`
 of monotonic wall time. A loading/title stall emits no eligible window and
 fails closed.

All three timing/progression signals are Release-safe and read-only. The
harness explicitly sets `PSX_WIP3_PROGRESS_DIAG=1` and
`PSX_WIP3_RACE_CLOCK_DIAG=1`; ordinary launches do not collect or print them.
The timing line samples the native scheduler counters which own Hueponik's
fixed PAL100 phase/wrap clock and compares them with 100 guest VBlanks plus the
same wall interval. The companion probe hooks the stock per-ship simulation
owner at `0x800F04E8`, groups calls by the guest `a0` object pointer, and
compares the busiest object against 100 guest VBlanks. Object updates alone are
not accepted as a real-time speed proof: one update per VBlank can coexist with
the wrong selector clock. Windows are eligible only when updates span their
beginning and end, excluding race/demo transitions. `MOVING` remains a separate
visual check and is not used as a speed proxy.

For a screen without CD audio, `-AllowNoCdda` makes the absence of active CDDA
non-fatal while retaining the SPU and guest-rate checks. Do not use that switch
for the menu/music acceptance run.

The focused progression parser fixtures are independent of the game process:

```powershell
py -3 -m unittest tools/test_summarize_wip3_progress.py -v
```

They prove that two `1.00x` title windows pass, two `0.40x` windows fail even
when the fixture also reports perfect host/audio cadence, and a log with no
eligible title window fails closed.

The race-clock fixtures also reject wrong per-store delta semantics:

```powershell
py -3 -m unittest tools/test_summarize_wip3_race_clock.py -v
```

The final speed receipt is deliberately stricter than the memory probe. Run the
probe with `-CaptureMode PresentShot`; `Auto`/`Window` evidence is marked
`INVALID_CAPTURE_EVIDENCE`. After visually confirming that at least three
accepted composed frames show the post-start
chase view and a legible stock HUD timer, transcribe those displayed values:

```powershell
py -3 tools/verify_visible_race_timer.py `
  artifacts/candidate-probe/<run>/visible-race-timer.json `
  --reading 2=0:05.60 --reading 4=0:09.61 --reading 6=0:13.60 `
  --output artifacts/candidate-probe/<run>/visible-race-timer-review.json
```

The verifier rehashes the exact captured images, binds each reading to its
monotonic `Stopwatch` timestamp, requires every interval to be `0.95..1.05x`,
and requires the aggregate to be `0.99..1.01x`. Do not invoke it for a flyby,
start pan, loading screen, demo, or any frame without the visible race HUD.
Its focused fixtures include a false `2x` clock and a tampered-image rejection:

```powershell
py -3 -m unittest tools/test_verify_visible_race_timer.py -v
```

The capture contract has a process-free static regression test:

```powershell
py -3 -m unittest tools/test_candidate_capture_contract.py -v
```

It rejects reintroduction of desktop capture, requires explicit capture modes,
and verifies the composed-surface protocol, bounded unique debug port, PID
ownership, exact completion sequence, provenance fields, hashes/timestamps,
Release fallback, and empty-frame guards.
