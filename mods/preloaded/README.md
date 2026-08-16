# Preloaded mods

Ship reviewed, default-disabled packages here:

```text
packages/<package-id>/<version>/
  manifest.toml
  …
```

Build wiring copies `mods/preloaded` next to the game executable as `mods/`.
Framework hardware mods (8 MB RAM, loading speed) are staged from psxrecomp
`mods/builtin` first; they appear on this title's Mods page with `game_id = "*"`.

`wipeout3.enhancement.framerate` (1.0.14) is converted from
[hueponik/wip3out-patches](https://codeberg.org/hueponik/wip3out-patches)
as a **plain** hueponik overlay plus AOT cadence: **SimVblankGate** entry and
VBlank plugins force **gSimVblankSkip=2**. Title `Game 60 fps` is CRTC; sim
should feel ~30 Hz. Enable **8 MB** only for Enhanced/Full.

`wipeout3.enhancement.framerate-120` is the experimental unlocked 120 Hz path
(conflicts with the package above).

Regenerate with `tools/gen_hueponik_framerate_mod.py` after a stock Track 01
dump is in `disc/`. See `psxrecomp/docs/MOD_PACKAGES.md`.
