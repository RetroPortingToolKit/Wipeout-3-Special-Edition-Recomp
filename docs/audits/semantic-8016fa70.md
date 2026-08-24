# Semantic audit: guest `0x8016FA70`

## Result

`0x8016FA70` is a real callable renderer entry with a complete save/restore
frame and return, despite being reached only by a private indirect tail jump.
The defensible name is:

```text
SubdivideAndSubmitPackedFaceStrip
```

For every selected 64-byte face record, it expands a run of packed vertex
pairs into projected scratch records. Each two adjacent pairs define one quad.
A visible quad is split at its four edge midpoints and center, producing four
child quads which are submitted through the two callbacks chosen by
`DispatchPackedFacePrimitive`.

## Boundary and reference evidence

- The function occupies `0x8016FA70..0x8016FC27` (`0x1B8` bytes). It creates
  a 52-byte frame in scratchpad at `0x1F8003CC`, saves `ra`, the inherited
  `fp`, `s0..s7`, and `gp`, then restores all of them and returns through
  `jr $ra` at `0x8016FC20` (delay slot `0x8016FC24`).
- The preceding image interval `0x8016F18C..0x8016FA6F` is not part of this
  entry. The explicit prologue and self-contained epilogue agree with the AOT
  function root and range.
- There is no direct `j`/`jal` and no stored aligned address word targeting
  `0x8016FA70` in the base image. Instead, `0x8016FC50..0x8016FC54` constructs
  the address in `$t5`; selectors 2, 4, and 5 of
  `DispatchPackedFacePrimitive` choose it as their tail target.
- The dispatcher leaves the original caller's `$ra` intact. Thus this entry
  returns directly to `SubmitSceneSectionFaces`, not to the dispatcher.

The entry is therefore callable but not through a normal public o32 call site.
`emit = false` remains necessary for a structured host lift.

## Private ABI and setup

The inherited input contract is:

```text
$a0  section owner / transform-bearing render object
$a1  low-10-bit face-record index
$a2  consecutive face-record count (1..8)
$at  0x1F800010, immediately after the four callback words
```

The entry copies `$a1/$a2` into `$s0/$s1` and calls the shared setup leaf at
`0x801701B4`. That leaf establishes the renderer context:

- converts the face index to a 64-byte record pointer;
- selects the current ordering table and loads the global packet cursors;
- loads XYZ addends from owner offsets `+0x14/+0x18/+0x1C`;
- loads the signed screen bounds used by projection helpers;
- resolves the 12-byte source-vertex array and the 24-byte packed-pair table;
- initializes the shared clipping workspace.

Calling these values a face record, source vertex, and pair table describes
their observed use; it does not assert recovered original typedef names.

## Face-record and packed-pair traversal

The outer loop processes `$a2` consecutive 64-byte records. For each record:

| Face-record field | Observed role |
| --- | --- |
| `u16 +0x34` | index into the 24-byte packed-pair table |
| `u16 +0x36` | number of pair-to-pair quads in the strip |

The index is multiplied by 24 and passed, with the count, to
`ProjectPackedVertexPairs` (`0x80170254`). That helper has an important
asymmetric traversal: it emits one initial projected vertex pair from the word
at `descriptor + 4`, then emits `count` more pairs from words at
`descriptor + 0` while stepping the descriptor by 24 bytes. Its result is
therefore `count + 1` adjacent pairs, occupying `32 * (count + 1)` scratch
bytes. This is stronger than describing the helper as producing merely two
records per iteration and explains the owner's exact topology.

Every packed word supplies two unsigned 16-bit vertex indices. The helper
resolves 12-byte signed-XYZ vertices, applies the owner addends, halves the
coordinates, projects with GTE `RTPS`, and stores two 16-byte projected vertex
records. It returns the AND of all screen outcodes. A nonzero result makes this
function skip the entire face record as a common-plane trivial reject.

## Quad subdivision and submission

For each of the `count` descriptors, the routine treats pair `i` and pair
`i+1` as four corners. The current pair begins at scratch `p`; the next pair is
at `p + 32`, so the corner records are at offsets `0,16,32,48`.

The four corner outcodes at record offset `+14` are ANDed first. A nonzero
common bit skips that quad. Otherwise `BuildProjectQuadMidpoints`
(`0x801704F0`) receives the corner order `{p+0,p+32,p+16,p+48}` and overwrites
a separate 80-byte workspace with four projected edge midpoints plus the
projected center.

The routine then issues four private callback calls, describing the four
sub-quads around that center:

| Child | Callback | Corner-source pattern |
| ---: | --- | --- |
| 0 | scratch word 0 | midpoint 0, original 0, original 1, center |
| 1 | scratch word 0 | original 0, midpoint 2, center, original 2 |
| 2 | scratch word 1 | original 1, center, midpoint 1, original 3 |
| 3 | scratch word 1 | center, original 2, original 3, midpoint 3 |

Here “scratch word 0/1” means the callback addresses at
`0x1F800000/0x1F800004`, installed by `DispatchPackedFacePrimitive` before its
tail jump. For selectors 2, 4, and 5 these resolve to combinations of
`0x801700C4`, `0x8016FD34`, and `0x8016FEFC`. The routine advances a material
or texture-template pointer by 12 bytes after each child, so the four calls use
four consecutive templates.

The template base is selected from a table rooted at `0x801E2724`: byte
`descriptor + 0x0E` selects a 504-byte group, and bit 2 of byte `+0x0F`
selects its second 252-byte half. These strides and selector effects are exact;
a source-level material or animation type is not yet proven.

After all quads and face records, the function publishes the updated packet
cursors to `0x801E5668` and `0x801E5664`, restores its private frame, and
returns. Packet formats and near clipping are delegated to the selected
callbacks rather than implemented here.

## Symbol decision

- Add `0x8016FA70` as `[[func]] SubdivideAndSubmitPackedFaceStrip`.
- Mark it `known`: boundary, selector references, record strides, adjacent-pair
  topology, subdivision geometry, callback calls, and cursor side effects are
  instruction-level facts.
- Keep `emit = false`: the function depends on a scratchpad callback table,
  inherited renderer registers, and tail-dispatch return ownership.

## Evidence inspected

- `disc/SCES_028.45`: full instruction stream and scans for direct calls and
  aligned address words.
- `generated/SCES_028.45_full_140.c`: entry, loops, helper calls, indirect
  callback invocations, cursor stores, and epilogue.
- `generated/SCES_028.45_full.ranges` and
  `generated/SCES_028.45_dispatch.c`: generated root, `0x1B8` range, and
  internal continuation ownership.
- `docs/audits/semantic-8016fc28.md`: packed dispatcher and callback matrix.
- `docs/audits/semantic-80170254.md` and
  `docs/audits/semantic-801704f0.md`: projected record and midpoint-helper
  contracts, checked against the underlying instructions for this audit.
