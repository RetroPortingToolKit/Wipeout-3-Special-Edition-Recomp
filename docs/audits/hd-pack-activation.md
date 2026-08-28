# HD pack and font activation audit

## Verdict

The clean direct-stock probe selected the intended 292-entry HD texture pack.
All 20 font texture/palette keys required by the runtime are present,
unambiguous, and actively substituted. This includes the four main Helvetica
keys: the 256x32 atlas (`861ad9f1`) and 256x36 atlas (`d20dee94`), each under
palette hashes `273d3b40` and `5888b400`. Their replacement files are exact
16x atlases (4096x512 and 4096x576 respectively), so each texture hash maps to
the correct source geometry while its palette hash preserves the intended
colour.

The previous `exclude.txt` entries were not evidence of bad Helvetica assets.
They were a broad workaround added during investigation of doubled edges in a
different, two-pass F500 draw. Repository commit `71e9e48` had already restored
these unchanged Helvetica replacements after the fully-HD Aug-18 render was
accepted. Suppressing either texture hash makes all text using that atlas fall
back to the original bitmap, so the pack now has no font exclusions.

## Reproducible receipt

The isolated run at `artifacts/candidate-probe/20260828-012014-930` recorded:

```text
[tex_pack] legacy root indexed: files=292 unique=292 ambiguous=0 mappings=390 fonts=complete
[tex_pack] replace=1 dump=0 pack=...\hd_textures\WipEout 3 - Special Edition (Europe) (En,Fr,De,Es,It)-texture-replacements (292 entries) ...
```

Its `run.json` binds `--memcard-dir` to that receipt's new `isolated-state`
directory and records the repository as the process working directory. Verify
the static inventory, defaults, runtime selection, and isolated-state
provenance together with:

```powershell
python tools/verify_wipeout3_hd_pack.py `
  --runtime-log artifacts/candidate-probe/20260828-012014-930/runtime.stdout.log `
  --run-json artifacts/candidate-probe/20260828-012014-930/run.json
```

The expected result is `PASS`, 292 unique PNG keys, 20/20 required font keys
present and active, 4/4 Helvetica keys active, and zero excluded font keys.
Font exclusions now fail closed even without `--require-all-fonts-active`; the
flag remains accepted for compatibility with existing invocations.

## Why HD text previously disappeared

There were two distinct failure modes:

1. Before commit `fccb017`, the pack root was not pinned. Launching a disc from
   another directory made the runtime search beside that disc and silently
   index zero entries, reverting textures and fonts together. The current
   portable default is `hd_texture_dir = "hd_textures"` and the runtime receipt
   proves it resolves to this repository's pack.
2. Font atlases are boot-time uploads. Before the upload tracker was integrated
   with savestates, restoring a state repopulated VRAM without rebuilding those
   tracked uploads, so their HD replacements disappeared until a re-upload.
   A fresh isolated-state boot avoids that stale-state ambiguity.

## Visual check

The three nominal captures in the isolated probe are not game evidence: they
captured the foreground web browser instead of the candidate window. They must
not be used to accept font rendering. The two user captures from the preceding
test session visibly show intact F500 menu glyphs, including `frame rate`,
`ship lod`, and `track 4x4`; they prove a prior build rendered the F500 face but
do not substitute for a correctly targeted capture from this exact candidate.

The next visual probe must capture by the candidate window handle (or first
bring that exact PID's top-level window to the foreground) and include both an
F500-heavy hack/menu frame and a Helvetica-heavy stock menu frame.

## Configuration invariant

`game.toml` enables HD substitution and pins `hd_texture_dir = "hd_textures"`.
The pack-local `exclude.txt` must contain no font texture hash. The static
verifier checks both the 20-key inventory and this active-substitution policy,
and separately checks the four Helvetica replacements against the dimensions
of their keyed atlas.
