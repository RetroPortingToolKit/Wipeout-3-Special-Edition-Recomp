# GooseStation / DuckStation 8 MB main-RAM deep audit

Date: 2026-08-22  
Target: WipEout 3 Special Edition recompiled build  
Scope: RAM selection and mapping, BIOS exposure, generated-code dispatch, state, DMA, overclock, and title-hack interaction.

## Executive result

The selected release profile does request GooseStation-compatible 8 MB RAM. The
runtime starts with an 8 MB backing allocation but defaults the live map to
retail 2 MB; the `psx.enhancement.8mb-ram` plugin must set the session request
before `memory_init()`. The high window is therefore real only when the package
is selected early enough. The existing evidence includes a guest store at
`0x80780AF8`, so this is not merely unused host capacity.

The principal defect is configuration-level, not the normal selected-profile
path: the 8 MB package is opt-in and the title packages do not declare it as a
dependency. Missing state, omitted package, or a stale/hand-built profile can
silently boot with 2 MB. No runtime core change is made by this audit.

## Evidence boundary

The pinned `psxrecomp` submodule commit is `1b860bae0497a1d5ac8b0259d8a9a0f23b073c0e`,
but the remote no longer serves that object; `git submodule update --init`
failed with “does not contain ...”. Consequently, source-level statements below
use the prior checked-in parity audit's recorded paths and observed artifacts,
not an unverified checkout of a different upstream revision. Current upstream
documentation independently describes `runtime/src/memory.c` as RAM/ROM/
scratchpad/MMIO routing and the execution model as AOT code plus dirty-RAM
fallbacks; it is useful context, not proof of this pinned build.

Primary local evidence: `docs/audits/hardware-parity.md`, `game.toml`,
`mods/state.toml` in the release tree, `tools/gen_hueponik_framerate_mod.py`,
`tools/verify_one_folder_release.ps1`, and the retained `psx_last_run_report.json`
and save-state logs referenced by the parity audit.

## Selection and address map

The effective sequence is:

```text
load config/state -> resolve mod plan -> clear prior RAM request
-> activate 8 MB plugin -> memory_init() -> start BIOS/guest
```

The recorded implementation contract is:

| Region/operation | Effective behavior | Risk or proof status |
|---|---|---|
| `0x00000000..0x001FFFFF` physical RAM | Unique retail low 2 MB | Required baseline; covered by normal guest execution. |
| `0x00200000..0x007FFFFF` physical RAM | Unique only after 8 MB activation | High-detail code/data depend on this being live. |
| KSEG aliases | Virtual address is normalized to physical RAM before map lookup | Alias behavior is documented; an independent exhaustive page-alias test is not retained. |
| Unregistered high pages | Fold to the low 2 MB alias | DuckStation-style compatibility behavior; callers must not infer that every high address is unique. |
| High relocated engine (`0x80780000+`) | Package overlays/trampolines target this range | `last_store_pc=0x80780AF8` proves execution reached it. |
| Scratchpad/MMIO/BIOS | Separate routing domains, not expanded by 8 MB | No evidence that 8 MB leaks into MMIO or BIOS windows. |

The important distinction is “8 MB backing store” versus “8 MB registered
address space.” A silent fallback can retain the former while exposing only the
2 MB map. Logs showing the unique-8-MB startup line and a high-region PC are the
minimum useful runtime proof.

## BIOS exposure and boot

`game.toml` requires the external retail `SCPH1001` boundary (`openbios=false`,
`bios_hle=true` is the controlled shell-skip policy). BIOS/kernel RAM layout is
therefore part of the state identity: the BIOS initializes low RAM and the game
loader later copies/loads game content there. The 8 MB extension is not a claim
that retail BIOS code can freely use all 8 MB; it supplies additional unique
guest RAM for the title's relocated enhancement engine.

The safe boot ordering is activation before `memory_init()`, before BIOS code or
the EXE can observe the map. A late toggle would be unsafe: BIOS/loader writes,
generated dispatch keys, and saved RAM contents could have been interpreted
under the 2 MB alias map. Existing boot evidence reaches `game_started=1`, but
does not constitute a byte-for-byte BIOS-HLE-off equivalence proof.

## Load/store, recompiler, and DMA consequences

All generated MIPS loads/stores must enter the shared RAM/MMIO router. A direct
host-array shortcut is incorrect for aliased virtual addresses and for the
high-window registration boundary. The documented implementation keeps code
address canonicalization separate from data mapping: an aliased code PC may be
folded for AOT lookup even when a high physical page is unique. This is why a
high-PC dispatch/guard observation is useful but does not by itself prove every
high-page data access.

CD and memory-card DMA are consumers of the same guest address routing. The
DMA engine must resolve its destination through the live map, preserve width and
wrap semantics, and raise the same completion/IRQ sequence regardless of whether
the target lies in low or high RAM. The retained load-gate evidence proves
32 sequential memory-card reads and CD/XA activity on the selected profile; it
does not prove a dedicated high-RAM DMA matrix. No evidence supports a silent
DMA-specific 2 MB fallback, so this remains an untested boundary rather than a
reported bug.

## Save states and BIOS identity

The state format records the resolved mod-plan fingerprint and the live RAM
section length (`2 MB` versus `8 MB`). A length mismatch is rejected, and high
page registration is rebuilt after restore. Same-profile 8 MB save/load has
passed in the retained continuity/current-load logs. BIOS identity is also a
compatibility boundary: retail and OpenBIOS layouts must not share a state.

The CPU overclock is not serialized. A state saved at 900% can load with another
effective overclock if mod fingerprint and RAM length still match. This does
not create a RAM fallback, but it makes timing-sensitive continuation
non-reproducible. A 2 MB/8 MB mismatch should fail closed; the missing test is a
deliberate cross-profile restore receipt, not an assumption that failure occurs.

## Overclock and title hacks

`game.toml` requests `cpu_overclock=900`. The recorded cycle implementation
scales CPU retirement charges, while CRTC, SPU, CD, and other device service stay
in the normal cycle domain. Thus 900% is not a nine-times refresh or DMA speed.
PAL x2/100 Hz is a separate title/CRTC feature.

The generator labels `ntsc8`, `ntscfull8`, `pal8`, `palfull8`, and 8 MB 120-Hz
variants as requiring 8 MB and stages the enhancement engine at
`0x80780000+`. The package manifests do not enforce the complete tuple
“title patch + 8 MB + 900%”; therefore these combinations remain accepted:

| Combination | Expected outcome | Classification |
|---|---|---|
| Enhanced/Full patch + 8 MB + 900% | Intended path; high-PC evidence exists | Applied, not full parity-proven |
| Enhanced/Full patch + 2 MB | High engine aliases/fails or dispatches incorrectly | Silent 2 MB fallback risk |
| 8 MB + 100% | High RAM exists; timing/performance contract differs | Configuration gap |
| 900% + stock 2 MB | Fast CPU with no high engine capacity | Configuration gap |
| PAL x2 + missing 8 MB dependency | PAL timing can activate independently | Particularly easy partial profile |

## Boot / race / save-state matrix

| Scenario | Required setup | Evidence now | Acceptance gap |
|---|---|---|---|
| Cold retail-BIOS boot | Same BIOS/disc, state present, 8 MB activation before init | `game_started=1`; unique-8-MB log in selected profile | A/B against missing/late activation and BIOS-HLE-off |
| BIOS → title | 8 MB + 900%, no PAL x2 | High-region execution report | Full BIOS/kernel RAM fingerprint equivalence |
| Menu → card load | 8 MB, normal speed | 32 card reads captured in route work | Compare low/high destination DMA and IRQ order |
| Menu → race load | 8 MB, PAL x2 off for isolation | Route reaches on-track race; guest cadence ~99–101 Hz | Equal normal/accelerated CD/DMA/fingerprint endpoint |
| Save before card/race load → restore | Same BIOS/mod/RAM size | Same-profile save/load succeeded | Cross-2/8 MB rejection receipt; serialize/check overclock |
| PAL x2 / title hack | Authenticated PAL package plus 8 MB | Title selector/package evidence | Race, CDDA/XA, timeout, and state equivalence at x2 |
| 900% overclock | 8 MB profile, device clocks unchanged | Guest-clock checks pass | Long race soak and cross-state timing proof |

## Silent fallback verdict and required follow-up

Confirmed risk: absent or disabled `mods/state.toml`/8 MB feature selects the
default 2 MB live map without a hard failure. The current release verifier
requires the package, so the shipped release path is guarded; ad-hoc launches
and incomplete profiles are not. High-PC instruction guards/interpreter use are
performance fallbacks, not evidence of RAM fallback.

Before claiming full GooseStation/DuckStation parity, retain these receipts:

1. cold boot with explicit effective RAM size and high-page probe;
2. low/high RAM load/store and CD/card DMA address/IRQ trace;
3. equal-route normal versus accelerated race-load fingerprints;
4. same-state 2 MB↔8 MB rejection and BIOS identity rejection;
5. title-hack matrix with 8 MB and 900% enforced, plus PAL x2 race/CDDA soak.

No runtime core, shared configuration, or mod implementation was changed for
this audit.
