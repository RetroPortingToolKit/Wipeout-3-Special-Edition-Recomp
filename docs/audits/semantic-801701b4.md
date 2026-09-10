# Semantic audit: guest `0x801701B4`

## Result

`0x801701B4` is a real renderer-private callable entry.  It prepares all of the
shared state needed to project an indexed packed-face record and then falls
straight into `ProjectPackedVertexPairs` at `0x80170254`.  The useful name is
`SetupAndProjectPackedVertexPairs`.

This is not an ordinary o32 leaf with a standalone return at `0x80170250`.
Instead, its semantic extent overlaps the independently callable suffix at
`0x80170254`: callers enter the setup at `0x801701B4`, execute the projection
body through `0x801704EC`, and return through that body's `jr ra`.

## Boundary and callers

- The preceding textured-quad routine returns with `jr ra` at `0x801701AC` and
  uses `0x801701B0` as its delay slot.
- The setup occupies exactly `0x801701B4..0x80170253` (`0xA0` bytes).  There is
  no branch or return at its end; `0x80170250` prepares `$s5`, followed by the
  first instruction of `ProjectPackedVertexPairs` at `0x80170254`.
- The retained base image contains exactly three direct calls: `0x8016F990`,
  `0x8016FAB4`, and `0x801707B8`.  They belong respectively to
  `ProjectAndSubmitPackedFaceGroups`, `SubdivideAndSubmitPackedFaceStrip`, and
  `SubdivideTwiceAndSubmitPackedFaceStrip`.
- No aligned address-bearing data word for `0x801701B4` was found.  The three
  direct calls are sufficient to establish its callable boundary.

The generated `R 801701B4 A0` range therefore describes only its unique setup
prefix.  A source-level reconstruction must preserve its deliberate
fallthrough into the separately rooted `0x80170254` body.

## Private input and output convention

The callers establish a renderer-specific register convention rather than an
independent C ABI.  In particular `$a0` is the owning object, `$s0` is the
packed-face index, and `$fp` refers to the caller's frame.  The setup publishes
long-lived values in saved registers for the projection and submission loops.
The eventual return value is the aggregate four-direction clip mask computed
by `ProjectPackedVertexPairs`.

## State established by the prefix

The instructions at `0x801701B4..0x80170250` perform these concrete operations:

1. Save the two current output cursor globals at `0x80173A9C` and `0x80173AA0`
   into caller-frame slots `$fp+44` and `$fp+48`.
2. Multiply the incoming face index by 64 and add the active render-context
   face-array base loaded through `[0x801985D4]+0x14`, yielding the selected
   64-byte face record in `$s0`.
3. Select a template-bank pointer from `0x8018AE88` using the signed global
   selector at `0x80175CB0`, then advance it by 60 bytes into `$s2`.
4. Load the owner's XYZ addends from `$a0+0x14/+0x18/+0x1C` into `$t0..$t2`.
5. Initialize the clipping workspace at `0x801E25D0` with words `2` and `15` at
   offsets 0 and 16, and retain `0x801E2724` in `$s5` for screen-bound tests.
6. Load the active geometry base and renderer-owned pointers from the context
   at `0x801985D4`, plus the global packet/ordering-table cursors at
   `0x801E5668` and `0x801E5664`.
7. Fall through into `ProjectPackedVertexPairs`, which consumes these registers
   and returns the common clip mask to the original caller.

This contract explains why all three packed-face renderers share the prefix:
they differ in how they subdivide or submit the projected pairs, but require
the same face record, transform addends, geometry tables, clip workspace, and
output cursors.

## Catalog action

Add a known, non-emitted `[[func]]` at `0x801701B4` named
`SetupAndProjectPackedVertexPairs`.  Keep `0x80170254` as the independently
callable `ProjectPackedVertexPairs` suffix; neither symbol should imply that
the setup prefix returns before the suffix.

## Evidence inspected

- `generated/SCES_028.45_full_140.c`: complete setup prefix, the three direct
  call sites, preceding return, and fallthrough into `0x80170254`.
- `generated/SCES_028.45_full.ranges`: `R 801701B4 A0` followed by
  `R 80170254 29C`.
- `docs/audits/semantic-8016f94c.md`, `semantic-8016fa70.md`, and
  `semantic-80170774.md`: independently recovered caller contracts.
- `docs/audits/semantic-80170254.md`: independently recovered projection-body
  boundary, behavior, and return value.
