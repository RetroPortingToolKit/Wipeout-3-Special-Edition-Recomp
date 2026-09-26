# Preloaded mods

Ship reviewed, default-disabled packages here:

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

`wipeout3.enhancement.framerate` (1.2.0, shown as **NTSC / PAL Mode**) is
converted from [hueponik/wip3out-patches](https://codeberg.org/hueponik/wip3out-patches)
(pinned to 1a630b05). It ships only hueponik's two **full** builds, picked by
**Region**: NTSC (`ntscfull8`) or PAL (`palfull8`). Both need 8 MB main RAM,
which one unconditional `[[requirement]]` (package format 7) switches on
implicitly; the player never toggles 8 MB RAM. Every edit, including the boot
EXE's, is one disc overlay (1.0.14 also re-encoded the EXE edits as `main_exe`
patches, which made every plan fail its guards at `0x80010098`).

Measured (headless, guest time, GP1 display-flip ring): stock 25.0 fps; NTSC
18.9 and PAL 19.3 fps over a 175 s race, 12 to 25 by scene. hueponik's full
builds assume a heavily overclocked CPU; at the stock clock they are CPU-bound
(new frame every 3 to 4 VBlanks). The skip2 plugins (**SimVblankGate** entry +
VBlank, `gSimVblankSkip=2`) make no measurable difference: the builds hold
skip=2 themselves. NTSC also runs the race below real-time speed (about
fps/30): the runtime keeps 50 Hz VBlanks for this PAL disc even in the NTSC
video mode. PAL game speed is correct.

`wipeout3.enhancement.frame-interpolation` (**Frame Blending**) is
presentation-only: it blends the game's own frames (25 per second or fewer) up
to the display rate.

Regenerate the NTSC / PAL package with `tools/gen_hueponik_framerate_mod.py`
after a stock Track 01 dump is in `disc/` (needs `xdelta3` on PATH and the
hueponik patches, downloaded on first run). Then update `aot/overlays.json`'s
`manifest_sha256`. See `psxrecomp/docs/MOD_PACKAGES.md`.
