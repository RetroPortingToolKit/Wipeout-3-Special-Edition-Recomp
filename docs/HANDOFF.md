# Handoff

Updated: 2026-08-30

Scope: deep code, bloat, performance, and release-path review on the canonical
root `master` worktree.

Authoritative product plan: [Reliable widescreen replacement plan](WIDESCREEN_RELEASE_PLAN.md)

Historical record: [Handoff through 2026-08-28](archive/HANDOFF-through-2026-08-28.md)

## Current verified state

- The current title profile is PAL x2 at `cpu_overclock = 200`, with PGXP
  geometry, perspective, depth, and CPU propagation disabled. The compatible
  slot-10 race receipt held about 99.8 FPS / 1.00x for roughly 77 seconds;
  rendering remained near 50 FPS as expected for PAL x2 presentation.
- Reliable presentation has live acceptance at 4:3, 16:9, and exact 21:9 on
  two tracks with PGXP disabled. Durable visual and pacing receipts remain in
  `artifacts/widescreen-reliable-20260827/`.
- Root commits `9a76c36`, `e9a990c`, and `be22d45` replaced drifting release
  checks with one source-driven validator and fixed nested-profile handling in
  real setup archives. The lightweight root suite was green after each change.
- Root commit `cb8452b` makes setup archives carry the title-owned rebuild
  closure and guards that closure without configuring or compiling the game.
- Root commit `6fc426e` makes title hardware and plug-in contracts explicit:
  the 8 MiB default is now consumed by runtime code, retired plug-ins no longer
  enter through a source glob, Skip FMV is title-owned, and both normal and
  optional PGXP runtime targets receive the RAM contract.
- Nested runtime commits `23349551` and `80d34a37` validate title RAM defaults
  and centralize plan activation across initial launch and lobby rematch. The
  focused CTest profiles and lifecycle guard pass.
- A configure-only setup-host probe passed with `PSX_SETUP_WIZARD=ON`, LTO off
  for the local MinGW toolchain, no dead high-RAM generation target, and the
  8 MiB compile contract present in the Ninja graph.
- The working release executable still predates the current source profile.
  No final game build has been claimed during this review.

## Next concrete step

Integrate the bounded GP0 diagnostics, release-CI, and audit-index increments;
then run one consolidated focused verification and only the build/proof needed
to certify the resulting product state. Execute Gate 1 of the widescreen
replacement plan only after the plan's explicit approval condition is met.

## Open blockers

- Resolve the current Track 01 identity mismatch without making
  `--skip-hash-check` permanent.
- Complete a longer controlled moving-race soak and deterministic or listened
  CDDA proof before release acceptance.
- The standalone nested runtime test tree has a pre-existing launcher-header
  mismatch: `main.cpp` references `RECOMP_LAUNCHER_FMV_FILTER_COUNT` when the
  standalone target does not include recomp-ui. Product-root configure is not
  affected, but the nested full test build is not currently clean.
- Local `master` is intentionally divergent from `origin/master`; it is not a
  safe push target. Delivery still requires a clean integration lane.

## Protected decisions

- Keep CPU 200 and the PGXP-off reliable-widescreen baseline unless new
  controlled evidence supersedes them.
- Treat PR #7 as forensic reference only; do not merge, rebase, or build from
  it.
- Do not push the divergent local `master`, force-update refs, or discard
  unrelated root/nested worktree changes.
- Do not restore the inert compile-definition-only RAM migration, broad plug-in
  glob, or dead high-RAM generator target.
- Do not infer permission to bypass disc identity or publish a release.

## Evidence map

- [Historical implementation and profiling chronology](archive/HANDOFF-through-2026-08-28.md)
- [Reliable widescreen replacement gates](WIDESCREEN_RELEASE_PLAN.md)
- `artifacts/widescreen-reliable-20260827/`
- `tools/verify_release_freshness.py`
- `tools/test_setup_release_source_closure.py`
- `tools/test_mod_plugin_contract.py`

## Ruled out

- Rebuilding after every small refactor: use focused static/unit/configure
  checks until a build is needed to prove behavior.
- Deleting the roughly 110 GiB ignored diagnostic/build corpus during review:
  ownership and retention policy must be established before cleanup.
- Treating old 900%/100% CPU, PGXP-on, stale package, or stale build receipts as
  current authority; they remain preserved in the historical archive only.
