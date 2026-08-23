# GooseStation/DuckStation CPU overclock deep audit — WipEout 3 SE

Date: 2026-08-22  
Scope: the checked-out WipEout 3 SE project and its pinned GooseStation-compatible
framework surface, compared with the public DuckStation implementation. This is
an audit only; no runtime or shared configuration was changed.

## Executive conclusion

The repository’s `cpu_overclock = 900` is semantically a 900% CPU-clock setting,
not “900% faster frames” and not a nine-times CRTC, CD, GPU, or audio multiplier.
DuckStation converts 900% to the reduced fraction `9/1`; its overclock state is
then consumed by CPU timing. The useful WipEout pairing is therefore:

```
title hack / 8 MB profile  -> makes the enhanced game path valid
CPU 900%                  -> supplies the required emulated CPU timing/perf mode
PAL x2 / 100 Hz           -> separate CRTC/VBlank and presentation policy
CD, DMA, GPU, SPU         -> remain hardware peripherals with their own timing
```

This repo expresses the separation correctly in `game.toml:47-55` and in the
PAL-x2 plugin. It does not yet prove that a 900%-accelerated WipEout session is
equivalent across save-state restore, CD/DMA ordering, XA/CDDA, or every title
hacking variant. The existing `docs/audits/hardware-parity.md` and
`docs/audits/pacing.md` correctly keep those gates open.

## What “900%” means

DuckStation’s current settings implementation reduces a percentage by its GCD:
`numerator = percent / gcd(percent, 100)` and `denominator = 100 / gcd(percent,
100)`. Thus 900% becomes `9/1`, and the active flag is true when the fraction is
not `1/1` ([settings.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/settings.cpp)).
The UI describes the value as the percentage of the normal emulated clock, and
warns that overclocking can break games and increase requirements
([consolesettingswidget.cpp](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/consolesettingswidget.cpp)).

Therefore 900% means an emulated CPU clock of nine times the nominal CPU clock
for CPU scheduling purposes. It does not mean nine times host refresh, nine
times CD spindle speed, nine times SPU sample rate, or nine times GPU scanout.
The public DuckStation option history also shows 900% as one value in a CPU
overclock menu, alongside 100% default and 1000% ([option addition](https://repo.retrodeck.net/Xargon/Duckstation/commit/8edf02bca8e3ace410cb01045316efcf8c9ef20f)).

## Scheduler and clock domains

The important semantic boundary is guest time. A CPU overclock changes how much
CPU work can be scheduled relative to the nominal CPU clock; it must not turn
an event-driven peripheral into a CPU-speed peripheral. DuckStation keeps CPU,
DMA, timers, GPU, CD-ROM, and SPU as separate core subsystems (the core build
lists `cpu_core`, `cdrom`, `dma`, `gpu`, `spu`, and `timers`
[here](https://github.com/stenzek/duckstation/blob/master/src/core/CMakeLists.txt)).

For WipEout, the audit consequence is:

| Subsystem | 900% interpretation | WipEout proof required |
|---|---|---|
| CPU/scheduler | 9:1 CPU overclock state; not a host FPS promise | CPU-cycle and title-gate trace |
| DMA | Transfer/event ordering must remain hardware-observable | DMA request, completion, IRQ and RAM-window comparison |
| CD-ROM | Read/seek/sector cadence is not “9x CD” | command, sector, redelivery, IRQ and XA/CDDA trace |
| GPU/CRTC | Scanout/VBlank is separate; PAL x2 is separate again | CRTC period, VBlank, GPU FIFO/IRQ and presentation trace |
| SPU/audio | Sample production remains guest-clock-derived | SPU/CDDA/XA samples, host consumption and underrun trace |

DuckStation’s CD-ROM documentation describes the CD controller as a state
machine with sector buffers, data-DMA requests, XA/CDDA paths, and SPU FIFO
integration; those are explicit reasons not to infer CD/audio acceleration from
the CPU percentage ([CD-ROM/audio subsystem overview](https://deepwiki.com/stenzek/duckstation/2.3-cd-rom-and-audio-subsystem)).

## PAL x2 / 100 Hz is not 900%

The local PAL audit gives the relevant arithmetic: stock PAL is 50 VBlank/s;
PAL x2 divides the CRTC period to 100 VBlank/s, while the guest audio arithmetic
still yields about 44,100 samples/s. The PAL title selector must also produce
`gSimVblankSkip=1`; otherwise a title receiving 100 interrupts can still update
gameplay at only about 50 updates/s. This is why the local hook explicitly says
it cannot make an unpatched PAL executable simulate twice per PAL VBlank
(`src/mods/wipeout3_framerate_100_plugin.c`) and why PAL100 remains opt-in.

In short, 900% and PAL x2 are orthogonal knobs. A PAL x2 run can be CPU-bound at
less than 100 guest updates/s even with the overclock configured. Conversely, a
900% CPU setting does not authenticate the PAL title’s 100-Hz selector and must
not be used as evidence of 100-Hz gameplay.

## 8 MB and title hacks

DuckStation documents 8 MB as 2 MB base RAM plus 6 MB expansion, and warns that
games need a larger heap or compatible modification and that memory mirrors can
break ([console settings source](https://github.com/stenzek/duckstation/blob/master/src/duckstation-qt/consolesettingswidget.cpp)).
That matches this repository’s package generator: the enhanced/full WipEout
variants are explicitly marked as needing 8 MB (`tools/gen_hueponik_framerate_mod.py`).

The 8 MB switch is consequently a memory-map/title-compatibility prerequisite,
not an overclock. The title hack selects code/data that can use the extra heap;
the CPU setting supplies the requested emulated clock behavior. They should be
treated as one tested profile, but not conflated into one timing multiplier.
The local static checks currently verify package ownership and requested values;
they do not establish that every 2 MB/8 MB and stock/enhanced executable pair
has identical behavior under 900%.

## Save states

DuckStation advertises save-state support, and its settings model includes
overclock state (`OverclockEnable`, numerator, denominator) as console state
([settings.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/settings.cpp)).
For this project, a save-state correctness claim needs more than “the state
loads”: the restored CPU fraction, RAM size/title variant, CRTC multiplier,
pending DMA/CD events, GPU FIFO state, SPU/CDDA/XA buffers, and host pacing mode
must be compatible with the state’s origin.

The current repository has a useful `savestate_status` receipt and focused load
gates, but `docs/audits/goosestation-parity.md` and `docs/HANDOFF.md` still
identify paired normal/accelerated traces as unfinished. The safe disposition is
to reject or clearly scope cross-profile loads (2 MB vs 8 MB, stock vs hacked
title, 100% vs 900%, PAL x1 vs PAL x2) until a checkpoint/restore comparison
proves CD/DMA/audio and title-state equivalence.

## Title hacks and DuckStation compatibility policy

DuckStation’s project description says its default configuration prioritizes
playability and discourages hacks, while still exposing per-game database
overrides and patches ([README](https://github.com/stenzek/duckstation/blob/master/README.md),
[game database](https://github.com/stenzek/duckstation/blob/master/src/core/game_database.cpp)).
That is materially different from this repo’s product contract: the WipEout
title patch, 8 MB feature, 900% setting, and PAL x2 plugin are deliberately
composed as a known profile. A DuckStation game-database entry that merely sets
CPU overclock cannot substitute for the WipEout executable/title transformation.

## Required acceptance evidence

Before calling the profile GooseStation/DuckStation-equivalent for WipEout 3 SE,
capture one deterministic normal-vs-profile run with:

1. exact title/disc hash and title-hack selector;
2. RAM size and effective CPU fraction (`9/1` for 900%);
3. CPU cycle/PC checkpoints and CRTC/VBlank counts;
4. CD command/sector/IRQ, DMA request/completion, and relevant RAM writes;
5. GPU FIFO/IRQ state and presentation swaps separately from VBlank;
6. SPU, XA/CDDA, host-consumed audio, underrun, and overflow deltas; and
7. save-state restore from a pre-card and pre-race checkpoint, with the same
   route continued in both branches.

Until that evidence exists, the correct claim is: **900% is configured and its
semantics are understood; WipEout’s full accelerated/profile equivalence is not
yet proven.**
