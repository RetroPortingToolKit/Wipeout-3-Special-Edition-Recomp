# Project state

## Goal

Deliver WipEout 3 Special Edition as a normal modern-PC game produced by clean
static/region recompilation. Following one lawful setup from player-provided
verified media, it must launch directly and feel like a complete 4K, exact-100
Hz, ultrawide-capable remaster with the full defensible set of GooseStation-class
graphics improvements. It must not ship an interpreter, runtime compiler,
emulator wrapper, hidden fallback, copyrighted disc image, or retail BIOS.

## Architecture

Verified user media is patched and inventoried offline. Exact executable and
runtime-payload identities select precompiled bounded regions. Generated native
code runs against the independent `clean-aot` machine, device, rendering, audio,
input, media, and host layers. Title identities, addresses, enhancements, and
asset policy remain outside generic components. Unknown executable identities
and PCs fail closed.

The quarantined legacy lane is retained only for deterministic input routes,
behavior observations, and discovery receipts. Its code, tests, diagnoses, and
runtime fallbacks are not implementation or correctness authorities.

## Verified working state

- Exact user Redump media and pinned PAL patch identities are known and locally
  reproducible without committing copyrighted bytes.
- The prepared main PS-X EXE and observed runtime variants have exact hashes and
  bounded provenance receipts.
- Focused clean components exist for catalog, compiler, CPU, GTE, memory,
  machine timing, DMA/timers/interrupts, GPU, raster, CD-ROM/media, SPU, SIO,
  MDEC, rendering provenance, host presentation, and product setup.
- Recovery verification on 2026-08-21 passed the aggregate GCC 16.1 Release
  build and all 15 CTest targets.
- Python verification passed 118 tests and 584 subtests with one declared
  environment-dependent skip.
- A deterministic quarantined route reaches a live race. GooseStation behavior
  evidence demonstrates PAL-x2 presentation and 44.1-kHz audio, but it is not
  proof of the clean product.

## Current limitations

- No clean product executable boots the BIOS or executes the game.
- No complete reviewed game CFG, finite indirect-target closure, raw bounded
  payload compiler adapter, or complete executable-epoch catalog exists.
- Earlier adversarial audits rejected intermediate SIO, CD-ROM, host, and SoC
  snapshots. Later combined source and tests exist, but every finding must be
  reconciled against the current tree.
- Focused component tests do not establish whole-machine hardware equivalence.
- The exact product CPU-retirement multiplier, unique 100-Hz simulation-update
  proof, full-race audio/media timing, performance, and soak evidence remain open.
- Widescreen projection/culling, PGXP-quality vertex provenance, texture/font
  replacement, native rendering, and game fixes are not integrated end to end.

## Known policy and provenance

- HD texture and font redistribution/use permission is confirmed by the project
  owner. Before assets ship, add a durable permission receipt naming the creator,
  date, scope, permitted uses, attribution obligations, source identities,
  transforms, and final hashes.
- The hueponik PAL patch is a user-applied pinned transformation and is never
  redistributed with retail media.
- Ghidra output, captures, legacy traces, and external implementations are
  hypothesis and behavior evidence only; they do not define clean source.

## Active milestone: authenticated clean boot foundation

Objective: integrate the current clean tranches into one reviewed product state,
then execute authenticated code through the clean runtime on the path toward a
real BIOS-to-game boot.

Acceptance criteria:

1. Reconcile every known component-audit finding against current source.
2. Commit the shared clean-room integration without losing prior-agent work.
3. Pass aggregate GCC and Clang Release plus non-recovering ASan/UBSan tests.
4. Pass generic-source isolation and final link-map exclusion gates.
5. Complete authorized regions and finite targets for the first boot slice.
6. Add the bounded raw-image compiler adapter required by authenticated payloads.
7. Link one clean product executable with no interpreter or runtime compiler.
8. Execute authenticated guest instructions and fail closed on unknown code.
9. Preserve a deterministic BIOS-to-game route as the expanding acceptance test.

Do not trade correctness for early playability. Full 4K, exact 100 Hz,
ultrawide, audio/media correctness, GooseStation-class graphics, full content,
and modern-PC packaging remain required release milestones even when they are
outside the first integration checkpoint.

## Verification commands

```powershell
cmake -S clean-aot -B build-clean-aot -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-clean-aot --parallel 4
ctest --test-dir build-clean-aot --output-on-failure
python -m pytest -q tests clean-aot/catalog/tests
```

Also run the repository's Clang sanitizer, clean-room source scan, release-link
map, deterministic route, timing/audio, rendering, performance, and relocated
package gates as those product targets become available. A passing unit suite is
never sufficient evidence for a release claim.

## Important decisions

- Use the dual-lane strangler only during development; the legacy lane never
  enters the release.
- Preserve native device/media time independently from the fixed exact CPU
  retirement multiplier needed for PAL x2.
- BIOS-to-game boot is required; direct executable entry is not the finished
  product path.
- Hardware gaps can be staged but not permanently waived for release.
- Unknown code and media identities fail closed rather than falling back.

## Next likely milestone

After authenticated clean boot is stable, extend the deterministic route through
intro, menus, race loading, first race frame, full race, and then the complete
content matrix while integrating exact 100-Hz timing, production rendering,
ultrawide composition, PGXP-quality geometry, HD textures/fonts, audio, input,
saves, and modern packaging behind the same no-fallback acceptance gates.
