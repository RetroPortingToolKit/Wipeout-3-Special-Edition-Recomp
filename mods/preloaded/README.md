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
Install player `.psxmod` archives through the launcher Mods manager instead of
committing them here. See `psxrecomp/docs/MOD_PACKAGES.md`.
