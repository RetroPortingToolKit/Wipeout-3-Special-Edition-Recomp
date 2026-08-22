# Wipeout 3 Special Edition  Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/total)](https://github.com/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/latest/total)](https://github.com/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp)](https://github.com/TechnicallyComputers/Wipeout-3-Special-Edition-Recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **Wipeout 3 Special Edition** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/mstan/recomp-ui).

Wipeout 3 is a racing game that retains the same basic elements of its predecessors, and introduces players to the F7200 Anti-Gravity Race League. Set in 2116, players control futuristic anti-gravity ships owned by racing corporations and pilot them on eight circuits (plus four hidden prototype tracks).

| | |
|---|---|
| Players | 2 |
| Region | Europe (PAL) |
| Publisher | Psygnosis |
| Year | 2000 |

## Running the game

Open the game executable. Its built-in launcher owns video, audio, input,
BIOS, disc, and accessibility settings and then starts the game. There is no
installer and no separate Generate & Build wizard in the player package.

On first launch, select your legally dumped PlayStation BIOS and your own
WipEout 3 Special Edition disc image. The launcher remembers external paths;
neither copyrighted input is copied into the application package.

## Legal

You must own the original game and provide your own disc image and PlayStation
BIOS dump. Disc images and retail BIOS dumps are not redistributed and must
never be committed.

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — RetComM-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Quick start (dev)

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

Zip prefix for CI artifacts: `wse`.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.
