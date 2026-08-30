# Handoff

Updated: 2026-08-30

Scope: completed deep code, bloat, performance, test, and release-path review on
the canonical root `master` worktree and its pinned `psxrecomp` framework.

Authoritative product plan: [Reliable widescreen replacement plan](WIDESCREEN_RELEASE_PLAN.md)

Historical record: [Handoff through 2026-08-28](archive/HANDOFF-through-2026-08-28.md)

## Current verified state

- The Release product is current at framework revision `4ccc6be1`, version
  `0.1.0`, `-O3 -DNDEBUG`, `PSX_NO_DEBUG_TOOLS=1`, and the title-owned 8 MiB RAM
  contract. The final executable links successfully.
- Release PE `.bss` fell from 369,817,120 to 96,884,832 bytes, saving
  272,932,288 bytes (260.29 MiB, 73.80%). Lean builds omit diagnostic rings and
  producer work while debug builds retain them.
- GPU DMA again folds 24-bit addresses through installed 2/8 MiB RAM before
  applying high-page uniqueness. Focused alias/aperture coverage passes.
- The Windows malformed-shard negative test uses a thread-scoped loader error
  mode. It still rejects the invalid DLL without raising a `python.exe - Bad
  Image` dialog.
- The title suite passes 79 tests plus 13 subtests. The recompiler graph passes
  all 55 enabled tests (57 total, two disabled). The complete runtime build
  succeeds and CTest passes all 68 enabled tests (71 total, three disabled).
- The setup packager stages only clean tracked title/framework sources, verifies
  the complete rebuild closure and framework pins, strips only the staged PE,
  and leaves the canonical executable unchanged.
- `dist/wse-0.1.0-codex-deep-audit-windows-x64-20260830.zip` passed the final
  freshness/source-closure verifier with 1,953 canonical members. Its SHA-256 is
  `984f12a4472bd9292bfbfc2ebfc9d54d8098830e1f305355b0c7d904d2423d06`.
- The current compatible gameplay receipt remains CPU 200 / PGXP off: about
  99.8 FPS / 1.00x for roughly 77 seconds, with PAL x2 presentation near the
  expected 50 FPS. This is prior acceptance evidence, not a new long soak.

## Next concrete step

Deliver the nested framework history to a reachable integration ref first, then
reconcile the title's divergent `master` with `origin/master` in a clean lane.
After that integration, repeat the final source-closure check and the remaining
runtime acceptance gates before any public release.

## Open blockers

- Root `master` is hundreds of commits ahead and 110 behind `origin/master`;
  neither a normal push nor a force push is safe.
- The pinned nested commit is not contained in a remote-tracking branch. Pushing
  the root alone would publish a submodule revision other users cannot fetch.
- Track 01 disc identity still needs resolution without making
  `--skip-hash-check` permanent.
- A longer controlled moving-race soak and deterministic or listened CDDA proof
  remain release acceptance work.
- `sio_dualshock_rumble_test`, `spu_fidelity_test`, and
  `sio_card_protocol_test` remain explicitly disabled. Their executables build;
  the SIO tests retain documented protocol failures rather than being mislabeled
  as passing.
- Vulkan was requested but unavailable, so the local Release uses the inert
  fallback. LTO is disabled by this toolchain, and optional Beetle comparison is
  unavailable without `libmednafen_psx.a`.

## Protected decisions

- Keep CPU 200 and the PGXP-off reliable-widescreen baseline unless new
  controlled evidence supersedes them.
- Treat PR #7 as forensic reference only; do not merge, rebase, or build from
  it.
- Do not push divergent branches, force-update refs, or discard unrelated
  root/nested worktree changes.
- Do not restore unconditional diagnostic rings, broad plug-in source globs,
  inert compile-definition-only RAM behavior, or dead high-RAM generators.
- Do not reduce live overlay-table capacities until representative high-water
  measurements justify the new bounds.
- Do not infer permission to bypass disc identity or publish a release.

## Evidence map

- [Project deep review (2026-08-30)](audits/project-deep-review-2026-08-30.md)
- [Historical implementation and profiling chronology](archive/HANDOFF-through-2026-08-28.md)
- [Reliable widescreen replacement gates](WIDESCREEN_RELEASE_PLAN.md)
- `artifacts/widescreen-reliable-20260827/`
- `tools/verify_release_freshness.py`
- `tools/test_setup_release_source_closure.py`
- `framework_pins.txt`

## Ruled out

- Rebuilding after every small refactor: focused static/unit/configuration tests
  were used until an integrated build could prove the final state once.
- Deleting the 102.54 GiB ignored freeze corpus during review: ownership and
  retention authorization must be established before recoverable cleanup.
- Pruning the 11.22 MiB vendored test/doc corpus without an upstream/vendor
  policy: it is repository weight, not shipped runtime weight.
- Treating old 900%/100% CPU, PGXP-on, stale package, or stale build receipts as
  current authority; they remain in the historical archive only.
