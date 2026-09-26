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

`wipeout3.enhancement.framerate` (1.1.0, shown as **NTSC / PAL Mode**) is
converted from [hueponik/wip3out-patches](https://codeberg.org/hueponik/wip3out-patches):
a **plain** hueponik overlay plus AOT cadence (**SimVblankGate** entry and VBlank
plugins force **gSimVblankSkip=2**). Two options pick one of hueponik's six
builds: **Region** (NTSC 60 Hz, about 30 fps in a race / PAL 50 Hz, about 25)
and **Extras** (None = stock 2 MB, Enhanced / Full = hueponik's 8 MB builds,
which need 8 MB Main RAM). Every edit, including the boot EXE's, is one disc
overlay; 1.0.14 also re-encoded the EXE edits as `main_exe` patches, which made
every plan fail its guards at boot (`mod plan ... rejected at 0x80010098`).

`wipeout3.enhancement.frame-interpolation` (**Frame Blending**) is
presentation-only: it blends the game's 30/25 fps frames up to the display rate
and pairs with NTSC / PAL Mode.

Regenerate the NTSC / PAL package with `tools/gen_hueponik_framerate_mod.py`
after a stock Track 01 dump is in `disc/` (needs `xdelta3` on PATH and the
hueponik patches, downloaded on first run). Then update `aot/overlays.json`'s
`manifest_sha256`. See `psxrecomp/docs/MOD_PACKAGES.md`.
