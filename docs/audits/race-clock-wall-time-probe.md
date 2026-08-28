# Race-clock versus wall-time acceptance probe

## Outcome

The deterministic gameplay-speed oracle is the stock title's elapsed race
clock, observed immediately before its authoritative store and compared with a
monotonic host clock. This rejects the failure mode where the runtime reports
100 guest VBlanks per second and healthy 44.1 kHz audio while the race itself
advances at roughly 40% speed.

The probe is read-only, Release-safe, and off by default. It does not use the
Hueponik patch engine or write guest RAM.

## Static ownership proof (SCES-02845)

Generated owner `func_8010D808` contains this sequence:

| Guest PC | Stock instruction | Meaning |
| --- | --- | --- |
| `0x8010DEB8` | `lw a3, -0x1dec(at)` | Load global timer delta from `0x801CE214` |
| `0x8010DEC0` | `lw v0, 0x114(a1)` | Load per-racer elapsed race clock |
| `0x8010DEC8` | `addu v0, v0, a3` | Advance elapsed race clock |
| `0x8010DECC` | `sw v0, 0x114(a1)` | Authoritative elapsed-clock store |

The same owner builds `0x801CE214` at `0x8010D8E4..0x8010D8F0` as twice its
selected race-clock tick. The direct PAL100 scheduler must reproduce Hue's
divide-by-two sequence: `0,2,0,2...` centiseconds across 100 VBlanks, or one
centisecond per 10 ms on average. Known-good full-Hue states corroborate
`[0x801756F4]=1` and `[0x801CE214]=2` at the corresponding phase; the similarly
spelled `0x801D56F4` is unrelated and remains zero. The title's time-formatting
path also decomposes these values through decimal and sexagesimal divisions.
The public SCES-02845
"Total time 0:00:00" Xplorer code independently targets `0x8010DECC`,
corroborating that this is the total-race-time store rather than a generic
frame counter.

The concrete rate-1 receipt is sibling state
`wipeout3se-cleanroom/build-remaster-max-pgxp-20260823/candidate-saves/scph1001/state_80162474_slot01.pst`
(SHA-256 `75cce1345186d697981f2f9dc4e861231cee242e634e22a87227ad8c803f79da`).
Its 8 MiB RAM section records `0x801750A4=1`, `0x801750A8=0x4000`,
`0x801750AC=0x0862`, `0x801756F4=1`, and `0x801CE214=2`.

## Probe contract

`src/mods/wipeout3_race_clock_probe.c` registers:

- activation ID `wipeout3.diagnostic.race-clock-on`;
- instruction-site ID `wipeout3.diagnostic.race-clock-store` at
  `0x8010DECC`;
- environment gate `PSX_WIP3_RACE_CLOCK_DIAG=1`.

At the pre-store callback, `a1` (`gpr[5]`) owns the timer at offset `0x114`,
and `v0` (`gpr[2]`) is the pending new centisecond value. The callback reads
the old word, verifies that every store delta is zero or two centiseconds,
measures a stable object for at least two host seconds, prints one compact
window, and returns zero so the stock store executes. Timer resets, pointer
changes, discontinuities, and host gaps reset the window instead of
manufacturing a result.

Example:

```text
psxrecomp: wip3 race clock: object=0x8018A240 start_cs=120 end_cs=320 game_seconds=2.000000 wall_seconds=2.004000 ratio=0.998004 samples=200 zero_steps=100 two_steps=100 average_step_cs=1.000000 eligible=1
```

`tools/summarize_wip3_race_clock.py` requires at least two eligible active-race
windows by default. Every window—not merely their average—must contain both
zero- and two-centisecond stores, average `0.99..1.01` centiseconds per store,
and have `game_seconds / wall_seconds` in `[0.95, 1.05]`. Missing windows,
internally inconsistent fields, a constant-two (2x timer) stream, and a 0.4x
game clock all fail closed.

## Required integration (intentionally not made in this audit)

The owning integration change must:

1. add `0x8010DECC` to `[recompiler].mod_instruction_sites` in `game.toml`;
2. select both plugin IDs in the diagnostic package/plan used by the candidate;
3. export `PSX_WIP3_RACE_CLOCK_DIAG=1` only for the bounded probe run;
4. run `tools/summarize_wip3_race_clock.py` on the Release log and require exit
   code zero.

The probe only produces eligible windows during an active race. Automation
must enter or wait for a race; a title/menu-only run correctly fails for lack
of windows.

## Focused verification

```powershell
python tools/test_summarize_wip3_race_clock.py
python tools/summarize_wip3_race_clock.py tools/testdata/wip3-race-clock-pass.txt
python tools/summarize_wip3_race_clock.py tools/testdata/wip3-race-clock-slow.txt
```

Static provenance for the corroborating encrypted cheat and its decoder:

- <https://gamehacking.org/game/92846>
- <https://github.com/mlafeldt/xpcrypt>
