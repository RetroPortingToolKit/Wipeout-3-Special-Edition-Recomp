# Preloaded mods

The release catalog contains only the title-owned PAL x2/100 Hz and widescreen
packages. The current WipEout profile in `mods/state.toml` also selects the
framework-owned 8 MB RAM, PGXP, Fast Loading, and Skip FMV packages.

```text
packages/<package-id>/<version>/
  manifest.toml
  …
```

Build wiring copies `mods/preloaded` next to the game executable as `mods/` and
stages framework packages from `psxrecomp/mods/builtin`. Retired NTSC/120 Hz,
legacy framerate, and CD-speed packages are not release inputs. The PAL x2
package authenticates the user-provided media and supplies only the runtime
host timing hook; it does not redistribute or approximate Hueponik's disc
transformation.
