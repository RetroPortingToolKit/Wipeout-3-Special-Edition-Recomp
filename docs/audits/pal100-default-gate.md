# PAL x2 / 100 Hz default gate

Date: 2026-08-22  
Disposition: **auto-enabled by release requirement; performance remains unproven**

## Decision

The release requirement is automatic PAL x2/100 Hz selection. This is a
configuration and launch-policy decision, not a claim that every race sustains
100 Hz. Retained runtime evidence still shows unresolved presentation/audio
and equivalence failures, so the release must preserve the safety gates below
and must not advertise proven race performance.

This resolves the apparent contradiction by separating two meanings of
“default”:

| Check | Meaning | Required value now |
| --- | --- | --- |
| `tools/verify_pal100_title.py` | The authenticated PAL media/package contains the title-side selector-2 contract needed by PAL x2 | `default_enabled = true` is required by this title-authentication check |
| `tools/verify_static_release.ps1` | The staged release carries the required automatic selection | Manifest `default_enabled = true` and PAL100 enabled in state |
| `mods/state.toml` in a portable release | The launcher’s effective user selection | Must be recorded; it must not be inferred from source or the title verifier |

The title verifier authenticates the media/title contract. The static verifier
checks that the release actually carries the requested default in both package
metadata and launcher state. Neither verifier proves sustained race timing.

## End-to-end trace

1. Launcher/mod state: the launcher owns staged `mods/state.toml`; the source
   tree does not. `tools/verify_one_folder_release.ps1` requires the PAL
   package to be enabled in that state for a selected-release receipt.
2. Package selection: the PAL package targets `SCES-02845` and pins the
   authenticated Track 01 and executable hashes. It conflicts with the stock
   and NTSC-x2 framerate packages.
3. Plugin initialization: the package selects
   `wipeout3.framerate.pal-x2`, registered by
   `src/mods/wipeout3_framerate_100_plugin.c`. Activation requests native 100
   Hz and CRTC multiplier 2; it does not patch title simulation state.
4. Authenticated title selector: `tools/verify_pal100_title.py` checks the
   exact media/executable hashes and selector words. Selector 2 stores
   `gSimVblankSkip = 1` and PAL-x2 state 1; this is necessary for 100 Hz game
   simulation, but the verifier is not a runtime cadence or race test.
5. BIOS boot: the runtime defers the native period and CRTC multiplier until a
   programmed PAL game CRTC is observed, so BIOS startup is not accelerated.
6. Menu and race: the retained corrected input route reaches the memory-card
   UI, main menu, vehicle/ship selection, track load, and an on-track race.
   The normal route has roughly 99–101 Hz guest cadence in the captured race,
   but cumulative bridge underruns remain and the full normal-vs-accelerated
   equivalence proof is incomplete.

## Required default-on gates

Change the package to default-on only after a release receipt contains all of
the following, using the same authenticated title, BIOS, renderer, and
settings:

1. `verify_pal100_title.py --json` passes, and the staged package and launcher
   state select the same PAL target without any conflicting framerate package.
2. BIOS boot and title/menu transition remain at stock PAL cadence until the
   PAL CRTC is programmed; no early multiplier activation occurs.
3. A real-time 100 Hz route reaches and sustains the title/menu and race path
   with no pacer debt or material guest-rate collapse.
4. SPU/CDDA production remains at 44.1 kHz with zero underruns/overflows over
   boot, FMV, menu, media load, and a deterministic race soak. PAL x2 must not
   turn the audio clock into 88.2 kHz.
5. FMV presentation is separately measured at its authored PAL cadence while
   guest pacing remains 100 Hz; display-area flip counters are not accepted as
   swap-rate evidence.
6. Paired normal and PAL-x2 traces agree at the required startup-card and
   race-load gates: CD sector/IRQ/DMA ordering, card writes, state
   fingerprints, and a post-load continuation.
7. The result is reproduced in the staged one-folder release, with
   `tools/verify_static_release.ps1` updated in the same change to assert the
   now-proven policy and with the receipt retained.

Until every gate passes, the safe and internally consistent configuration is:

```toml
default_enabled = true
```

The title verifier’s default-on assertion must remain unchanged because it
checks package/title authentication, not effective launcher activation.
