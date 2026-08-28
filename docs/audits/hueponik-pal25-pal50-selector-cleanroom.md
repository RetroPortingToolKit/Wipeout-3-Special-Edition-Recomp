# Hueponik PAL 25/50 selector cleanroom contract

Date: 2026-08-28

## Result

Hueponik's `25HZ`, `50HZ`, and `100HZ` choices are three parameter values of
one timing graph. The 25/50 modes do **not** need Hueponik's menu, code island,
high-RAM engine, or Track 4x4 state. They are implemented through the same
direct authenticated hook surface as 100 Hz, parameterized by:

```text
s = selected menu index: 0, 1, or 2
p = (s >= 2)
gSimVblankSkip = (s < 1) + 1
```

The normal no-drop cadence is:

| selector | label | `p` | host/CRTC | `gSimVblankSkip` | normal scheduler rate | scheduler calls/s | temporal work gates/s | race cs/s |
| ---: | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 25HZ | 0 | PAL x1 / 50 Hz | 2 | 2 | 25 | 25 | 100 |
| 1 | 50HZ | 0 | PAL x1 / 50 Hz | 1 | 1 | 50 | 25 | 100 |
| 2 | 100HZ | 1 | PAL x2 / 100 Hz | 1 | 1 | 100 | 25 | 100 |

This resolves an apparent contradiction in the fixed-selector probe: the
title may render/update at 25, 50, or 100 calls per second while its original
25 Hz temporal-work gate remains 25/s. The visible race clock is separately
scaled to exactly 100 centiseconds per wall second in every selector.

Run the independent artifact verifier with:

```text
py -3 tools/verify_hueponik_frame_selectors.py
```

It authenticates the stock and Hueponik executables, pins 91 selector/timing
words and the four-entry coefficient table, and evaluates a one-wall-second
normal cadence for all three selectors.

Run the direct integration verifier and native semantic fixture with:

```text
py -3 tools/verify_pal100_direct_mod.py --root .
ctest --test-dir <build-dir> -R wipeout3-pal100-semantic --output-on-failure
```

The package exposes one restart-required `Frame Rate` choice with values
25/50/100 and an explicit default/preloaded value of 100. Three conditional
activation plugins select the mode at launch; one unconditional instruction
plugin owns the common authenticated title hooks. Activation clears phase,
race-clock, random/event, diagnostic, and high-rate record state before the
first guarded title callback atomically selects PAL x1/50 or PAL x2/100 host
cadence. No mode installs guest high-RAM code, AOT patches, or Track 4x4.

## Evidence inputs

- stock SCES-02845 executable SHA-256
  `0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`;
- Hueponik unified PAL executable SHA-256
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`;
- Hueponik repository commit
  `ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`; and
- the repository README at that commit, which explicitly defines selectable
  PAL 25/50/100, says 100 requires GooseStation PAL-x2 timing and a heavy CPU
  overclock, and says there is no automatic 100-to-50 fallback.

The resident selector publisher at `0x8017BB8C..0x8017BBAC` computes exactly:

```text
gSimVblankSkip = (s < 1) + 1
p               = !(s < 2)
```

The 8 MiB publisher performs the same stores at `0x8017BB70` and
`0x8017BB80`. Both are already independently checked by
`tools/verify_hueponik_menu_contract.py`.

## Complete per-selector formulas

`rate` is the stock unsigned halfword at `0x80173AF8`. Hueponik's patched
counter bounds constrain its supported timing path to 1 or 2. Rate 1 is the
normal single-VBlank path for 50/100; rate 2 compensates a two-VBlank interval
and is the normal path for selector 25.

For every scheduler call:

```text
mode_word       = p + 1
K               = (0x8000 >> p) << (rate - 1)
coefficient     = table[2*p + (rate - 1)]
scale(x)        = (x*K + 0x8000) >> 16
phase_mod       = 2 << p
loop_multiplier = p ? rate : 4
fixed_phase     = p ? 0 : rate
motion_shift    = 7 + p
special_mul     = 77 - 4*p
special_shift   = 2*rate + 10 - 2*p
```

The authenticated coefficient table at `0x801756C4` is:

```text
{ 0x1111, 0x1000, 0x0862, 0x081E }
```

Expanded values:

| selector branch | rate | `K` | coefficient | phase modulus | loop multiplier | fixed phase | motion shift | special multiplier | special shift |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 25/50 (`p=0`) | 1 | `00008000` | `1111` | 2 | 4 | 1 | 7 | 77 | 12 |
| 25/50 (`p=0`) | 2 | `00010000` | `1000` | 2 | 4 | 2 | 7 | 77 | 14 |
| 100 (`p=1`) | 1 | `00004000` | `0862` | 4 | 1 | 0 | 8 | 73 | 10 |
| 100 (`p=1`) | 2 | `00008000` | `081E` | 4 | 2 | 0 | 8 | 73 | 12 |

The table is indexed by `p`, not by the menu selector directly. Therefore
selectors 25 and 50 use the same coefficient family; their difference is the
published simulation skip and the resulting normal `rate`/call cadence.

## Phase, temporal gate, and race clock

Let `phase` be Hueponik's private scheduler phase and `phase_wrap` the raw
wrap result:

```text
total      = (phase & (phase_mod - 1)) + rate
phase_wrap = (total >= phase_mod)
phase      = total & (phase_mod - 1)

if p:
    work_gate = phase_wrap
else if rate < 2:
    work_gate = !phase_wrap
else:
    work_gate = 1
```

Normal sequences from a zero phase are:

- selector 25, rate 2: `work_gate = 1,1,1,...` for 25 calls/s;
- selector 50, rate 1: `work_gate = 1,0,1,0,...` for 25 gates/s; and
- selector 100, rate 1: `work_gate = 0,0,0,1,...` for 25 gates/s.

The published work gate, not raw `phase_wrap`, must drive the current direct
plugin's scheduler-gated owner sites. They are equal only for `p=1`. A simple
renaming of the current fixed-PAL100 `wrap` field would be incorrect for 25/50.

Race-clock delta has a separate accumulator:

```text
if p == 0:
    race_phase = 0
    race_tick  = rate
else:
    total      = race_phase + rate
    race_tick  = total >> 1
    race_phase = total & 1

race_delta_cs = 2 * race_tick
```

Consequently the normal modes publish:

- 25: 25 calls * 4 cs = 100 cs/s;
- 50: 50 calls * 2 cs = 100 cs/s; and
- 100: alternating 0/2 cs over 100 calls = 100 cs/s.

All 40 patched half-phase consumers that load private `0x56F4` are
semantically equivalent to loading the stock raw rate at `0x3AF8` when
`p=0`. Only 100 needs the native half-phase quotient. The separate private
`0x5434` consumer at stock site `0x800F1DA4` follows the same rule: raw rate
for 25/50, zero for 100.

## Shared high-rate path

At `0x80175604`, Hueponik tests `p` before the common mixer:

- `p=1`: both stock detour sites enter the private high-rate record/mixer;
- `p=0`, first site: execute the original `lw v1,0x3AFC(s3)` path; and
- `p=0`, second site: execute the original `lhu v1,0x128(s0)` path.

Therefore the native high-rate records and mixer are required only for 100.
Selectors 25 and 50 must execute the corresponding stock path at
`0x800F15DC` and `0x800F1D18`; calling the 100 Hz mixer in either p=0 mode is
not faithful.

## What remains stock-like at 50

The following behavior must remain stock-equivalent for the 50 selector:

- PAL x1 CRTC and 50 host VBlanks/s;
- independent real-time SPU, CDDA, XA/MDEC, and SFX device clocks;
- motion integration shift 7;
- raw-rate half-phase consumers;
- the original two shared-mixer paths; and
- raw rate at the fixed-phase consumer.

The 50 selector is **not** equivalent to disabling the direct mod. Hueponik's
unconditional frame arithmetic, counter-bound changes, scheduler scale and
coefficient graph, temporal work gate, and its 32-bit iteration increment are
still active. The direct implementation does not route `50` to untouched
retail code merely because several p=0 consumers reduce to stock semantics.

The iteration detour is a subtle example. For both p values it increments
`s4` as a 32-bit quantity and shifts the loop quantity left by `p`. At p=0
the shift is an identity, but Hueponik does not restore the stock signed-16
extension. That difference is normally invisible but is part of the exact
contract.

## Direct hook reuse

The direct PAL hook set implements one parameterized selector graph without a
guest overlay or high RAM:

- keep every unconditional Hue timing replacement active for all values;
- make the two initializer callbacks publish skip 2 for `25`, skip 1 for
  `50`/`100`;
- store native `selector` and derived `p` rather than reading or writing
  patched padding at `0x80175064`;
- split native scheduler state into raw `phase_wrap` and published
  `work_gate`;
- parameterize `K`, coefficient, phase modulus, race delta, loop multiplier,
  motion shift, and special constants using the formulas above;
- make the half-phase callbacks return raw `rate` for p=0;
- bypass the native high-rate mixer to exact stock continuations for p=0; and
- select host PAL x1/50 for 25/50, PAL x2/100 for 100.

No selector needs Hueponik's menu/input detours, memory-card format, private
`0x80174A00+` code/data island, `0x805F/0x8078` replacement engine, or Track
4x4 state.

## Package option design

Preserve the current package and feature identifiers so installed state keeps
working, and add one choice option:

```toml
[[option]]
feature = "framerate-pal100"
id = "rate"
label = "Frame Rate"
type = "choice"
default = "100"

[[option.choice]]
value = "25"
label = "25 Hz"

[[option.choice]]
value = "50"
label = "50 Hz"

[[option.choice]]
value = "100"
label = "100 Hz"
```

Use one unconditional common title-hook plugin plus three conditional mode
activation plugins selected with `when = { rate = "25" }`, and so on. The
preloaded state should explicitly store:

```toml
[feature.values]
rate = "100"
```

This keeps 100 as the release default without recreating Hueponik's in-game
menu. Start+Select remains the stock pause-menu input.

## Activation, transitions, and save states

The safest product contract is launch-time selection with restart required.
At cold activation, before the first title initializer:

1. derive `p` and `gSimVblankSkip` from the selected option;
2. clear native phase, half-phase, gate, random/event, diagnostic, and
   high-rate-record state exactly as Hueponik's initially zeroed private data;
3. configure PAL x1/50 or PAL x2/100 atomically with the selected mode; and
4. let the guarded title initializer callbacks publish the selected skip once.

Do not force `gSimVblankSkip` every VBlank. The title has temporary swap and
restore ownership, and periodic forcing was already shown to race it.

If a later UI supports a live 25/50/100 transition, a blanket reset would not
match Hueponik. Its menu preserves scheduler phase, random counter, and mixer
records, then the next p=0 call naturally clears race half-phase. Such a hot
transition must publish selector/skip and switch host pacing at one scheduler
boundary before the next VBlank. Until that atomic path is implemented and
tested, require restart rather than exposing a partial live change.

The direct plugin's native phase, race half-phase, random/event state, and
high-rate records replace guest RAM that ordinary savestates used to capture.
They must be included in the runtime's save/load snapshot contract (or loads
must be explicitly rejected). Resetting them independently on load can shift
the race clock, temporal gate, or high-rate mixer even when guest RAM restores
correctly.

## Host/device boundary and risks

The authenticated title artifacts prove only the title formulas. They do not
implement a host clock switch. Product integration must pair:

- selector 25/50 with CRTC multiplier 1 and native 50 Hz;
- selector 100 with CRTC multiplier 2 and native 100 Hz; and
- every selector with unchanged real-time audio/CD/XA device clocks.

The Hueponik README explicitly says there is no automatic fallback. A 100
selector running on a 50 Hz host is expected to be two times slow; a 50
selector must not be inferred from `gSimVblankSkip == 1` because 100 publishes
the same skip. Host mode must follow the selected value/derived `p` directly.

FMV is outside the selector scheduler while MDEC/XA playback is active. Any
temporary host-pacing policy for FMV is runtime integration, not a title
formula recovered here, and needs a separate real-time FMV/CD/XA acceptance
test. It must never rewrite the selected title mode based on an FPS counter.

Primary integration risks are:

- conflating raw phase wrap with p=0 published work gate;
- sending 25/50 through the high-rate mixer;
- treating 50 as disabled/retail timing;
- using skip value 1 to distinguish 50 from 100;
- changing CRTC rate without the title selector at the same boundary;
- failing to snapshot native timing state; and
- validating only rendered FPS instead of race clock versus wall time.
