# Semantic audit: guest `0x80171D0C`

## Conclusion

`0x80171D0C` is the start of the **primitive packet-build loop inside the
larger renderer entered at `0x80171AFC`**.  It is not an independently
callable PSX ABI function.  The generated-function boundary at `0x80171D0C`
is a recompiler seed/split boundary: execution reaches it from `0x80171D04`,
with all meaningful inputs already resident in saved registers.

Recommended local symbol: `SceneryPrimitivePacketBuildLoop` (high confidence
for packet build/submit; medium confidence for the narrower word "Scenery").

## Boundary and signature

There is no defensible C-callable signature for `0x80171D0C` itself.  Its
effective internal-loop inputs are:

- `s0`: current output GPU packet pointer (initially the word at
  `0x80198620`)
- `s1`: current source primitive-record pointer (loaded from owner/object
  offset `+0x1c`)
- `s2`: remaining primitive count (owner/object halfword `+0x12`)
- `s3`: ordering-table base selected through the table rooted at
  `0x8018AE88`, then biased by the caller-provided index
- `s6 = 250`: depth/near-threshold used by the per-vertex reject tests
- `fp`: transformed-vertex cache base (`0x8018F0CC` in the observed setup)
- `t7`: the source record's primitive type byte, loaded in the delay slot at
  `0x80171D08`

The enclosing `0x80171AFC` entry receives conventional arguments and performs
projection/clip-code generation before this loop.  Its setup preserves the
original `a0/a1/a2` as `s0/s1/s2`, but later repurposes them; consequently the
register meanings above, rather than an invented standalone prototype, are
the accurate signature for this region.

Outputs/side effects:

- emits one PSX GPU primitive packet at `s0` for each accepted source record;
- links the packet into an ordering-table bucket using the GTE depth result
  (`OTZ`, shifted/scaled, added to `s3`);
- advances `s0` by the emitted packet size, advances `s1` by the source-record
  size, and decrements `s2`;
- rejected primitives advance only the source side/count;
- the enclosing routine ultimately publishes/restores its accumulated state
  and returns through the epilogue beginning at `0x801727AC`.

## Rendering role

Evidence in the generated instructions is characteristic of a PSX primitive
converter, not a general clipper:

1. A packed index word in each source record is unpacked in 8-bit fields
   (`>> 5`, `>> 13`, `>> 21`, each masked with `0x7f8`) and added to `fp`.
   These select 8-byte transformed-vertex records.
2. Each selected record supplies a packed screen coordinate at `+0`, a depth
   halfword at `+4`, and a clip-code halfword at `+6`.
3. The loop ANDs all vertex clip codes.  A nonzero intersection rejects a
   primitive because all its vertices share at least one outside plane.
4. It runs GTE `NCLIP` (`cmd 0x06`) for winding/backface rejection and GTE
   `AVSZ3`/`AVSZ4` (`cmd 0x2d`/`0x2e`) for ordering-table depth.
5. Accepted branches write standard PSX packet layouts.  Observed packet
   lengths include 7/9/12 words for textured/shaded triangle/quad variants
   and 4/5 words for compact untextured variants; `s0` advances by the exact
   corresponding byte sizes (for example `0x20`, `0x28`, `0x34`).
6. The packet tag is spliced into an ordering-table entry: the old OT link is
   copied to packet offset `+2`, and the packet address is written back into
   the bucket with the packet word count encoded in its high byte.

The record byte at `s1+1` controls rendering flags.  The code tests bits 0 and
2 around winding/acceptance paths and folds bit 1 into the GPU opcode (for
example base opcode `0x34 | 2`).  Exact game-level meanings of every flag are
not proven here.

## Calls and control flow

`0x80171D0C..0x801727A7` contains **no `jal` subcalls**.  It is a leaf-like,
fully inlined dispatch/build loop using GTE commands and direct memory writes.
Its predecessor `0x80171D04` branches either to this loop or to the shared
epilogue at `0x801727AC`, depending on the accumulated trivial-reject result.
Loop branches return to the primitive-type dispatch while `s2` remains.

This also explains why treating generated `func_80171D0C(CPUState*)` as a
standalone host-call target is unsafe: entering it without the enclosing
routine's saved-register setup would dereference unrelated state.

## Confidence

- **High:** internal-loop rather than ABI function; source-record iteration;
  transformed-vertex lookup; shared-plane trivial reject; NCLIP winding test;
  AVSZ3/4 depth; PSX GPU packet emission and OT linking; no subcalls.
- **Medium:** `Scenery` qualifier.  Existing neighborhood symbols and prior
  runtime work use that label, but the instruction stream itself proves only
  a model/mesh primitive renderer.
- **Low / intentionally unresolved:** exact source structure names, all type
  byte values, and the semantic name of each flag bit.

## Primary evidence

- `generated/SCES_028.45_full_140.c`: generated blocks for
  `0x80171AFC..0x801727A7`
- `generated/SCES_028.45_dispatch.c`: the artificial AOT split entry and size
- `symbols.toml`: previous provisional name and neighboring projection labels

