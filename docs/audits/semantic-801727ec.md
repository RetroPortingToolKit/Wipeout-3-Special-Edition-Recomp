# Semantic audit: guest `0x801727EC`

## Conclusion

`0x801727EC` is not an ABI function and does not implement the previously
guessed `ProjectPackClipCodesQuad`.  It is the stack-frame prologue immediately
following the real entry at `0x801727E8`.  The owning routine reuses the shared
mesh-render setup, constructs projected midpoint geometry, and emits several
primitive packet variants.  Recommended symbols:

- function `0x801727E8`: `SubdivideAndSubmitMeshPrimitives` (`known`);
- block `0x801727EC`: `SubdivideMeshPrimitivePrologue` (`known`).

## Boundary and callers

The only direct call found is the `jal 0x801727E8` at `0x80142514`.  Its delay
slot supplies `a0 = s0`; the preceding instruction supplies `a1 = *s7`.
`0x801727E8` itself executes `move a3, zero` and falls directly into the stack
allocation at `0x801727EC`.  No direct `jal`, `j`, or captured function-pointer
reference to `0x801727EC` was found.  Treating the generated split at
`0x801727EC` as a source-level function therefore creates a false callable
boundary and loses the forced fourth argument.

The owner extends through the primitive handlers ending at `0x80172DD8`.
Those handlers jump to the common loop/epilogue at `0x801727A0` and
`0x801727AC`, shared with the preceding renderer.  `0x80172DDC` begins a
separate conventional function with its own frame and return.

## Effective ABI and shared setup

The entry makes a 44-byte frame, saves `ra`, sets `v0 = 20`, then calls
`0x80171BE8`.  That address is a deliberately shared renderer setup body: it
saves the complete callee-saved register set, captures `a0`, `a1`, `a2`, and
the `v0` selector into `s0..s3`, loads GTE/render state, and retains the return
continuation in `s4`.  For this caller, `a3` has been explicitly cleared by the
instruction at `0x801727E8`.

After setup returns, the routine combines the translation words at
`s4+0x20` with the transform-state words at offsets `0x14..0x1c` and stores the
result in the work record at `s7+4..+12`.  It also clears `s7+0`.  The routine
has no stable scalar return contract: its observable outputs are work/packet
records, ordering-table links, and the final packet-pointer publication through
the shared epilogue.

## Geometry and packet behavior

The body beginning at `0x80172840` reads packed signed XYZ vertices, adds the
translation, averages endpoint coordinates with arithmetic shifts, and feeds
the resulting midpoint vertices to GTE `RTPS`.  This is geometry subdivision,
not merely clip-mask packing.  The loop then uses projected screen/depth data,
clip tests, and primitive metadata to select handlers spanning
`0x80172A40..0x80172DB8`.

Those handlers copy colors/UV or other primitive attributes into the current
packet record, set a triangle/quad vertex count, preserve depth-derived tag
data, and invoke packet/ordering-table helpers in the `0x801425B8..0x80142634`
range.  Source records advance by type-dependent sizes (12, 20, 24, or 36
bytes), while the common loop decrements the primitive count and eventually
publishes the advanced packet pointer at `0x80198620`.

## Confidence and intentionally unresolved details

- **High:** `0x801727E8` is the ABI entry; `0x801727EC` is only its prologue;
  the owner uses the shared mesh renderer setup; midpoint generation, GTE
  projection, type-dispatched packet construction, and shared final epilogue.
- **Medium:** the source-level verb `Subdivide` and `Mesh` descriptor term.
  Both match the proven operations, but original names are unavailable.
- **Unresolved:** exact meanings of `a1`, incoming `a2`, every primitive type
  value, and each field of the 84-byte work/packet record.  They are not needed
  to reject the prior boundary and clip-packer identity.

## Primary evidence

- `generated/SCES_028.45_full_126.c`: sole call at `0x80142514`
- `generated/SCES_028.45_full_140.c`: entry/setup and subdivision body
  `0x801727E8..0x80172Axx`
- `generated/SCES_028.45_full_141.c`: primitive handlers through
  `0x80172DD8`, then independent function at `0x80172DDC`
- `docs/audits/semantic-80171afc.md`: shared renderer setup and epilogue
