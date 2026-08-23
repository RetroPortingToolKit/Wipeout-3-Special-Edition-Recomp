# Bounded release-build stall audit — 2026-08-22

## Result

The bounded `build-release --target psx-runtime -j4` attempt was terminated
cleanly after the recorded 115/118 tasks. It did not produce a fresh release
executable: the existing EXE timestamp and hash were unchanged, so the stale
artifact gate remains a failure.

The strongest current location hypothesis is the final compiler/cache/toolchain
handoff. ccache failure is unproven, and the evidence does not support calling
static-overlay generation the active cause: the generated objects largely
completed and no source compilation error was emitted.

## Evidence

- `build-release/.ninja_log` records generated objects 43–63,
  `SCES_028.45_dispatch.c.obj`, the PAL100 plugin object, the resource object,
  and `SCPH1001_full.c.obj`; no final link completion is recorded.
- `build-release/CMakeCache.txt` uses bundled Ninja/Clang and bundled ccache
  launchers, with cached `-flto=full` / `PSX_ENABLE_LTO=ON` settings. LTO is an
  important confounder for the next diagnostic.
- The existing release EXE remained unchanged after termination. No partial
  output is a release artifact.

## Bounded next diagnostic

Run one diagnostic-only `-j1 -d explain` repetition under a hard 120-second
limit. Record C/C++ compiler launchers, release flags, `PSX_ENABLE_LTO`, ccache
statistics, child process command lines, elapsed time, peak memory, and the
first active Ninja edge. If the edge is a compile, compare ccache versus the
same compiler command with the launcher bypassed; if it is link/LTO, retain and
measure the verbose link command separately. Do not package or promote either
probe as a release build.

Current conclusion: ccache/toolchain involvement is plausible, but ccache,
link, and static-overlay failure are each unproven.
