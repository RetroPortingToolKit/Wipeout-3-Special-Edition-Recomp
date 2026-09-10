# Runtime acceptance-receipt audit — 2026-08-22

This document defines the minimum authenticated evidence needed to claim that
the requested remaster features are effective at runtime. Configuration is not
treated as proof.

## Authentication boundary

Every receipt must identify the executable/build hash, authenticated BIOS and
disc/title hash, staged `game.toml`, `mods/state.toml`, renderer, host refresh
mode, and a monotonic capture interval. Raw debug JSON must be retained, not
only a derived FPS number. The receipt must also record disabled turbo, rewind,
netplay, and auto-skip paths.

## Minimum receipt matrix

| Claim | Configured evidence | Runtime-proven receipt | Fail condition |
| --- | --- | --- | --- |
| 100 Hz gameplay | `game.toml`, PAL package/plugin, selector 2 from `tools/verify_pal100_title.py` | A race interval with VBlank and gameplay updates at 99.5–100.5/s, `swap_interval: 0`, active 10 ms pacing, bounded debt, and no increasing audio underruns/overflows | VBlank-only, headless/turbo output, or package selection without live title updates |
| 50 Hz FMV | PAL x2 MDEC policy in `psxrecomp/runtime/src/main.cpp` | Active depth-24/MDEC interval with guest pacing near 100/s and movie swaps at 49–51/s; skipped movie ticks still pace guest/audio | 100/s movie swaps, skipped pacing, or screenshot-only evidence |
| PGXP effectiveness | `CMakeLists.txt`, `game.toml` PGXP flags, selected PGXP package | Authenticated initialized backend plus deterministic PGXP on/off A/B showing expected geometry/depth changes, no fallback, and reset/save-state continuity | Build/config presence or a PGXP label alone |
| 8 MB activation | `mods/state.toml`, `game.toml`, `tools/verify_8mb_runtime_receipt.py` | `live_ram_bytes: 8388608`, activation before memory/BIOS/game init, high-address read/write, high-RAM DMA, 8 MB save-state metadata, and passing 8 MB + 900% pairing | Backing allocation or package selection without live mapping evidence |
| 900% CPU pacing | `game.toml: cpu_overclock = 900`, source boundary audit | CPU retirement scales while CRTC, SPU, CD, DMA, and timers remain in their normal cycle domain; 100 Hz route stays within the 10 ms budget | Treating 900% as 9× frames/audio/CRTC or using static output as live proof |

## Current disposition

The project has strong configured evidence and several partial runtime probes,
but no single authenticated receipt satisfies every row. In particular,
`vblank_rate` is not by itself a gameplay-update counter, and the retained
FMV/race evidence still leaves long-run audio, race, PGXP-effectiveness, and
normal-vs-fast-loading closure open.

**Configured: yes. Runtime-proven as a complete combined acceptance claim: no.**
