# Bounded validation: `0x807814D0`

Date: 2026-08-22

## Result

The requested bounded validation could not be run in this worktree. The
canonical ignored capture `build-release/overlay_captures-8078.json` is not
present, and the checked-out `psxrecomp` gitlink is unpopulated. The associated
generator log, generated static overlay, and last-run report are also absent.

Therefore this run establishes neither:

- an integrated static dispatcher case/range/CRC for `0x807814D0`; nor
- native-versus-interpreter parity evidence.

No full corpus, unbounded generation, recapture, or source/submodule mutation
was attempted.

## Availability checks

The following fixed paths were checked and were all missing:

```text
build-release/overlay_captures-8078.json
build-release/overlay-8078-gen.log
build-release/generated/overlays_static.c
build-release/psx_last_run_report.json
psxrecomp/.git
```

The capture path is intentionally ignored by the repository (`.gitignore`),
so its absence cannot be resolved from tracked files. Once the canonical
capture and populated submodule are available, the safe test remains the one
specified in `docs/audits/native-performance.md`: select one target-bearing
row, compile one isolated CPS-enabled fragment outside `generated/`, and
require exact range/CRC coverage plus a legal static entry before attempting a
target-specific parity probe.
