# Audit archive

This directory is a point-in-time evidence archive. Its reports record what was
observed when each investigation ran; they may be stale and are not authoritative
for current project state or release decisions. Use the current
[handoff](../HANDOFF.md) for operational status and the
[widescreen release plan](../WIDESCREEN_RELEASE_PLAN.md) for release scope and
acceptance gates.

## Runtime, launch, and release evidence

- [BIOS, disc, and loading paths](boot-cd.md)
- [Feature-state persistence](feature-state-persistence.md)
- [Launch activation](launch-activation-audit.md)
- [PAL100 default gate](pal100-default-gate.md)
- [PAL100, 900%, and PGXP diagnostic](pal100-900-pgxp-diagnostic.md)
- [PGXP runtime effectiveness](pgxp-runtime-effectiveness.md)
- [PGXP save-state harness](pgxp-savestate-harness.md)
- [Release-build stall (2026-08-22)](release-build-stall-2026-08-22.md)
- [Release feature observability](release-feature-observability.md)
- [Release freshness (2026-08-22)](release-freshness-2026-08-22.md)
- [Runtime acceptance receipt (2026-08-22)](runtime-acceptance-receipt-2026-08-22.md)
- [Runtime reproducibility guards (2026-08-24)](runtime-repro-guards-2026-08-24.md)

## Rendering, timing, and emulator parity

- [FMV presentation](fmv-presentation.md)
- [Four-feature performance](four-feature-performance.md)
- [GooseStation 8 MB deep audit](goosestation-8mb-deep.md)
- [GooseStation CPU-overclock deep audit](goosestation-overclock-deep.md)
- [GooseStation PAL100 deep audit](goosestation-pal100-deep.md)
- [GooseStation parity](goosestation-parity.md)
- [GooseStation PGXP deep audit](goosestation-pgxp-deep.md)
- [GooseStation skip-engine applicability](goosestation-skip-engine.md)
- [Hardware parity](hardware-parity.md)
- [HD assets](hd-assets.md)
- [PAL100 pacing](pacing.md)
- [Renderer quality](renderer-quality.md)
- [Vulkan, PGXP, and HD parity](vulkan-pgxp-parity.md)

## Native execution and high RAM

- [Guest data map at `0x8016CC0C`](data-map-8016cc0c.md)
- [High-RAM guard at `0x80781474`](high-ram-80781474.md)
- [High-RAM validation at `0x807814D0`](high-ram-807814d0-validation.md)
- [High-RAM native dispatch](high-ram-native-dispatch.md)
- [Next native hot block (2026-08-22)](native-next-hot-block-2026-08-22.md)
- [Native performance](native-performance.md)

## Semantic reports

The `semantic-*.md` files are address-oriented reverse-engineering notes and are
intentionally not enumerated here. Discover them by filename with
`rg --files docs/audits | rg 'semantic-.*\.md$'`, narrow by guest address with a
filename filter such as `rg --files docs/audits | rg 'semantic-8016'`, or search
their contents with `rg '<term>' docs/audits/semantic-*.md`.
