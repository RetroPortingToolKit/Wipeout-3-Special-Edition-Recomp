# Handoff

## Current position

- The original dirty checkout remains preserved at
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se`.
- The canonical cleanroom is
  `C:\Users\Shokunin\Documents\ChatGPT\wipeout3se-cleanroom` on
  `codex/remaster-cleanroom`, committed at `cb89d3a` with the reviewed device,
  runtime, host, media, raster, and integration tranches still uncommitted in
  the shared worktree. Do not discard or overwrite this dirty state.
- `psxrecomp@d56bd024` is now classified as a quarantined, non-shipping route
  runner. Its available history is title-tainted back to the local root and is
  not a clean framework base.
- A generic CPS fallthrough defect was independently reproduced and fixed in
  the legacy runner: a reachable unnamed straight-line successor now publishes
  the architectural next PC instead of surfacing a false `PC=0` exit.
- The deterministic route
  `tests/routes/intro-to-first-race.json` completed all 18 steps / 3,117 guest
  frames and reached a live FEISAR race on Porto Kora. The ignored evidence is:
  - `build-clean-baseline/evidence/intro-to-first-race-receipt.json`
  - `build-clean-baseline/evidence/bridge-route-end.png`
  - `build-clean-baseline/evidence/legacy-race-baseline.json`
- The legacy race baseline proves this is not the product architecture:
  approximately 290 million interpreted instructions, 225 million interpreter
  dispatch fallbacks, zero native overlay dispatches, and a 21.4 ms scene-GPU
  maximum while still paced at 59.94 Hz.
- Sol Ultra selected a dual-lane strangler replacement. New original work lives
  under `clean-aot/`; the legacy lane remains only until each boot/race milestone
  has a deterministic replacement.
- The prepared PS-X EXE is now independently inventoried as one immutable image:
  load range `[0x80010000, 0x8017C000)`, entry `0x80162474`, text SHA-256
  `d483d38423965f7995e3d13fc919c887cf63c3f7f2b9d618859c80771488b0c3`.
  The full local file SHA-256 is
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`.
- The first large PBP compressed-entry hypothesis was falsified. Entry 10 of
  the sampled race container decodes exactly from 109,523 to 207,684 bytes,
  but starts with named tunnel/wall scene records and geometry-like data. It is
  an asset payload, not evidence of an executable overlay.
- A hash-pinned Ghidra 12.1.3 headless pass over the exact text image produced
  1,208 untrusted candidate function roots, 139,387 candidate instructions and
  259 coalesced body ranges covering 557,548 bytes (37.4% of the image). These
  are discovery hints only. Probing the current compiler with one image-wide
  region correctly exposed why bounded code/data regions are mandatory: linear
  fallthrough eventually decodes asset/data words as control transfers.
- All 88 on-disc PBP race containers were extracted into ignored evidence and
  verified without retaining decoded bytes in Git: 5,172 entries total, 3,086
  exact bounded-LZSS payloads and 2,086 exact stored payloads.
- The exact deterministic race was replayed once with the quarantined runner's
  capture path explicitly armed. The ignored v2 manifest SHA-256 is
  `ae880d9e52b6aaddf97295d2f3db735d25190264d05adda047fbc8ab5f6a2a32`.
  Independent normalization found six non-overlapping RAM regions, 118,808
  captured bytes, 12,199 exact observed PCs, and 10,983 candidate entry roots.
  The receipt remains discovery-only and explicitly does not prove semantic or
  executable coverage. Its ignored normalized receipt SHA-256 is
  `53c475bccb58e888a7d0b2ba521d64eb345959d08c6a8958a741d78a1d2334c6`.
- The dynamic race bytes are no longer an undifferentiated overlay hypothesis.
  Exact bounded expansion of all 88 PBP containers plus `NFE.PB` proves that
  four high-RAM capture regions are loaded from `NFE.PB` entry 5. Three regions
  are byte-identical source slices. The fourth compares 52,940 authenticated
  source bytes and differs at only four aligned words; all four immediate values
  are exactly doubled, and two of those words were observed executing on this
  route. This establishes a finite runtime variant but does not yet assign a
  timing semantic to the edits. The complete logical-source match receipt
  SHA-256 is
  `ad65b51fa8921cc336b2cc0260a8dbbd2361edf0bf482c71f5ac6c77aca19b76`.
- The first low-RAM capture is now pinned to the user's exact retail SCPH1001
  BIOS slice at file offset `0x13B00`. Across 12,292 captured bytes, 306 bytes in
  87 aligned words differ at runtime; 10 changed words were observed executing.
  The ignored exact delta receipt SHA-256 is
  `18122448535f526b1b12b28d34a8a29b27c7c640bf2e31130035f072151929b8`.
  The second low-RAM region is zero except for a tail copied by the game. Its
  complete 116-byte live slice at `[0x8000DF7C,0x8000DFF0)` is byte-identical
  to authenticated patched-EXE text at
  `[0x801673D4,0x80167448)`; all 26 observed PCs lie inside that exact copy.
  The bytes independently match the documented PlayStation card-specific-delay
  and early-card-IRQ BIOS patch shapes, but the catalog needs only the measured
  source identity and relocation. The ignored exact-copy receipt SHA-256 is
  `da73bfd703c16824ada60e42e3b0e6ec2be9f1019a3eca387e0899020cd05263`.
- A fresh unseeded Ghidra 12.1.3 pass over exact `NFE.PB` entry 5, loaded at the
  source-to-RAM mapping base `0x800BF800`, reports 109 candidate functions and
  33,604 candidate instructions. Normalization coalesces 134,416 of the
  136,908 payload bytes into three candidate body ranges, with the main range
  `[0x800BF9B0,0x800E041C)`. The ignored normalized hint receipt SHA-256 is
  `53b9ac2906e7e961594e0c0d348840747c8b25faf5ada3e385bed7835c1468d9`.
  This prioritizes a bounded whole-payload AOT pass but remains discovery-only;
  it is not code/data truth or coverage evidence.
- The user's untouched multi-track Redump set has now been located and fully
  verified: Track 01 CRC32 `a0e9e5c2`, MD5
  `b9d48f139544051893f45eed62780e42`, and SHA-1
  `a7bb5cdafdf97ba1cd99cc832439f57ce0c39a8a`; all 13 audio tracks also match
  the manifest. Applying pinned `pal_track01.xdelta3` with hash-verified Xdelta
  3.2.0 produces the exact hueponik PAL target SHA-256
  `397b8a30f401bbd9d12904d6054946474ce4746bdf19565a8db534d65ba179e1`.
  Official hash-pinned `b3sum` independently verifies Redump Track 01 BLAKE3
  `8f7d08844df14b1afe8150fb0e434bf9c4f088834da65c8623025722d56586a5`,
  patch BLAKE3
  `c663c95edf41e844ef2c3e2d27c6409870aa7a2a245491b6549b325c744d7f62`,
  and patched Track 01 BLAKE3
  `f192d03f9fb94c91cf719ca796ffe1145471d9a8cec4c125c7f612ee0af306be`.
  The ignored prepared-disc receipt SHA-256 is
  `9e31857ad2d73f5740cacede8527373c57342f430be9f5ee75db981f5cb22200`.
  Independent source/target EXE-text comparison reports 84,728 changed bytes,
  21,912 changed aligned words, and 685 exact changed-word ranges. The largest
  contiguous range is `[0x8002271C, 0x80034848)` (18,507 words). This is byte
  provenance only; no code/data or semantic claim is inherited from the patch.
  The ignored delta receipt SHA-256 is
  `093649db4ba22cb93e2c5e5151b37c591d816d190cf43ea939c2a4be29b976c3`.
- The independently implemented memory/bus tranche passed a Sol Ultra audit and
  was reproduced byte-for-byte in the canonical tree. It now owns exact RAM
  mirrors, scratchpad, immutable caller BIOS, fail-closed MMIO routing, DMA
  origin rules, and catalog-bound code-page stamps that cannot be replayed
  across buses, resets, or A-to-B-to-A writes. Fresh canonical GCC Release and
  Clang ASan/UBSan builds both pass.
- The clean GTE oracle passed final Sol Ultra review at `67ffd6c` after fixes
  for signed MAC wrap visibility, RGB FIFO depth, command latch timing, and
  defensive register/timing boundaries. GCC/Clang Debug/Release, Clang
  ASan/UBSan, deterministic vectors, clang-tidy, and isolation all pass.
- The renderer-neutral GPU frontend passed Sol Ultra review at isolated commit
  `1cfabc4` and is reproduced byte-for-byte in the canonical tree. It now owns
  corrected retail texture-disable semantics, packet aliases, transfer packing,
  overlap ordering, status/read latches, immutable packet snapshots, and
  bounded constant-time queues. Canonical GCC Release and Clang ASan/UBSan pass.
- The bounded CD-ROM/CDDA controller slice passed Sol Ultra review at isolated
  commit `7062a5c` and is reproduced byte-for-byte in the canonical tree.
  Reason-coded IRQ packets, request-gated transfer FIFOs, sector sizing, track
  boundaries, CDDA mixing/autopause, and XA handoff are covered; BIN/CUE parsing,
  XA decode/resampling, subchannel detail, and physical drive timing remain
  explicit integration work. Canonical GCC Release and Clang ASan/UBSan pass.
- The MDEC tranche passed final Sol Ultra review at isolated commit
  `fa03a1453ba9876309fbe11ce69c2ef89e53e97f` and is reproduced
  byte-for-byte in the canonical tree. The review corrected opcode 4--7
  behavior, RLE termination and zig-zag semantics, IDCT indexing/rounding, DMA
  backpressure and parameter accounting, whole-word FIFO requests, cycle
  accounting, and UB-safe snapshots. Fresh canonical GCC Release and Clang
  ASan/UBSan tests pass. Remaining native-console precision and arbitration
  questions are explicitly documented in `clean-aot/mdec/README.md`.
- The software raster tranche passed final Sol Ultra review as a deterministic
  reference oracle. The review corrected integer-coordinate/top-left triangle
  coverage, 12-bit fractional color/UV interpolation, PS1 fixed-point line
  DDA and inclusive polyline joins, texture/STP/modulation/dither semantics,
  wrapped overlap transfers, clipping, malformed-state budgets and noexcept
  dispatch. It matches pinned hardware-capture digests for the complete
  triangle and UV/color scenes and all 147 overlap-copy interiors. This is not
  the production 100-Hz renderer: draw-to-display gating still needs the GP1
  display rectangle, and cache behavior, CRTC/interlace/24-bit scanout and the
  native backend remain explicit integration work.
- The pinned HD texture pack now has a strict metadata-only manifest: all 292
  present PNGs pass signature/chunk-CRC/IHDR validation and exact SHA-256
  inventory (2,890,854 bytes total). `Hashes.ini` declares 390 keys, so 98
  mappings are explicitly missing rather than silently treated as assets. The
  external two-hash filename semantics remain a behavior clue, not yet an
  accepted clean runtime lookup algorithm.
- The 100-Hz clock model has been re-audited against pinned hueponik,
  GooseStation, and DuckStation sources. Hueponik's PAL 100-FPS mode explicitly
  requires PAL x2 plus a heavy CPU overclock. GooseStation's PAL x2 doubles
  only the CRTC/video clock and states that at least 200% CPU is required;
  SPU and CD-ROM rates remain native. DuckStation implements the independent
  CPU multiplier by expanding system/CPU ticks while inversely converting
  device work, preserving 44.1-kHz SPU and media time. Therefore the generic
  machine needs an exact independent CPU-retirement multiplier. The previously
  asserted stock-only 338,688-cycle 100-Hz budget is retained only as the
  100% baseline, not as the product setting. The exact max-detail product
  multiplier remains a measured acceptance value; the tainted legacy lane's
  historical 900% setting is a hypothesis, not an inherited conclusion.
  Integration must also distinguish CPU-retirement time from native device
  time: feeding multiplied CPU totals into the CD-ROM slice's current fixed
  33.8688-MHz periods would incorrectly speed media up. Either scale every
  device deadline into the multiplied CPU timeline with exact carry, as the
  behavior reference does, or keep a separate native master-time coordinate
  and perform exact conversions at MMIO/event boundaries. Mixing the two is a
  release-blocking timing error.
- The generic machine clock has now passed Sol Ultra review after corrections
  for full-width rational arithmetic and frame-start retirement visibility.
  It accepts reduced positive multipliers through exact 10x, including
  fractional ratios, without overflowing an intermediate product. At exact
  100 Hz it emits 441 SPU samples per update and CDDA cadence `0,1,1,1` while
  CPU retirement advances independently. GCC and Clang Release plus
  non-recovering Clang ASan/UBSan pass; an independent arbitrary-precision
  oracle checked 189,989 boundary/random cases with zero mismatches.
- The clean SoC tranche is accepted under the parent Sol Ultra audit. The
  canonical code matches the reviewed implementation apart from safer CTest
  registration and passes fresh GCC `-O3 -Werror`, Clang ASan/UBSan, focused
  interrupt/DMA/root-counter tests, and its source-isolation scan. Its counter
  API explicitly advances in native device time, never multiplied CPU
  retirement time.
- A pinned GooseStation libretro behavior oracle now builds successfully under
  WSL and boots the exact prepared user disc through RetroArch 1.22.2. A
  10,000-frame PALx2 run at the historical 900% hypothesis reached a live demo
  race, reported 99.49 Hz and 44.1 kHz, and advanced exactly 100 seconds of core
  time. A recorded 4,000-frame race window contained 3,937 distinct rendered
  frame hashes and 55 consecutive duplicate images. Those image duplicates do
  not identify simulation duplicates, so 900% remains an oracle hypothesis,
  not an accepted product multiplier; a memory/state-level unique-update probe
  and deterministic controlled race are still required.
- The behavior oracle now has an independently derived seven-confirm replay
  from boot through FEISAR/Vector/Porto Kora and into a live controlled race.
  The 11,499-frame replay produces 11,500 lossless video packets; each sampled
  one-second live-race window at 93, 100, and 110 seconds contains 100/100
  distinct frame hashes. With a working dummy audio sink, the same run emits
  5,096,833 decoded stereo PCM frames at 44.1 kHz. That measured cadence is
  consistent with GooseStation's reported 99.49-Hz PALx2 behavior and is
  deliberately not substituted for the product's exact 100-Hz/441-sample
  contract. The metadata-only receipt is
  `docs/evidence/goosestation-palx2-route-v1.json` (SHA-256
  `d961b73926a26db24e870875931fa812625aea8fd272d7485bbb0084ec4d7a69`);
  the replay, capture, BIOS, and disc bytes remain ignored/untracked.

## Next concrete step

Reconcile and commit the shared cleanroom integration as one reviewed product
state. First close or explicitly defer the known SIO, host, CD-ROM, SoC and
whole-machine acceptance findings; then rerun the aggregate GCC/Clang sanitizer
matrix and release-isolation/link-map gates. After that, complete the authorized
CFG/region manifest for the exact main executable plus the authenticated
`NFE.PB` entry-5 runtime variant, add the bounded raw-image compiler adapter,
and link one clean runtime/product executable with no legacy fallback. The
milestone ends when that executable advances its first authenticated guest
instruction and fails closed on every unknown PC or executable identity.

## Product decisions confirmed by the owner

- The deliverable is a normal modern-PC game: after initial lawful media setup,
  the player opens it directly and reaches the game without interacting with an
  emulator, compiler, BIOS menu, developer tool, or compatibility fallback.
- BIOS-to-game boot is part of the acceptance path. Directly jumping into a
  prepared executable is not an acceptable substitute for the finished product.
- The target experience is a 4K, exact-100-Hz, ultrawide-capable remaster of
  WipEout 3 Special Edition, with the full set of defensible GooseStation-class
  graphics improvements. A reduced first-class feature set or half-measure
  compatibility release is not an accepted endpoint.
- Hardware-precision gaps may be staged during development but may not be hidden
  behind permanent compatibility policy for release. Every known gap must be
  implemented, measured, or explicitly proven irrelevant to the complete game.
- The HD texture and font creator has granted permission. Record the creator,
  scope, date, source material, permitted uses, required attribution, and asset
  hashes in a durable provenance receipt before shipping or redistributing the
  assets; this handoff records the owner's confirmation but is not itself the
  permission artifact.

## Open blockers and unknowns

- No clean-AOT code has yet booted a BIOS or game instruction.
- ISO inventory found one PS-X EXE signature. The deterministic race now proves
  one finite high-RAM load from `NFE.PB` entry 5, one bounded low-RAM BIOS
  variant, and one exact main-EXE relocation into the BIOS patch window. These
  observations still require finite indirect targets, CFG closure, exact
  loader/patch reproduction, and replay across broader routes before release
  catalog acceptance.
- Hardware tranches now exist for GPU, SPU, CD-ROM, DMA/timers/interrupts, SIO,
  MDEC, memory, machine timing, media, raster and host presentation. Their
  focused tests are not whole-machine or hardware acceptance. Earlier audits
  rejected intermediate SIO, host, CD-ROM and SoC snapshots; later fixes and
  reviews must be mapped finding-by-finding before any release claim.
- No clean product executable links these tranches, generated regions and host
  presentation together. No clean-AOT code has booted the BIOS or game.
- The compiler has bounded region lowering and deterministic emission, but no
  complete reviewed game CFG, finite indirect-target closure, raw bounded-image
  adapter for `NFE.PB` entry 5, or 100% executable-epoch catalog.
- 100 Hz machine cadence exists and the GooseStation oracle supplies behavior
  evidence, but the product CPU multiplier, unique simulation-update proof,
  full-race timing, audio/media exactness and performance soak remain open.
- Wide projection/culling, PGXP provenance through every vertex, texture/font
  replacement, and independently justified game fixes are not integrated into
  the clean shipping lane.

## Recovery verification (2026-08-21)

- Canonical product checkout `master` is preserved at `97bd3d7`, 95 commits
  behind `origin/master` (`768cdbf`), with locally advanced `psxrecomp` and
  `recomp-ui` submodules. It is the legacy/product lane, not the clean shipping
  implementation.
- `codex/remaster-cleanroom` is the clean integration branch. Nine isolated
  tranche branches remain divergent and should be treated as review receipts,
  not independently shippable products.
- Fresh aggregate GCC 16.1 Release configure/build succeeded from
  `clean-aot/CMakeLists.txt`; all 15 CTest targets passed.
- Fresh Python verification passed 118 tests and 584 subtests; one test was
  skipped by its declared environment gate.
- Those results establish buildability and regression-test health only. They do
  not supersede adversarial audit findings or establish hardware equivalence,
  static completeness, release isolation, game boot, race completion, 100-Hz
  correctness, or packaging readiness.
- The old project-recovery task failed before producing a report. This handoff
  is now the durable repository record of the reconstructed state.

## Ruled out

- Do not clean up or ship `d56`; its architecture and source history are tainted.
- Do not copy/cherry-pick Claude-era fixes, assets, tests, or conclusions.
- Do not count legacy interpretation, runtime compilation, menu runs, synthetic
  benchmarks, or duplicated presentation frames as static recompilation or
  100-FPS evidence.
- Do not conflate PAL x2 with whole-machine speedup or adaptive host pacing.
  PAL x2 is a 100-Hz CRTC cadence. An independent, fixed, exact CPU-retirement
  multiplier is required by the patch and must leave SPU at 441 samples per
  100-Hz VBlank and CDDA at three sectors per four frames. Do not accept the
  multiplier until a deterministic max-detail race proves 100 unique updates,
  1.00x wall/audio/media time, and no missed simulation deadlines.
- Do not commit disc images, retail BIOS data, patched executables, captures,
  generated C, screenshots, saves, or packaged binaries.
