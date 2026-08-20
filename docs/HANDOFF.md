# Handoff

## Current position

- The original dirty checkout is preserved at
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se` and has not been cleaned,
  reset, rebased, or staged.
- Clean integration work is isolated at
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-cleanroom` on
  `codex/remaster-cleanroom`, starting from scaffold commit `d980fa1`.
- The clean framework branch is `codex/wipeout3-region-recomp` at upstream
  commit `d56bd0247c472f7987e61162fcd7b812b65c4e7f`.
- `docs/CLEANROOM_POLICY.md` defines the quarantine boundary and evidence gates.
- No prior remaster code, assets, fixes, tests, or benchmark claims have been
  accepted. No game build or launch has occurred in the cleanroom yet.

## Next concrete step

Build the first evidence tranche in parallel: media/provenance validation, a
fixed-race benchmark contract and automation surface, clean-framework codegen
shape analysis, overlay/dynamic-code inventory, timing/audio oracle, wide/PGXP
oracle, and asset/font source inventory. Review the resulting tranche before
integrating any feature implementation.

## Open unknowns

- The exact local paths and independently recomputed identities of the user's
  Redump media, BIOS, hueponik patch output, and race save/input route have not
  yet been recorded in the cleanroom.
- Clean upstream boot/race behavior, native-code coverage, host performance,
  100 Hz timing, audio behavior, and rendering output are all UNKNOWN.
- The minimum region compiler design needed for WipEout 3's overlays and any
  self-modifying code is UNKNOWN until the clean workload is captured.
- Every title-specific defect and every prior font/texture variant remains an
  unconfirmed hypothesis or source candidate.

## Ruled out

- Do not merge, cherry-pick, or copy the tainted branches as a shortcut.
- Do not count a synthetic microbenchmark, menu run, or unit test as race
  performance evidence.
- Do not hide interpreted/fallback execution or trade exact timing/audio for a
  headline frame-rate number.
- Do not commit disc images, BIOS data, patched images, save data, captures, or
  generated executables.
