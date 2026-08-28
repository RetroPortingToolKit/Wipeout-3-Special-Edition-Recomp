# Hueponik visual settings: direct-native implementation

Date: 2026-08-28

## Result

The complete authentic 8 MiB choice sets for `SHIP LOD`, `SHIP DIST`, and
`BIRDS` are now host-configured native features. Their defaults are the target
`MAX`, `65K`, and `HIGH`. The authenticated stock executable remains unchanged
in guest RAM and generated AOT.

The evidence and every source descriptor are decoded in
`hueponik-menu-complete-cleanroom.md`. This implementation uses that 8 MiB
contract because the release enables the framework's native 8 MiB RAM feature.
It does not use the narrower 2 MiB menu tables.

## Native boundaries

| feature | callback sites | continuation / result |
| --- | --- | --- |
| Ship LOD | `80130A20` | verifies the LUI and ORI, publishes the selected full threshold to `a0`, continues at `80130A28` |
| Ship Distance | `80130A0C`, `80152974` | verifies each adjacent LUI/ORI pair, publishes the selected primary square to `v0`, continues after both words |
| Ship Distance | `801308E4`, `801308FC` | exact signed `slti` against the selected radius |
| Ship Distance | `8013085C`, `8013086C` | constructs the selected `s1` square around the intervening stock instructions |
| Birds | `800FA224` | publishes the exact selected word result to `v0` |

The target 65K profile is exactly primary square `03FFC001`, signed radius
`1FFF`, and `s1` square `7FFFFFFF`. This is the authenticated 8 MiB profile,
not the 2 MiB `32K` profile (`00F42400`, radius `0FA0`).

Each two-word callback retires the skipped ORI's exact pipeline step before it
publishes the full register value. The authenticated next PCs `80130A14`,
`80130A28`, and `8015297C` are native continuation seeds, so returning from a
callback resumes at the original signed comparison instead of falling through
the clean-RAM interpreter. This preserves the two-instruction guest timing and
avoids adding interpreted work to the ship-rendering path. PGXP CPU/depth
tracking remains disabled for this release.

Birds `Stock` intentionally returns `00003000`, matching Hueponik's `ori
v0,zero,0x3000`; disabling the native feature is the only route that executes
the title's original load.

## Selection and exclusion

All authentic choices live in the launcher package catalog and persist in the
host mod state. No Start+Select drawing/input/card-persistence code is ported.
The packages are independently enabled, and the release state explicitly
selects Max, 65K, and High.

Track 4x4 has no package, callback, or replacement state. None of
`805FF7C4..805FF7D6` is referenced. Draw Distance is an independent native
five-choice selector at the stock-owner return `801422F4`; it defaults to Far.

## Widescreen billboard cleanup

The four type-4 billboard words formerly implemented as generated patches are
also native callbacks at `8013AD58`, `8013AD6C`, `8013AD70`, and `8013AD74`.
They read GTE FLAG, apply sibling mask `80460000`, and publish the existing
branch predicate. The plugin is activated by the widescreen feature only.

## Verification

Run:

```text
py -3 tools/verify_hueponik_menu_contract.py
py -3 tools/verify_wipeout3_hue_visual_presets.py
py -3 tools/verify_wipeout3_billboard_type4_cull.py
py -3 tools/verify_wipeout3_draw_distance_far.py
ctest --test-dir <build> -R "wipeout3-(hue-visual-presets|billboard-type4-cull|draw-distance)"
```

The static verifier rejects any remaining `[[recompiler.patch]]`, missing
site, wrong default/choice list, high-RAM dependency, guest code write, or
Track 4x4 address.
