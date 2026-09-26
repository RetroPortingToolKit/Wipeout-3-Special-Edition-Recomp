# Preloaded mods

Ship reviewed packages here (default-disabled unless noted):

```text
packages/<package-id>/<version>/
  manifest.toml
  …
```

The framework stages these into `<exe>/mods/bundled` beside its own
`mods/builtin` packages (8 MB RAM, CD speed, PGXP, ...), which target every game
with `game_id = "*"`. This title declines two builtins with
`EXCLUDE_BUILTIN_MODS` in `CMakeLists.txt`: host-paced fast loading, and the
generic file-picker bezel (`wipeout3.enhancement.bezel` replaces it with the
team-mark artwork).

`wipeout3.enhancement.framerate` (1.3.0, feature **NTSC Mode**) is converted
from [hueponik/wip3out-patches](https://codeberg.org/hueponik/wip3out-patches)
(pinned to 1a630b05). It ships one build: hueponik's plain `ntsc` (60 Hz,
stock 2 MB RAM), which his README recommends for an unmodified PlayStation
(30 fps instead of 25). The feature is **hidden** and **on by default**: no
options, no `[[requirement]]`. Every edit, including the boot EXE's, is one
disc overlay (1.0.14 also re-encoded the EXE edits as `main_exe` patches,
which made every plan fail its guards at `0x80010098`). The skip2 plugins
(**SimVblankGate** entry + VBlank, `gSimVblankSkip=2`) stay attached.

Measured (headless, guest time, GP1 display-flip ring; game speed from the
race timer vs guest time). It needs the framework fix that makes VBlank
follow the video mode (beads-eio.3.194). Stock: 25.0 fps at 50 Hz. NTSC Mode:
30.0 fps at 60 Hz (a new frame every 2 VBlanks), game speed 1.00. Without the
skip2 plugins it averages 30.7 fps, still at speed 1.00, but frames alternate
unevenly between 1 and 2 VBlanks. The plugins stay for even pacing.

1.2.0 shipped hueponik's 8 MB **full** builds (`ntscfull8` / `palfull8`, picked
by a Region option). They assume a heavily overclocked CPU; at the stock clock
they measured 18.9 / 19.3 fps (12 to 25 by scene, a new frame every 3 to 4
VBlanks), below the stock game, so 1.3.0 dropped them.

`wipeout3.enhancement.frame-interpolation` (**Frame Blending**) is
presentation-only: it blends the game's own frames (30 per second or fewer) up
to the display rate.

Regenerate the NTSC Mode package with `tools/gen_hueponik_framerate_mod.py`
after a stock Track 01 dump is in `disc/` (needs `xdelta3` on PATH and the
hueponik patch, downloaded on first run). Then update `aot/overlays.json`'s
`manifest_sha256`. See `psxrecomp/docs/MOD_PACKAGES.md`.
