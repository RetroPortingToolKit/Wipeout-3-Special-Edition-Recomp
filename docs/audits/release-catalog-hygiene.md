# Release catalog hygiene

The shipping catalog is direct-plugin only. Hueponik's published patch remains
an authenticated analysis oracle, but its derived disc-sector overlays,
guest-memory patch catalogs, replacement high-RAM menu engine, and Track 4x4
state are not release inputs.

The retired catalogs were:

- `wipeout3.enhancement.framerate/1.0.14`
- `wipeout3.enhancement.framerate-120/1.0.0`
- `wipeout3.enhancement.framerate-pal100/1.0.0`
- `wipeout3.enhancement.framerate-pal100/1.1.0`

Their 30/120 host helpers and `gen_hueponik_framerate_mod.py` were also
retired. They had no references from the current PAL 2.0 direct package.

Run this release gate from the project root:

```powershell
py -3 tools/verify_release_mod_hygiene.py
py -3 -m unittest tools/test_verify_release_mod_hygiene.py
```

The verifier fails closed on malformed manifests and rejects legacy catalog
paths, overlay or binary-delta payloads, manifest patch/overlay tables,
high-RAM guest addresses, Track 4x4 selectors, retired generators/plugins,
retired state selections, and `game.toml` recompiler/AOT patch catalogs.
