# Reliable widescreen replacement plan

Status: proposed for execution after user approval

Build mode: autonomous, with evidence recorded at every gate

Primary scope: reliable 4:3/16:9/21:9 presentation with PGXP disabled

Delivery: a new replacement PR; existing PR #7 is reference material only

## Goal

Deliver a clean, reproducible, PGXP-free widescreen change on the current
upstream base that preserves the project's newer renderer, mod, PAL100,
savestate, overlay, and texture-pack work. The result must pass title-specific
geometry checks, native-aspect fallback, Release pacing and CDDA checks, and a
controlled moving soak before it is proposed as the replacement for PR #7.

## Non-goals

- Do not merge, rebase, or retarget PR #7.
- Do not transplant the old PR's runtime snapshot or global precise-MAC0
  behavior.
- Do not enable geometry correction, perspective texturing, PGXP depth, or
  PGXP CPU mode.
- Do not make `--skip-hash-check` part of the build or release procedure.
- Do not push the current divergent local `master`.

## Execution checklist

- [ ] **1. Establish the clean integration baseline**
  Source: current `origin/master`, its pinned `psxrecomp` gitlink, and
  `docs/HANDOFF.md`.
  Work: Fetch current root refs and the exact submodule object pinned by
  `origin/master`; create a clean `codex/` replacement branch without changing
  or importing the dirty local `master`. Inventory root and nested changes at
  both ends before editing.
  Acceptance: The clean lane has no unrelated changes, builds the unmodified
  upstream baseline, and records exact root/runtime SHAs. The current dirty
  worktree and all unrelated worktrees remain untouched.
  Verify: `git status --short --branch`, `git worktree list --porcelain`, root
  `git ls-tree HEAD psxrecomp`, nested `git status`, and one baseline configure
  and build receipt.

- [ ] **2. Port only the runtime NCLIP consumer capability**
  Source: semantic delta from nested commit `fed7568f`, not its ancestry.
  Work: Reimplement `nclip_exact_sites` on the clean pinned runtime: config
  parsing, code generation, CPU state transport, exact-sign computation, and
  configured branch consumption. Keep native guest-visible MAC0 unchanged.
  Port or rewrite tests against the clean runtime APIs. Do not pull the older
  runtime branch, freeze-watchdog commit, PGXP defaults, or unrelated generated
  analysis work.
  Acceptance: The clean runtime diff is limited to the capability and tests;
  configured branch sites observe exact NCLIP sign while all other readers see
  native MAC0.
  Verify: recompiler config/codegen tests, `recompiler_patch_test`,
  `gte_register_access_test`, diff audit against `fed7568f`, and full nested
  `git diff --check`.

- [ ] **3. Port the minimal title configuration**
  Source: the tested semantics in current `game.toml`, with PR #7 used only to
  identify historical intent.
  Work: Apply only the four type-4 billboard FLAG patches, nine exact-NCLIP
  sites, two plane-NX sites, 34 bounded horizontal cull widens, 64-pixel guard,
  sane startup width, and PGXP-off settings to the clean root configuration.
  Preserve newer upstream mod, renderer, PAL100, overlay, texture-pack, and
  release settings. Do not cherry-pick root commit `12642f9` wholesale.
  Acceptance: A semantic diff shows the selected widescreen settings and no
  unrelated configuration rollback; no unconditional near-Z or submitter keep
  patches are present.
  Verify: deterministic config parser test, patch expected-word validation,
  counted-site audit (4/9/2/34), and a focused diff against clean upstream.

- [ ] **4. Resolve the Track 01 identity gate**
  Source: configured hashes and the actual legal Track 01 image.
  Work: Recalculate size, MD5, SHA-1, and CRC32 from the exact data track;
  establish whether the current `462dcf...` image is a valid alternate dump,
  a transformed image, or the wrong input. Correct extraction/configuration
  only when the identity is proven. Keep the known-good configured identity
  intact unless an alternate is independently authenticated.
  Acceptance: Generation succeeds without `--skip-hash-check`, or work stops
  with a precise media-identity blocker rather than weakening the gate.
  Verify: saved hash receipt, CUE/track mapping audit, and a normal generation
  command with hash checking enabled.

- [ ] **5. Regenerate and prove the clean build**
  Source: clean root config plus the minimal clean runtime port.
  Work: Regenerate the main executable corpus and required overlays, configure
  Debug and Release from clean build directories, and run the focused runtime
  and recompiler suites. Confirm generated/runtime ABI and codegen hashes match.
  Acceptance: Debug and Release build from scratch with no stale generated
  shards, no verification loop, no hash bypass, and no state/ABI mismatch.
  Verify: clean configure/build logs; `recompiler_patch_test`,
  `psxrecomp-game`, `gte_register_access_test`, generated-source verification,
  and `git diff --check`.

- [ ] **6. Run the visual acceptance matrix**
  Source: fresh boot and fresh current-format states only.
  Work: Test Talon's Reach and Altima VII at 4:3, 16:9, and exact 21:9. Capture
  menu, countdown, live moving race, billboard/effect-heavy, and HUD frames.
  Use composed-frame capture for geometry and exact-HWND `PrintWindow` capture
  for final presentation so another foreground app cannot contaminate proof.
  Acceptance: 4:3 reports widescreen inactive with native projection; 16:9 and
  21:9 show the expected margins/squash and retain craft, track, scenery,
  billboards, lights, effects, countdown, and HUD. No wedges, missing near
  objects, overbright quads, clipped viewport, or unexplained black bands.
  Verify: named PNG matrix plus runtime status/log receipt for every aspect and
  track. Reject attract-mode and post-race replay shots as geometry evidence.

- [ ] **7. Prove Release pacing and CDDA timing**
  Source: the same clean Release executable used for visual acceptance.
  Work: Add a deterministic CDDA progress/duration receipt if current telemetry
  cannot prove it. Run PAL x2 with a fresh race state and compare simulated
  vblank cadence, CDDA sector/time progression, wall time, and underrun/error
  logs. Keep CPU overclock at 100 percent.
  Acceptance: After warm-up, simulated cadence stays within 99-101 Hz; CDDA
  advances at its media-authored wall-time rate (target drift at most 1 percent
  over a 60-second sample); no audio underrun, double-speed playback, freeze,
  or CD/controller error occurs.
  Verify: machine-readable 60-second Release receipt and error-log scan. Debug
  performance numbers are not accepted as Release evidence.

- [ ] **8. Run controlled moving reliability soaks**
  Source: clean Release, fixed test states, and scripted/held controller input.
  Work: Run at least five controlled moving minutes on each accepted track at
  16:9 and 21:9, keeping the race active rather than letting it enter replay.
  Include window resize/aspect transitions and one 4:3 fallback transition.
  Scope cleanup to the exact test PID.
  Acceptance: No intermittent geometry, viewport jump, black frame, effect
  flare regression, crash, freeze dump, pacing collapse, or new runtime error.
  Verify: bounded soak logs, periodic composed captures, final telemetry
  summary, and exact-PID exit receipt.

- [ ] **9. Prepare the two clean PR layers**
  Source: verified clean runtime and root branches.
  Work: Commit the minimal runtime capability separately and propose it to the
  runtime repository first. The root PR must pin a runtime commit fetchable
  from the submodule's configured `mstan/psxrecomp` origin; do not leave the
  root gitlink pointing only to a fork-private/unadvertised object. Then commit
  the minimal root title config and gitlink on a new replacement branch.
  Acceptance: Both diffs are reviewable and contain no dirty-worktree leakage;
  CI/submodule checkout works from a fresh clone; PR descriptions link the
  verification receipts and explain why PGXP is excluded.
  Verify: fresh-clone submodule update, clean rebuild/test subset, remote branch
  inspection, and PR check status.

- [ ] **10. Deliver and supersede the obsolete PR**
  Source: passing replacement PR(s) and final evidence bundle.
  Work: Update `docs/HANDOFF.md` with final SHAs, commands, artifacts, results,
  remaining non-goals, and rollback points. Push the verified branches and open
  the replacement PR(s). Once the replacement exists, mark PR #7 superseded
  with a link; do not close it earlier and lose the historical comparison.
  Acceptance: A reviewer can reproduce the build and acceptance matrix from a
  fresh clone, and the only open widescreen proposal is the clean replacement.
  Verify: PR URLs, passing checks, fresh-clone receipt, handoff diff, and final
  root/nested status audit.

## Stop conditions

Execution pauses only for a genuine external blocker: an unprovable disc image
identity, lack of authority to make the runtime commit fetchable from its
configured origin, or a visual/audio failure that requires a product choice.
Ordinary compile failures, test failures, stale states, and dirty-lane setup are
part of the goal and must be diagnosed and fixed without handing steps back to
the user.

## Goal completion definition

The goal is complete only when the clean replacement PR(s) are pushed, fresh
clone/build/checks pass, the visual matrix and controlled soaks pass, PAL100 and
CDDA timing pass, the media identity gate is not bypassed, the final handoff is
committed, and PR #7 is explicitly superseded. PGXP remains off and is not a
completion requirement.
