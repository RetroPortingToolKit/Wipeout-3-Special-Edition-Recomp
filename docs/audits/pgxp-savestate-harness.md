# Bounded PGXP save-state evidence harness

This focused gate consumes a retained runtime receipt; it does not build the
product or modify renderer/PGXP code:

```text
python tools/verify_pgxp_savestate_receipt.py <receipt.json>
python -m pytest tools/test_verify_pgxp_savestate_receipt.py
```

The receipt records configured and effective PGXP separately, requires all
effective geometry/texture/depth capabilities, verifies reset clearing, and
requires one committed `savestate_status.generation` advance. Each 4:3, 16:9,
and 21:9 case compares an equal deterministic continuation for geometry,
depth, texture, and culling. This proves the bounded receipt shape, not visual
parity beyond the captured route or untested backends.
