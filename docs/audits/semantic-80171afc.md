# Semantic audit: guest `0x80171AFC`

## Conclusion

`0x80171AFC..0x801727E7` is the shared implementation of a mesh transform and
primitive submission routine.  It first establishes rendering state, transforms
the model's vertices into an 8-byte screen/depth/clip cache, rejects a wholly
off-screen model, then converts accepted mesh records into PSX GPU packets and
links them into the ordering table.

Recommended symbol: `TransformAndSubmitMeshPrimitives` (`known`).  This replaces
the provisional `ProjectPackClipCodesTri`: the routine handles both triangles
and quads and owns packet construction and OT insertion, not merely projection.

## Boundary and entries

The owning routine begins at `0x80171AFC` with a 44-byte stack frame and ends at
the `jr ra` / stack restore at `0x801727E0..0x801727E4`.  The apparent generated
functions at `0x80171BE8`, `0x80171C20`, `0x80171D04`, `0x80171D0C`,
`0x801727A0`, and `0x801727AC` are split continuations or internal helpers, not
separate source-level owners.

No direct `jal 0x80171AFC` was found.  Three small ABI-visible wrappers tail-jump
to the common body while preserving their caller's `ra`:

- `0x80171AD8`: sets `a2 = 0`, `v0 = 20`;
- `0x80171AE4`: derives an odd-valued `a2` from the incoming `a2` pointer and
  sets `v0 = 5`;
- `0x80171AF4`: sets `a2 = 0`, `v0 = 0`.

All three are called from the dispatch at `0x80142050..0x80142084`.  That caller
loads `a0` from its object list and supplies `a1 = 0`; its branch selects the
wrapper/rendering variant.  Consequently `0x80171AFC` is a real shared function
body, but not a normal four-argument public entry: it also consumes the selector
placed in `v0` by its wrapper.

## Effective calling convention

The setup at `0x80171BE8..0x80171C1F` preserves:

- `a0` as the model/object descriptor (`s0`);
- `a1` as a mode/value (`s1`; zero at the observed callers);
- `a2` as a wrapper-specific mode/value (`s2`);
- incoming `v0` as the ordering-table selector/bias (`s3`).

The descriptor fields proven by access rather than named speculatively are:

- `+0x10`: vertex-count-related byte;
- `+0x12`: primitive record count (halfword);
- `+0x18`: source vertex array;
- `+0x1c`: source primitive-record array;
- `+0x20`: pointer to rendering/transform state, whose `+0x20..+0x3c` words are
  loaded into GTE rotation/translation control registers.

The routine returns no meaningful scalar (`v0` is zeroed on the normal final
path).  Its outputs are transformed-cache writes, GPU packet writes, OT links,
and publication of the advanced packet pointer at `0x80198620`.

## Transform and clip phase

The internal setup at `0x80171C20` loads the object's GTE transform, selects an
ordering-table base through the table rooted at `0x8018AE88`, saves the current
packet pointer from `0x80198620`, and assigns the transformed-vertex cache base
`0x8018F0CC`.

The loop at `0x80171B1C..0x80171BDF` consumes 12-byte source vertices and writes
8-byte cache entries.  For each projected vertex it stores:

- packed `SXY` at `+0`;
- projected depth at `+4`;
- a four-plane clip mask at `+6`.

The mask bits are assembled from signed screen-coordinate tests against the
frame bounds loaded from `0x80173A9C` and `0x80173AA0`.  An accumulator starts at
`0x0f` and ANDs every vertex mask; a nonzero result at `0x80171D04` means every
vertex shares an outside plane, so the whole model exits without packet work.

## Primitive conversion and ordering table

When the aggregate reject mask is zero, `0x80171D0C..0x801727A7` iterates the
descriptor's primitive records.  The record's packed vertex indices select
8-byte entries in the transformed cache.  Per primitive, the code:

1. ANDs vertex clip masks for trivial reject;
2. uses GTE `NCLIP` for winding/backface rejection;
3. uses `AVSZ3` or `AVSZ4` for triangle/quad depth;
4. dispatches on the primitive type byte and writes the matching textured,
   shaded, triangle, or quad GPU packet layout;
5. splices the packet into the selected OT bucket, preserving the previous OT
   link in the packet tag and encoding the packet word count in the tag's high
   byte.

Accepted records advance both packet and source pointers.  Rejected records
advance only the source record/count.  The epilogue at `0x801727AC` publishes
the final packet pointer and restores the complete saved-register set.

## Confidence and unresolved detail

- **High:** exact owner boundary; three wrapper callers; vertex transform/cache
  layout; aggregate and per-primitive trivial reject; NCLIP/AVSZ use; triangle
  and quad packet construction; OT linking; final packet-pointer publication.
- **Medium:** `Mesh` as the game-level descriptor term and the precise meaning
  of each wrapper mode.  The instruction stream proves a model-like indexed
  primitive collection, but not the original source type name.
- **Intentionally unresolved:** semantic names for every descriptor field,
  primitive type value, flag bit, and the exact unit of the `v0` OT selector.

## Primary evidence

- `generated/SCES_028.45_full_140.c`: body and continuations
  `0x80171AFC..0x801727E7`
- `generated/SCES_028.45_full_126.c`: the three calls at
  `0x80142050..0x80142084`
- `generated/SCES_028.45_dispatch.c`: AOT split/continuation ownership
- `docs/audits/semantic-80171d0c.md`: detailed audit of the internal packet loop
