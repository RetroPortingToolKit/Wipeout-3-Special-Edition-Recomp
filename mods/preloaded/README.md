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

The shipping WipEout catalog uses trusted native callbacks against the
authenticated stock `SCES-02845` executable. `framerate-pal100` provides the
25/50/100 selector; the title-side 8 MB handshake, widescreen, draw distance,
ship LOD, ship distance, birds, and startup-media skip are separate direct
features. Their defaults form the reviewed 8 MB / PAL100 profile.

Do not regenerate or ship a Hueponik xdelta, disc-sector overlay, AOT patch
manifest, replacement executable, high-RAM menu engine, or Track 4x4 state as
part of this catalog. The published patch is an analysis oracle only; direct
packages must resolve to plugins with no disc overlays, derived discs, or
guest-text writes. See `psxrecomp/docs/MOD_PACKAGES.md` for the manifest format.

Before staging a release, run `py -3 tools/verify_release_mod_hygiene.py`.
