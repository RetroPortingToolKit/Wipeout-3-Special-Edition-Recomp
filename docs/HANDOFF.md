# Handoff

## Native runtime settings UX (2026-08-22)

- `Wipeout3SE.exe` now opens a compiler-free Win32 settings window for missing
  first-run configuration or `--settings-ui`. Native tab controls cover
  display/4K/ultrawide, graphics enhancements, audio device/latency, controller
  deadzone/rumble, card slots, reset defaults, save, and cancel.
- Native controls and `--settings-ui-action` automation share one strict schema
  validator and durable atomic writer. Unicode save, byte-identical cancel,
  invalid rejection, and reset recovery pass in the genuine installed package.
- Canonical settings UI acceptance is bound to package manifest `849a197b...`
  and executable `00ad0cfd...`; it remains explicitly not game-ready.

## Memory-card persistence and recovery acceptance (2026-08-22)

- `Wipeout3SE.exe` now loads or recovers both durable 128-KiB card images,
  attaches them to SIO0 ports one and two, and atomically flushes each committed
  sector with two-generation backup rotation. Setup now rejects size-correct
  cards with malformed PS1 header/directory checksums as well as bad sizes.
- Product acceptance binds native proof and required persistence symbols to the
  installed package/link map, preserves
  both exact card hashes through real setup upgrade/rollback functions, checks
  all 64 scripted scenarios remain at 100 Hz, and rejects save-state material.
  This is memory-card persistence only; no emulator save states were added.
- Canonical acceptance is bound to package manifest `018f4712...`, executable
  `2dfcca55...`, and link map `9d96abea...`. The persistence release gate passes.
  Overall readiness remains fail-closed at 9/15 because the new executable also
  invalidates older setup/graphics/soak package bindings, alongside the three
  genuine game/media/behavior blockers.

## Behavioral route verification harness (2026-08-22)

- The product tooling now derives a deterministic 64-route scripted-input plan
  at 100 Hz for all required menu/mode/track/team/ship/FMV/audio routes.
- `behavior-run` invokes only the validated installed `Wipeout3SE.exe` and will
  publish eligible evidence only after authenticated game boot and exact
  package/executable/media/scenario identity checks. Every completed route must
  include remeasured static trace, frame, audio, and save captures.
- A current real invocation at `modes:single-race` exits 2 with no receipt:
  `installed product produced no behavioral proof`. This is the intended dormant
  state at the BIOS frontier. The coverage ledger remains 0/64 and fail closed.

## Split content coverage gate (2026-08-21)

- Product content readiness now has three independently hash-bound contracts:
  verified external-media availability, static executable/overlay generation,
  and clean installed-product behavioral execution. The first two pass for
  5,179 data payloads, 14 disc tracks, two authenticated executable identities,
  and 135,829 generated instructions with zero unresolved CFI.
- Behavioral evidence remains fail closed: all 64 policy routes are currently
  unexecuted (6 modes, 16 tracks, 8 teams, 8 ships, 8 menus, 3 FMV routes, and
  15 audio routes). Quarantined or non-product evidence is ineligible.
- `validate-release` is deterministic at 11 pass / 3 fail. Remaining failures
  are direct BIOS-to-game execution, behavioral-route execution, and
  authenticated media execution. Do not promote game readiness until those
  real installed-product paths execute.

## Same-epoch overlap registry and live shell loop (2026-08-21)

- Copied-RAM registries now retain independent exact range/hash/stamp contracts
  when generated bindings overlap. Dispatch considers only bindings that both
  cover the current PC and contain that exact entry, then selects the narrowest
  range deterministically. Equal-width ties use start address and identity.
- Exact duplicate contracts and conflicting identity/hash reuse reject. Every
  covering contract is mutation-validated before dispatch, invalidated slots
  are reclaimed, and the bounded metadata capacity is 256 exact bindings. No
  page-wide byte union or fallback was introduced. Legacy and packed generated
  tables share the same checked entry-resolution path.
- The real probe advances through 16 additional exact shell/handler frontiers:
  `0x80054C90`, `0x8005AA90`, `0xC80`, `0x8005AA98`, `0x8005A9D0`,
  `0x1D8C`, `0x1D00`, `0x8005A9E0`, `0x1F10`, `0x8005AA10`,
  `0x8005AA18`, `0x800522C0`, `0x800560C0`, `0x80056324`,
  `0x8003FFB0`, and `0x80054428`.
- From the final tranche, execution remains live through the bounded
  one-million-dispatch probe and stops at `0x800545B8` with 9,473,267
  instructions, 1,989,635 reads, 1,489,510 writes, and device totals
  2,331/22,951. Two probes agree exactly. Instrumented classification found no
  CD MMIO, GetID, ReadN, or raw-sector activity. This is a materially different
  live-loop/timing blocker, not an authorization frontier. Canonical evidence:
  `clean-aot/catalog/evidence/bios-shell-cd-execution-09473267.json`.

## SPU shell initialization frontier (2026-08-21)

- Four newly authenticated copied-shell tranches advance the real path from
  `0x8003FDB0` through `0x8003FCC8`, `0x80054060`, and `0x800544E0`.
- At `0x80054384`, the shell legally reads SPU KON (`0x1F801D88`) before
  read/modify/write initialization. Treating the write-only latch read as an
  unmapped MMIO fault incorrectly raised Cause `0x1C`. KON/KOFF now expose the
  hardware-visible zero value on reads; an adversarial test covers both halves.
- The corrected probe reaches UnknownCode `0x80054710` after 2,767,140
  instructions / 462,105 dispatches, 455,346 reads, 459,322 writes, 7,680
  isolated writes, and device totals 60/259. Repeated probes agree. There is
  still no CD MMIO, GetID, ReadN, or raw-sector activity.
- The next exact range overlaps an already bound tranche: the current compiler
  emits an entry for `0x80054710`, but runtime lookup retains the earlier
  bounded region, whose live graph did not include this indirect return entry.
  Reconcile overlapping same-epoch registries without widening authorization;
  do not infer dormant entries. Canonical evidence is
  `clean-aot/catalog/evidence/bios-shell-cd-execution-02767140.json`.

## BIOS shell/CD diagnosis (2026-08-21)

- The syscall exception now returns correctly. Exact live authentication follows
  vector `0x80000080`, handler `0xC80`, callback `0x1A00`, restore path
  `0xF40`, and resumes at EPC+4 `0xBFC0D968` while retaining Cause `0x20` and
  EPC `0xBFC0D964`. A stale packaged compiler had falsely emitted Reserved
  Instruction for SLTIU at `0x1A38`; the current compiler removes that false
  A0/0x40 path.
- Subsequent exact shell callbacks advance deterministically to copied-shell PC
  `0x8003FDB0`: 2,725,528 instructions / 458,801 dispatches, 445,880 reads,
  452,860 writes, 7,680 isolated writes, and unchanged device totals (2 reads,
  80 writes). Two repeated probes agree. No CD MMIO, GetID, ReadN, DMA transfer,
  or raw-sector read has occurred, so this remains a pre-CD shell frontier.
- Canonical execution evidence is
  `clean-aot/catalog/evidence/bios-shell-cd-execution-02725528.json`.

- The verified European Track 01 sector 4 contains the authentic spaced Sony
  Europe license banner. `DiscSource` previously discarded that identity and
  left GetID neutral; it now derives `disc_type=0x20` and `SCEE` strictly from
  those mounted bytes. Unknown media remains neutral and ambiguous banners
  fail closed.
- Zero reads had an additional control-flow cause: `0x80030000` is copied BIOS
  shell code (byte-identical to BIOS offset `0x18000`), not a game boundary.
  Executing only newly authenticated live shell regions reaches
  `0x80030000 -> 0x8005A8D0 -> 0xBFC072B8`, then the exception vector at
  `0x80000080` with Cause `0x20` and EPC `0xBFC0D964`. CD MMIO remains empty;
  no sector-read claim is made. The deterministic diagnosis is
  `clean-aot/catalog/evidence/bios-shell-cd-diagnosis.json`.
- Next: reauthenticate the exception-vector epoch reached by shell syscall,
  return through the exact shell callback, and continue live shell regions to
  the first GetID/ReadN. Do not bind the game registry at `0x80030000`.

## Authenticated game-bind rejection (2026-08-21)

- The real transition probe now mounts the verified BIN/CUE and links the
  latest precompiled main-game shard/CFI registry. The BIOS nevertheless reads
  zero raw sectors and reaches `0x80030000` with the same shell RAM epoch.
- The complete resident main load contract `[0x80010000,0x8017C000)` hashes to
  `eab668f3b877ca88b76acb2ab595009ebcdb88c8ff7d8cfe295c05ff4be086ba`,
  not authenticated main identity `d483d384...`; `0x80030000` is also not a
  generated registry root. Binding therefore rejects fail-closed and executes
  zero game instructions. No shell bytes were authorized.
- Canonical evidence is `bios-to-game-execution-00000000.json`. The next task
  is the CD boot/device path that currently performs no sector read, not game
  shard expansion or relaxed resident identity matching.

## Real BIOS-to-game transition (2026-08-21)

- Authenticating only copied-RAM `[0x1508,0x15D8)` in epoch `778e17e8...`
  advances the real SCPH1001 BIOS directly to game-space PC `0x80030000`.
  No game bytes were authorized by this tranche; `0x80030000` remains the
  honest fail-closed boundary for the authenticated game binding.
- Two probes agree exactly: 2,695,618 BIOS instructions / 452,182 dispatches,
  441,893 reads, 451,154 writes, 7,680 isolated writes, two device reads, and
  80 device writes. The resulting RAM epoch is
  `7301264d93bfaa6235a8655610d2674746a44d50b82645569f1e2f9b0e99a3b6`.
- Canonical evidence is `bios-frontier-02695618.json`. Continue by proving the
  loaded game identity at `0x80030000` against finalized game-shard authority,
  not by extending BIOS manifests or adding fallback.

## Authenticated BIOS callback tranche (2026-08-21)

- From `0x1B20` / RAM epoch `29d475f7...`, the corrected compiler advanced
  through five exact live frontiers: copied-RAM `[0x1B20,0x1B44)`, callback
  `[0x1420,0x1444)`, copied-RAM `[0x2724,0x278C)`, and immutable BIOS
  continuations `0xBFC04678` and `0xBFC0472C` inside the existing
  `[0xBFC04610,0xBFC04850)` range.
- `0xBFC0472C` was verified as the saved caller return restored by the
  `0xBFC04678` continuation, not a sequential-JR defect. Correct pre-delay
  predicate/target sampling and the corrected retail RAM aperture remain in
  force; no semantic or hardware workaround was added.
- Repeated probes stop identically at UnknownCode `0x00001508`: 138,122
  instructions, 26,099 dispatches, 15,905 reads, 23,822 writes, 6,400
  isolated writes, one device read, and 62 device writes. Final RAM epoch is
  `778e17e876cd4a9d39482302fb32ef36c80a5bcbfaa1029c06af95c61a2b7a8e`;
  canonical evidence is `bios-frontier-00138122.json`.

## BIOS allocator semantic fix (2026-08-21)

- The `0x1274` allocator runaway was a compiler delay-slot ordering defect.
  The live sequence computes `r1=(33 < 8193)` before `BEQ`, then its delay slot
  computes the unrelated `r1=(8193 < 32)`. Generated code incorrectly tested
  the second value. Conditional predicates and indirect targets are now
  captured before their delay slots, with adversarial emitter coverage.
- Early and 100,000th-hit state, allocator RAM words, recent PCs, and stable
  mutation/device counters are pinned in
  `clean-aot/catalog/evidence/bios-allocator-loop-diagnosis.json`.
- Two corrected real probes agree exactly: 121,186 instructions / 22,272
  dispatches, final RAM epoch `29d475f7311496a0337b75f0e7842703b62c25db595700c9a4b3abe49b1d537b`,
  and honest UnknownCode `0x00001B20`. See `bios-frontier-00121186.json`.

## Two-stage authenticated game-shard integration (2026-08-21)

- Installed direct launch now selects copied epochs by the current stopped PC
  and retries only hash-authenticated overlapping candidates. This removes the
  stale-registry-order rejection and matches the canonical 2,695,618-instruction,
  452,182-dispatch BIOS frontier at `0x80030000`. The subsequent BIOS-only smoke
  rejection is the expected resident-game identity boundary, not game execution.

- First-run setup can now consume the hash-pinned game-shard authority and its
  two closed manifests, derive the exact main and route-mutated NFE images from
  verified user media, and emit native sources without retaining source media.
- The generated product registry binds game identities/accessors one-to-one and
  links their sources into `Wipeout3SE.exe`; the installed runtime remains
  compiler/setup-free. Resident game dispatch hashes the complete RAM image and
  rejects identity mutation or PCs outside closed shard coverage before dispatch.
- The stale 727-unit snapshot is superseded by the current authenticated
  83,244-instruction/583-region receipt: main contributes 75,217 instructions
  in 450 regions and route-mutated NFE contributes 8,027 in 133. Setup,
  registry, and installer acceptance now re-derive these exact entry totals and
  identities from the packaged receipt rather than trusting aggregate metadata.
- Synthetic BIOS-to-game transition and identity-mutation tests pass. A genuine
  direct product launch still stops in the authenticated BIOS frontier; this is
  not evidence of real game execution and game-ready remains false.

## BIOS allocator-runaway checkpoint (2026-08-21)

- From UnknownCode `0x1174` / RAM epoch `6c6da3c3...`, exact live-region
  authentication advanced through `[0x1174,0x1408)`, callback
  `[0x1030,0x10B8)`, and the observed branch continuation
  `[0x10E4,0x113C)`. The adjacent entry at `0x10B8` remains unauthorized.
- The path no longer reaches UnknownCode. Two repeated bounded probes remain
  authenticated for the full 1,000,000-dispatch ceiling and stop at `0x1274`
  inside the allocator loop: 2,320,994 instructions, 13,663 reads, 22,646
  writes, 6,200 isolated writes, one device read, and 62 device writes.
- This is a materially different blocker, not an authorization frontier. The
  final RAM epoch is `a968043add6eed35610a894bbef805faeaafe205981e3402b7f9ec18953ca9db`;
  canonical evidence is
  `clean-aot/catalog/evidence/bios-frontier-02320994.json`. Diagnose the free
  list/state feeding the live `0x1274` loop before extending any code region or
  dispatch ceiling.

## Authenticated BIOS frontier checkpoint (2026-08-21)

- The real SCPH1001 BIOS path advanced through five additional exact,
  fail-closed frontiers from immutable BIOS PC `0xBFC00DA0`: copied-RAM
  `[0x406C,0x4134)`, `[0x2C94,0x2E00)`, `[0x3E80,0x3F04)`, and, after a
  measured RAM mutation, `[0x113C,0x1174)`.
- The repeated probe is deterministic at 120,902 instructions / 22,208 native
  dispatches, with 13,654 reads, 22,634 writes, 6,200 cache-isolated writes,
  one device read, and 62 device writes. It stops honestly at UnknownCode
  `0x00001174`; COP0 status/cause/EPC/bad-vaddr remain zero.
- The final low-RAM epoch is
  `6c6da3c3b6f7fcb315def23609b5118a25f94ee12e68ed75f0b9f34d43bd980a`.
  Reauthenticate the bytes at `0x1174` from this epoch before continuing; do
  not reuse the immediately preceding `34fa157f...` image.
- Canonical evidence is
  `clean-aot/catalog/evidence/bios-frontier-00120902.json`. No decoder,
  interpreter, dormant edge, or unknown-PC fallback was added.

## Native production presentation checkpoint (2026-08-21)

- The Windows SDL3/Vulkan presenter is now a complete live host backend rather
  than a shader-validation placeholder. It uploads raster scanout through a
  reusable staging texture, samples it into the swapchain viewport, owns exact
  44.1-kHz audio submission, and retains replacement RGBA textures.
- App graphics policy maps deterministically to 3840x2160 exact-4:3 defaults or
  explicit 16:9/21:9/32:9/custom output. The headless backend implements the
  same replacement upload contract with pinned digest `f2a57b68e1fd4d73`.
- The pinned DXC shader rebuild matches the checked-in SPIR-V header. Strict
  native GCC tests pass 11/11 for the app graph and 5/5 for the host graph,
  including a real Windows Vulkan frame/replacement upload runtime test.

## Live graphics integration checkpoint (2026-08-21)

- The application has an opt-in production scanout mode which drains the
  runtime GPU queue into application-owned raster VRAM, plans scanout from live
  GP1 display registers, decodes RGB555/packed-24-bit output, and submits it to
  the host within the same update.
- Runtime configuration supports 4K and explicit generic/custom wide modes.
  Exact 4:3 remains the default and has pinned end-to-end visual digest
  `a0c4b5214903b3dd` from native GP0 fill through host consumption.
- Replacement loading verifies generation-safe identity, confined canonical
  paths, SHA-256, declared dimensions, native WIC PNG-to-RGBA8 decode, and an
  explicit upload sink. Texture/font key derivation remains title-owned; no
  title hashes were added to generic code.
- Strict GCC application-graph tests pass 10/10. Focused Clang 22 ASan/UBSan
  tests pass 5/5 across app, GPU, render, and raster.

## Current position

- Cross-evidenced executable authorization v2 is ready in
  `clean-aot/catalog/evidence/executable-authorization.json` as a deterministic,
  fail-closed receipt (SHA-256
  `49f9ecb8f966849786b5c29370a1341018d0ec2c0bcf4af2d79787771636f83a`).
  Its delta receipt (SHA-256
  `941a5e8b2ab0a45b2821cbd9729c40503c78d74ba42f3b025c913e98a2f03dd1`)
  proves 1,166 formerly rejected NFE-to-main direct calls and adds 2,202
  authorized main-image instructions. It now authorizes 101,033 main-image,
  31,719 NFE, and 3,013 exact verified-BIOS direct-frontier instructions. It
  remains non-selectable: 71 game-image direct edges, 142 game-image indirect
  sites, and 27 BIOS indirect sites remain rejected. Exact categories and all
  270 main, 18 NFE, and 56 BIOS unknown ranges are in
  `docs/EXECUTABLE_AUTHORIZATION.md` and the JSON receipt.

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

The live graphics boundary now loads the pinned Wip3out HD manifest, maps its
292 present entries into texture/font/banner title classes, verifies and
decodes PNGs, and explicitly retires stale Vulkan handles on source-generation
changes. Missing entries preserve the exact native scanout digest. Deterministic
3840x2160 4:3 and 5120x2160 21:9 receipts plus a real permitted-pack WIC/upload
probe are recorded in `clean-aot/app/evidence/hd-assets-visual-receipt.json`.
Permission metadata has a fail-visible `pending-details` receipt; creator,
date, scope, attribution, and evidence remain null until supplied rather than
being inferred. Next graphics work is the independently validated producer-side
VRAM texture/palette producer contract is now independently pinned to Beetle
PSX HW source and implemented over clean GPU/raster state: upload-rectangle and
CLUT CRCs identify manifest entries, resolved generation-safe handles feed
primitive sampling, and every unsupported or stale case remains native. The
derivation and deterministic live digest are recorded in
`clean-aot/app/evidence/beetle-upload-key-contract.json`. Next graphics work is
capturing broader clean game routes to measure manifest coverage and adding
hardware geometry sampling so the Vulkan path need not depend on CPU scanout.

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
- The live graphics drain now consumes generation-validated polygon provenance,
  applies explicit native/expanded projection and culling, perspective-correct
  UV/color interpolation, replacement sampling, and 4K/ultrawide scanout with
  byte-exact native fallback. The CPU/GTE/RAM producer still must publish those
  candidates for every shipping game polygon; incomplete provenance deliberately
  remains on the native raster path.

## Recovery verification (2026-08-21)

- Canonical product checkout `master` is preserved at `97bd3d7`, 95 commits
  behind `origin/master` (`768cdbf`), with locally advanced `psxrecomp` and
  `recomp-ui` submodules. It is the legacy/product lane, not the clean shipping
  implementation.
- `codex/remaster-cleanroom` is the clean integration branch. Nine isolated
  tranche branches remain divergent and should be treated as review receipts,
  not independently shippable products.
- Fresh aggregate GCC 16.1 Release configure/build succeeded from
  `clean-aot/CMakeLists.txt`; after correcting the aggregate to include media,
  runtime, and host, all 18 CTest targets passed.
- Fresh aggregate Clang 22.1.8 RelWithDebInfo with non-recovering ASan/UBSan
  built every component and passed the same 18 CTest targets with no sanitizer
  report.
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

## Authenticated bootstrap checkpoint (2026-08-21)

- The clean application now remeasures finalized setup, catalog, selection,
  BIOS, executable, CUE, and track inputs, mounts the measured disc, initializes
  the clean runtime at 100 Hz, loads the selected executable through its
  generated binding, and emits a canonical fail-closed bootstrap receipt.
- Synthetic end-to-end coverage reaches the first unknown generated PC and
  records zero semantic guest instructions; it intentionally does not convert
  control-flow dispatch into a guest-execution claim.
- The real executable authorization receipt is
  `clean-aot/catalog/evidence/executable-authorization.json` with status
  `partial-fail-closed`. It records 101,033 authorized main-EXE instructions,
  31,719 authorized NFE entry-5 instructions, and 3,013 verified BIOS
  direct-frontier instructions, but unresolved closure prevents a finalized
  selectable catalog and generated artifact.
- Consequently the deepest real product state remains authenticated evidence
  generation before runtime dispatch: BIOS instructions executed = 0 and game
  instructions executed = 0. Do not report BIOS or game boot until complete
  semantic generated regions and BIOS entry/reset-state binding exist.
- Fresh GCC and Clang ASan/UBSan application graphs each passed all 8 aggregate
  tests. Standalone host/render/GPU/raster passed all 4 tests; focused product
  and authorization Python tests passed 8 tests. Repository-wide pytest still
  collects five unrelated legacy `psxrecomp` failures (encoding/newline,
  obsolete assertions, and the live readram socket probe).

## Real BIOS reset execution checkpoint (2026-08-21)

- The runtime now has a distinct authenticated BIOS reset binding at
  `0xBFC00000`, immutable BIOS hash verification, generated CPU reset state,
  fail-closed generated dispatch, instruction accounting, and an explicit
  authenticated BIOS-to-game transition contract.
- `clean-aot/catalog/bios-reset-corridor.manifest` compiles only the 30
  cross-evidenced reset instructions. With the locally verified SCPH1001 BIOS,
  the generated region executed all 30 instructions, including two clean-bus
  MMIO stores, then stopped at the first excluded target `0xBFC00150`.
- The canonical real execution receipt is
  `clean-aot/catalog/evidence/bios-reset-execution.json`. This is the first real
  clean BIOS instruction execution in the repository: BIOS instructions = 30,
  game instructions = 0. No interpreter, JIT, legacy runtime, or fallback was
  linked or invoked.
- Next: cross-evidence and authorize the corridor beginning at `0xBFC00150`,
  extend generated load/store/COP0 semantics as encountered, then continue the
  same receipt chain until the authenticated game transition is reached.
- The next verified semantic frontier is 104 instructions in
  `clean-aot/catalog/evidence/bios-frontier-00000104.json`. Generated MTC0,
  MFC0, and RFE now operate on explicit COP0 status/cause/EPC/bad-address state.
  The real BIOS reaches `0x80000080` and fails closed because the RAM exception
  vector's copied-content epoch is not yet authorized.
- ADDI overflow semantics and successful same-PC generated dispatch advance the
  next deterministic frontier to 1,509 instructions across 67 dispatches in
  `clean-aot/catalog/evidence/bios-frontier-00001509.json`. The stop remains the
  exact fail-closed RAM exception vector `0x80000080`; no RAM content epoch or
  indirect target was inferred from the address alone.
- SLT/SLTU plus ANDI semantics advance authenticated execution to 17,353
  instructions and 3,232 dispatches. The new receipt
  `clean-aot/catalog/evidence/bios-frontier-00017353.json` stops without an
  exception at `0xBFC0703C`; this is a finite runtime return target inside the
  exact BIOS and can seed the next static direct-closure pass.
- Explicit COP0 breakpoint/debug-register state plus proved generated roots at
  `0xBFC03978` and `0xBFC00420` advance the deterministic real BIOS frontier to
  71,501 instructions across 12,282 dispatches. The canonical receipt is
  `clean-aot/catalog/evidence/bios-frontier-00071501.json`: 9,023 memory reads,
  13,452 memory writes, one device read, and 30 device writes. Its numeric stop
  PC is `0xA0000500` (an earlier handoff revision incorrectly rendered it as
  `0xA0000080`); the next tranche must authenticate the copied RAM execution
  epoch before binding that PC.
- The exact initial 64-KiB RAM epoch is SHA-256 `63dc69dd...a901c`; its bounded
  executable page `[0x500,0x1000)` is SHA-256 `99d536ee...e9570`. Distinct
  uncached and cached bindings preserve the BIOS transition
  `0xA0000500 -> 0x00000598` and invalidate on code-page mutation. After the RAM
  clear returns to BIOS and the proved `0xBFC042D0` root executes, the repeated
  frontier reaches 79,218 instructions and 13,830 dispatches, with 9,229 reads,
  14,991 writes, one device read, and 31 device writes. Receipt
  `clean-aot/catalog/evidence/bios-frontier-00079218.json` records the next RAM
  epoch SHA-256 `c1d13084...6b1744` and fail-closed PC `0x000000C0`, where the
  BIOS-installed low-RAM vector trampoline now begins.
- Exact-range epoch binding advances through the physical `0xC0` trampoline,
  dispatch table at `0x600`, selected handler at `0x540`, and exception-vector
  installer at `0xEB0`. Writes elsewhere on the same RAM page force a byte-hash
  recheck before refreshing the page stamp; any authenticated instruction-byte
  change still fails closed. Two repeated runs reach 79,382 instructions and
  13,864 dispatches with 9,253 reads, 15,013 writes, one device read, and 31
  device writes. Receipt `bios-frontier-00079382.json` stops cleanly at the next
  unbound physical RAM target `0x00006A70`; final 64-KiB RAM SHA-256 is
  `f1f4eeac...766c8`.
- The authenticated A0 call-44 stub at `0x00006A70` now transitions through
  the exact low-RAM A0 trampoline and service dispatcher to BIOS
  `0xBFC01920`. The compiler preserves `0xBFC0193C` both as the preceding
  jump's architectural delay slot and as an explicitly authenticated
  standalone entry; incidental edges into delay slots remain rejected. Two
  repeated probes first cross `0xBFC01940`, then reach 80,802 instructions and
  13,936 dispatches with 9,254 reads, 16,256 writes, one device read, and 34
  device writes. Receipt `bios-frontier-00080802.json` stops fail-closed at
  physical PC `0x000000B0`. The service-mutated 64-KiB RAM epoch is SHA-256
  `a8d34d5b...d4f4`; its zero-filled B0 target is deliberately not authorized
  as executable code.
- The zero-filled B0 diagnosis found a runtime hardware bug, not missing BIOS
  installation: A0/44 (`FlushCache`) sets COP0 Status IsC and issues 2,480
  cache-maintenance stores through cached KUSEG addresses. Those stores must
  target the isolated R3000A cache, but the runtime had forwarded them to RAM
  and erased the installed A0/B0/C0 vectors. Generated byte, halfword, and word
  stores now preserve RAM while IsC is effective; KSEG1 remains the uncached
  bus path. Adversarial coverage proves both behaviors.
- With the preserved exact epoch, B0 contains its installed trampoline to
  `0x000005E0`; the B0 dispatch table then selects the authenticated copied-RAM
  routine at `0x00000F2C`. Two repeated probes advance to 80,867 instructions
  and 13,947 dispatches, including 2,480 isolated-cache writes, and stop
  fail-closed at the next unbound copied-RAM PC `0x000027C0`. Receipt
  `bios-frontier-00080867.json` supersedes the hardware-invalid 80,802 receipt
  and records the new RAM epoch SHA-256 `c0020392...0cd3`.
- The `0x27C0` epoch executes 26 instructions before its own code writes change
  the whole-RAM identity to `39498839...036a5`; dispatch stops with
  `EpochMismatch` at `0x2150`, then resumes only after that new identity is
  compiled. Overlap reconciliation now rehashes exact prior ranges before
  disabling them, so unrelated same-page writes no longer discard the
  byte-identical A0/B0 trampoline; changed instruction bytes still invalidate.
- The refreshed epoch advances through copied-RAM initialization, immutable
  BIOS `0xBFC086B0`, B0 dispatch, and A0 service 0x2A. A subsequent RAM mutation
  stops before dispatch at `0x00003C90`. Two repeated runs reach 84,520
  instructions and 14,791 dispatches with 9,375 reads, 17,089 writes, 2,480
  isolated-cache writes, one device read, and 44 device writes. Receipt
  `bios-frontier-00084520.json` records fail-closed status `EpochMismatch` and
  next RAM epoch SHA-256 `6f30b27d...cc50b`.
- The `6f30b27d...cc50b` epoch is now authenticated only through exact function
  ranges `[0x3C90,0x3E78)`, `[0x28D0,0x2908)`, `[0x4360,0x4388)`,
  `[0x27A0,0x27BC)`, and `[0x2908,0x2958)`. The immutable BIOS return stub
  `[0xBFC06FDC,0xBFC06FE4)` is separately rooted; no containing RAM page was
  authorized. Two repeated probes reach 85,983 instructions and 14,874
  dispatches with 9,382 reads, 18,339 writes, 3,720 isolated-cache writes, one
  device read, and 48 device writes. Receipt
  `bios-frontier-00085983.json` stops fail-closed with `UnknownCode` at the next
  copied-RAM function `0x00002E00`; whole-RAM identity remains
  `6f30b27d...cc50b`.
- The exact copied-RAM function `[0x2E00,0x2E98)` reaches helper
  `[0x30C8,0x3108)`. Its first branch delay slot exposed missing static `SLTI`
  lowering; generated SLTI/SLTIU now use signed/unsigned comparisons against a
  sign-extended immediate instead of raising a false Reserved Instruction.
  Execution returns exactly to `[0x2958,0x2A64)` and stops at its direct helper
  `0x00003060`. The runtime's bounded copied-region table is 64 entries so this
  growing exact-range chain does not weaken into page authorization. Repeated
  probes reach 86,062 instructions and 14,893 dispatches with 9,388 reads,
  18,348 writes, 3,720 isolated-cache writes, one device read, and 48 device
  writes. Receipt `bios-frontier-00086062.json` records `UnknownCode` and the
  post-callback whole-RAM epoch `8af29f79...f1c9a`.
- Exact region `[0x3060,0x30C8)` under epoch `8af29f79...f1c9a` follows its
  observed loop and returns through the live `$ra` callback to `0x000031E8`;
  the dormant direct edge to `0x2FC8` is not speculatively dispatched. Two
  repeated probes reach 86,085 instructions and 14,899 dispatches with 9,391
  reads, 18,350 writes, 3,720 isolated-cache writes, one device read, and 48
  device writes. Receipt `bios-frontier-00086085.json` records `UnknownCode` at
  `0x000031E8`; RAM remains epoch `8af29f79...f1c9a` and COP0 reports no fault.
- The executed path through exact region `[0x31E8,0x33A4)` reaches only the
  exact A0/0x25 call stub `[0x6A90,0x6A9C)`. That stub traverses the existing
  authenticated A0 trampoline and BIOS service, then returns to the next
  unbound copied-RAM function at `0x00003108`; dormant edges remain rejected.
  Two repeated probes reach 86,291 instructions and 14,939 dispatches with
  9,422 reads, 18,369 writes, 3,720 isolated-cache writes, one device read, and
  48 device writes. Receipt `bios-frontier-00086291.json` records `UnknownCode`;
  RAM remains epoch `8af29f79...f1c9a` and COP0 remains fault-free.
- Exact region `[0x3108,0x31E8)` explicitly roots `0x31A0`, which is both a
  live branch target and another branch's architectural delay slot; no general
  delay-slot relaxation was added. Its live path reaches only A0/0x17 stub
  `[0x6B10,0x6B1C)`, then immutable BIOS comparison routine
  `[0xBFC03288,0xBFC03310)`. The service returns to copied RAM `0x0000283C`
  after mutating the whole-RAM epoch to `9548d3d4...4a35d`; stale authorization
  is rejected there. Two repeated probes reach 86,834 instructions and 15,057
  dispatches with 9,521 reads, 18,423 writes, 3,720 isolated-cache writes, one
  device read, and 49 device writes. Receipt `bios-frontier-00086834.json`
  records the new `UnknownCode` frontier with fault-free COP0 state.
- Under epoch `9548d3d4...4a35d`, only continuation `[0x283C,0x2870)` is
  authenticated. It calls the already-exact `0x4360` helper, then reaches A0
  service 0x96 through exact stub `[0x6AA0,0x6AAC)`; the subsequent A0/0x97
  call remains dormant and unbound. The live service enters only immutable BIOS
  wrapper `[0xBFC085B0,0xBFC085D4)` and returns to unbound copied RAM
  `0x00003C2C`. Two repeated probes reach 86,892 instructions and 15,069
  dispatches with 9,524 reads, 18,432 writes, 3,720 isolated-cache writes, one
  device read, and 50 device writes. Receipt `bios-frontier-00086892.json`
  records `UnknownCode`; RAM remains `9548d3d4...4a35d` and COP0 is fault-free.
- Fresh epoch-`9548d3d4...4a35d` bytes authenticate only `[0x3C2C,0x3CE0)`
  (SHA-256 `814b8e26...6a705`) and its live A0/0x2A stub
  `[0x6B20,0x6B2C)` (SHA-256 `ad7c5bc5...2876d`). A0/0x2A traverses the
  existing exact BIOS path, performs another 1,240 isolated-cache writes, and
  mutates RAM to epoch `9898a802...6e6d`. Execution then reaches formerly
  dormant A0/0x97 stub `0x00006AB0` and rejects it because it has not been
  authenticated in the new epoch. Two repeated probes reach 88,864
  instructions and 15,237 dispatches with 9,614 reads, 19,757 writes, 4,960
  isolated-cache writes, one device read, and 53 device writes. Receipt
  `bios-frontier-00088864.json` records fault-free `UnknownCode` at `0x6AB0`.
- Epoch-`9898a802...6e6d` A0/0x97 stub `[0x6AB0,0x6ABC)` is authenticated
  by exact SHA-256 `dd2df7ea...47d07`. Its sole live immutable wrapper is
  `[0xBFC0C1FC,0xBFC0C220)`, which enters the already-authenticated service path;
  adjacent A0 and BIOS wrappers remain rejected. The service performs another
  1,240 isolated-cache writes, mutates RAM to `315de527...d56fa`, and stops at
  the next unbound immutable BIOS target `0xBFC00DA0`. Two repeated probes reach
  90,914 instructions and 15,420 dispatches with 9,710 reads, 21,094 writes,
  6,200 isolated-cache writes, one device read, and 57 device writes. Receipt
  `bios-frontier-00090914.json` records fault-free `UnknownCode` at that target.

- Frontier 89: the authenticated second corridor `[0xBFC00150,0xBFC0023C)`
  executes another 59 instructions, including memory-control and cache-control
  initialization, then rejects the unbound `MTC0` PC at `0xBFC0023C`. The
  repeated deterministic receipt is
  `clean-aot/catalog/evidence/bios-frontier-00000089.json`.
- Do not commit disc images, retail BIOS data, patched executables, captures,
  generated C, screenshots, saves, or packaged binaries.

- Registry capacity is no longer authorization: runtime construction now owns
  a fixed manifest-sized registry (default 512, strict upper bound 4096),
  allocation/configuration fails closed, invalidated slots are reused, and all
  binding range/hash/stamp checks remain exact. More than 256 exact bindings
  are covered by the runtime adversarial test.
- Immutable BIOS shard `[0xBFC03FAC,0xBFC043AC)` (SHA-256 `a9a3f29f...50027`)
  now has an exact ROM contract and advances through `0x18BC` and `0x19C8` to
  copied-RAM `0x1858`. Receipt
  `bios-shell-registry-frontier-00001858.json` records 19,270,447 instructions,
  1,787,127 dispatches, zero CD MMIO, and fail-closed compiler rejection at
  `0x1858`: `control transfer has no bounded delay-slot instruction`.
