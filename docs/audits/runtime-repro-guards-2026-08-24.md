# Runtime reproducibility guard audit — 2026-08-24

The runtime now has a read-only audit helper at
`tools/verify_runtime_repro.py`. It checks the exact executable path (not a
basename or broad process-name match), requires exactly one candidate by
default, verifies the candidate's explicit `--bios` and `--memcard-dir`
arguments, bounds writable paths to a dedicated session root, and rejects a
candidate predating the recorded run boundary. It never starts, stops, or
cleans up a process. Use a process snapshot for deterministic review:

```text
python tools/verify_runtime_repro.py \
  --process-snapshot process-snapshot.json \
  --executable <session>/build/Wipeout_3_Special_Edition__Recompiled_pgxp.exe \
  --bios <session>/psxrecomp/bios/SCPH1001.BIN \
  --memcard-dir <session>/saves \
  --session-root <session> \
  --state-dir <session>/saves/scph1001 \
  --marker-root <session>/build \
  --run-start-epoch <epoch>
```

Omitting `--process-snapshot` performs the same read-only process-table query
through Windows CIM. A run boundary is mandatory when marker files are
audited: `psx_last_run_report.json` and `psx_freeze_heartbeat.json` are
cwd-relative and can otherwise be stale evidence from another build.

## Evidence from the active cleanroom run

- Exactly one WipEout runtime candidate was observed at each read-only sample.
  PID 13736 exited naturally between samples; an external relaunch is now PID
  15392, using the same
  `build-remaster-pgxp-off-20260824/Wipeout_3_Special_Edition__Recompiled_pgxp.exe`.
- Its command line explicitly selected the cleanroom BIOS and
  `cleanroom/saves`; the build-directory `game.toml` contains an older
  `analysis-saves-race` path, but `--memcard-dir` overrides it. No state was
  read from the canonical repository root.
- The relaunch still has `fatal: null`, but its reused heartbeat is older than
  the new process start. The guard therefore rejects it as stale rather than
  treating the previous run's frame count as proof for PID 15392. The prior
  run produced watchdog dumps labelled `spin_freeze`; this is a freeze/wedge
  finding, not a crash report. No process was started or terminated during
  this audit.

## Why the rebuilt PGXP-off binary rejected the old slot

The old PGXP-gate slot carries mod-plan fingerprint
`30e212c909a615e1eb70ad423b55b586f250c44dd336500399d3099ca96d5f95`, while
the rebuilt PGXP-off session reports live fingerprint
`850f6811...`. The runtime therefore refuses the slot during preflight before
applying machine state. This is an intentional resolved-mod-set mismatch,
not evidence that the PGXP ABI flavor transition itself is unsafe: a matching
PGXP-disabled transition is accepted in `artifacts/migration-check.stderr.log`
and invalidates PGXP provenance before rebuilding derived state.

The active cleanroom `slot01` is a separate case: its log says the compatible
codegen/ABI migration reached `boot_state: reject — section preflight failed`.
That slot is not rejected by the old mod-plan fingerprint check; its section
layout/content must be regenerated with the current build before using it as a
continuation fixture.

Run the guard tests with:

```text
python -m pytest tools/test_verify_runtime_repro.py
```
