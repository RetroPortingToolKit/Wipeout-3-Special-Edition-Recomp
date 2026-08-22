# Tainted implementation hypotheses

The prior Claude-era documents are retained only as a list of things worth
testing. Their addresses, causes, fixes, metrics, commits, generated assets,
and conclusions are not accepted evidence and must not be copied into the clean
lane. Every item below starts **UNVERIFIED**.

## Fonts and text atlases

- Candidate observation: four native atlas identities may cover two display
  faces and two small-text/banner faces, with fourteen palette variants.
- Candidate rendering rule: PS1 semi-transparency/STP may make replacement
  alpha categorical rather than ordinary coverage alpha, and transparent RGB
  may affect filtered edges.
- Candidate asset sources: F500 Ang-ular and Arimo. F500 licensing was reported
  as absent; Arimo was reported as OFL. Neither source or permission record is
  accepted until independently reacquired and hashed.
- Candidate defects: resampling ringing may create doubled strokes; black RGB
  in transparent padding may create halos.

Required proof: clean-lane GP0/VRAM upload identities, an independently recorded
UV-cell census, palette/STP classification, legal source provenance, generated
atlas manifests, topology/alpha tests, and in-game captures for every palette.
Legacy texture hashes may be used only to seek the corresponding clean trace;
they do not become product constants by repetition. Legacy pack paths,
exclusion lists, and cached PNGs are not reusable.

## Game-side defect candidates

1. A Manor Top wall face may select an unwritten CLUT row adjacent to a grey
   palette. Prove this from a clean VRAM upload/GP0 trace and an image A/B before
   considering a guarded product-layer correction.
2. A type-4 billboard path may emit saturated GTE projection results without
   checking FLAG. The reported guest addresses and four instruction patches are
   tainted. Reproduce the exact command/register sequence and visible wedge at
   wide aspect before designing a correction; do not silently modify the
   authenticated hueponik executable.
3. Wide projection may expose integer-NCLIP/culling disagreements. Preserve the
   guest's integer visibility decision until a clean differential trace proves
   a title policy. Precise determinants are telemetry, not an automatic
   override.
4. A race-teardown effect path may write through a null pool and later corrupt
   BIOS event state. Require a deterministic clean write trace, call context,
   and minimal reproducer before adding any guest-state guard.
5. HUD meter/readout packets may require product-layer grouping around a common
   anchor. Reconstruct grouping from clean packet captures at 4:3 and every wide
   aspect; do not inherit the prior adjacency heuristic.

## Performance claims

- The quarantined race receipt independently confirms that the old runner was
  overwhelmingly interpreter/fallback driven. It does not validate the old
  explanations about helper spilling, LTO, rewind, texture invalidation, or
  overlay compilation.
- The historical 900-percent CPU setting is an unverified product hypothesis,
  not a value to inherit. Independent pinned-source review does prove that
  hueponik's 100-FPS mode requires PAL x2 plus a heavy, independently clocked
  CPU multiplier. The clean machine must therefore measure and pin the product
  multiplier while preserving 441 SPU samples per 100-Hz update and three CDDA
  sectors per four updates. 338,688 CPU-retirement cycles per update is only
  the 100-percent baseline; multiplied retirement must not speed device time.
- Legacy save states, generated overlays, runtime reports, symbolized profiles,
  and benchmark numbers are discovery material only. Clean acceptance begins
  at a deterministic user-media boot/race route with static variant coverage.
- The legacy label "overlay" is not evidence that disc assets contain code.
  One sampled PBP entry whose expanded size matched a supposed runtime code
  window was independently decoded and identified as tunnel/wall scene data.
  Future executable epochs require exact load/write provenance plus executed-PC
  evidence; size coincidence and dirty-page classification are rejected.

## Promotion rule

An item leaves this file only when its clean reproducer, expected observation,
implementation boundary, tests, and evidence receipt are committed together.
If clean evidence contradicts a prior narrative, the narrative is discarded;
it is not reconciled by adding another compatibility hack.
