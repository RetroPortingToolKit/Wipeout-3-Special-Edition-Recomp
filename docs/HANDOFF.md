# WipEout 3 SE refactor handoff

## Current verified position

- Canonical root worktree is `master`; preserve the unrelated untracked
  `docs/internal/upstream/WIPEOUT3_SESSION_ENGINE_DRAFTS_2026-08-20.md`.
- Clean Release build passes `tools/verify_static_release.ps1` with the
  external retail BIOS boundary (`SCPH1001.BIN` supplied by the user).
- Direct launch with the PAL SCES-02845 cue reaches `game_started=1` through
  the recompiled BIOS path. The disc is not bundled.
- The currently supplied PAL Track 01 is already the target of the upstream
  PAL 25/50/100 patch: xdelta3 reports `source is already up to date` when
  applying `pal_track01.xdelta3` from upstream `dev` at revision
  `ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`. The supplied executable also
  contains the `100HZ` title data. This authenticates the title input, but is
  not yet proof that the clean runtime drives its 100-Hz menu path.
- Debug RelWithDebInfo run verified the external HD pack is live: 445 indexed
  entries, 299 matched, 300 decoded images, 47,919 replacement hits, and zero
  decode failures.
- Normal CD timing probe recorded 3,840 exact data-sector deliveries, with no
  early/late/lost interrupts or response overwrites in the observed window.
- Disposable PAL-100 package probe now selected `wipeout3.framerate.pal-x2`
  against the authenticated patched media and logged PAL x2 / 100 Hz with a
  10 ms pacer through the real SCPH1001 BIOS path. Debug telemetry recorded
  3,841 exact data-sector deliveries (zero early, late, pended, or lost), 285
  native XA audio sectors, 1,340,640 CD-input frames, 2,219,248 SPU frames at
  44.1 kHz, and zero audio underruns. This is strong runtime timing evidence,
  but does not yet prove exact game simulation-update count or stable racing.

## Open blockers

- PAL 100 Hz is not implemented. Runtime PAL refresh multiplier support exists,
  but the PAL game timing loop is structurally different from the NTSC-120
  patch. Do not guess constants or enable PAL x2 without simulation, SPU/CDDA,
  timeout, and media-load equivalence traces.
- Normal-vs-accelerated memory-card and race-load equivalence is not proven;
  acceleration remains disabled by default.
- Runtime still links generic overlay/autocompile/interpreter plumbing even
  though the release configuration disables its behavior; strict link-surface
  removal remains unfinished.
- Widescreen/ultrawide and complete race/content soak validation remain
  unproven.

## Next concrete work

1. Map PAL frame-gate/time-scale sites from the verified PAL executable and
   generated dispatch, then build a trace-backed PAL-100 candidate.
2. Compare normal and accelerated load traces (CD command/sector/DMA,
   memcard writes, state fingerprints, and audio timing) before enabling it.
3. Remove or prove dead all runtime compiler/interpreter paths in the release
   link graph, then rerun the static boundary gate.
4. Validate ultrawide/content coverage and perform a long deterministic soak.

## Ruled out

- Reusing the existing NTSC-120 package as a PAL-100 patch: PAL sites do not
  have the same instruction layout or timing constants.
- Treating HD-pack discovery as proof of replacement: live substitution was
  separately measured and is now confirmed only for the observed 299 entries.
- Repeating an unbounded PAL-100 search without a preserved PAL executable,
  generated dispatch, and trace receipt: the latest bounded investigation
  produced neither a candidate nor evidence suitable for enabling the mode.
  Keep PAL x2 disabled until those authoritative inputs and the required
  simulation/SPU/CDDA/media comparisons exist.
