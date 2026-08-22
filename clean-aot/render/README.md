# Renderer-neutral geometry foundation

This directory is a standalone C++20 component for enhancement geometry. It has
no game-specific behavior or presentation policy and does not depend on a
renderer or another runtime.

The packet parser recognizes the PS1 triangle and quadrilateral polygon layouts,
including every raw/modulated and opaque/semitransparent flag combination. It
sign-extends the hardware's 11-bit native coordinates and preserves each full
UV word, including the CLUT and texture-page attribute halves.
`ProvenanceState` models generation-checked GTE screen registers and
word-addressed memory copies. Native positions stored in RAM are decoded from
the stored word rather than supplied separately. Full-word copies can carry a
live precise coordinate; overlapping writes and explicit GTE invalidation make
old storage stamps unusable. PS1 RAM addresses are canonicalized across the
KUSEG/KSEG0/KSEG1 views and all four 2 MiB mirrors before memory generations
are assigned. A CPU or DMA overwrite through any alias therefore invalidates
the same precise candidate. Addresses outside the architectural 8 MiB RAM
aperture retain their full identity, avoiding collisions with MMIO or generic
external packet storage.

`PolygonAssembler` splits a quad into the PS1 GPU's `(0, 1, 2)` and
`(1, 2, 3)` triangles. Each triangle is enhanced only when all three candidates
have finite precise coordinates and current storage provenance. A missing,
stale, or unvalidated candidate converts the entire triangle to native
fixed-point vertices with an explicit `NativeFallbackIdentity`; partial-
provenance triangles are not emitted.

`ProjectionPolicyPlanner` keeps composition policy outside packet and
provenance code. Its default plan is exactly neutral 4:3 geometry with native
hardware culling. Horizontal expansion requires an explicit target aspect and
an explicit choice between native culling and a matching expanded frustum;
the planner supplies only a generic clip-space scale and contains no title
addresses, track constants, or object exceptions.

`ReplacementManifest` indexes textures and fonts by the external pack's opaque
two-word key without assigning unverified semantic names to either word. Every
resolved identity binds the exact asset SHA-256, manifest generation, and
current raster-source generation. Reloading a manifest or modifying VRAM makes
cached replacement handles stale deterministically. Paths, dimensions, hashes,
duplicate keys, and malformed key encodings fail closed during bounded startup
construction; image decoding and title policy remain outside this component.

The interpolation functions implement the renderer-independent perspective
formula using clip-space `w`. Scaling all `w` values by a common finite factor
does not change the result while the scaled inputs remain finite and non-zero.
Zero, mixed-sign, cancellation-unstable, and non-finite inputs fail closed.
Combined UV/color interpolation is atomic: an invalid or partially supplied
attribute rejects the whole enhanced sample.

## Behavioral-reference boundary

Polygon packet facts were cross-checked against the PSX-SPX documentation at
commit `b791ca25b7833b356b28917383f231078762a75d`. That project explicitly says
its document is not clean-room and describes source-permission concerns. It was
therefore used only as a behavioral checklist: no implementation code or prose
was copied, and it is not treated as implementation provenance or a correctness
oracle. The code and adversarial tests in this directory were independently
written under the repository's clean-room policy.

Build and test from this directory:

```text
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```
