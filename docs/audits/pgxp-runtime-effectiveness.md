# PGXP runtime effectiveness receipt

`tools/verify_pgxp_runtime.py` is the focused boundary between the checked-in
configuration and runtime claims. It reports geometry correction, perspective
texturing, PGXP depth, CPU/data mode, culling, vertex-cache/reset state, and
renderer backend.

Run the static/configuration receipt with:

```text
python tools/verify_pgxp_runtime.py --manifest <staged>/mods/packages/psx.enhancement.pgxp/1.0.0/manifest.toml --output pgxp-runtime.json
```

Without `--runtime`, fields under `configured` and `culling` mean only that
the TOML loader input contains those values. The receipt intentionally marks
runtime effectiveness unknown. A built runtime may provide a JSON object with
keys matching the requested fields using `--runtime`; that is reported as
external evidence and is not authenticated by this tool.

Current checkout blocker: `psxrecomp` is an unpopulated submodule. Therefore
the config loader, launcher state application, PGXP implementation, vertex
cache/reset path, and OpenGL backend selection cannot be traced from this
checkout. The existing static release checks similarly prove staged config and
manifest defaults, not effective runtime state.
