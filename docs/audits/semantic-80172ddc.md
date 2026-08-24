# Semantic audit: guest `0x80172DDC`

## Conclusion

`0x80172DDC` is a genuine ABI-callable, single-purpose transform-state adapter.
It prepares the GTE and scratchpad inputs consumed immediately afterward by
`BuildVisibleOrientedQuadList` (`0x80172E3C`).  The evidence supports the
descriptive symbol `PrepareVisibleQuadTransform` with `known` status.

The name describes observed behavior and its sole retained caller; it is not a
claim about the original source identifier.

## Boundary and caller

- Entry: `0x80172DDC`, with an eight-byte stack frame and saved `a0`/`ra`.
- Exit: `jr ra` at `0x80172E34`; its delay slot at `0x80172E38` performs the
  final scratchpad store.  The exact extent is therefore 96 bytes,
  `0x80172DDC..0x80172E3B`.
- The retained image contains one direct call, `jal 0x80172DDC` at
  `0x801416A4`.  Its caller is `BuildVisibleObjectQuadPackets`; immediately
  before the call it loads `a0 = group->field_44`.
- No direct jump/call or captured function-pointer reference into the middle
  of this extent was found.  `0x80172E3C` begins the next independent routine.

The effective ABI is:

```c
void PrepareVisibleQuadTransform(VisibleQuadTransform *transform); // a0
```

The caller ignores `v0`, and this routine does not intentionally set a return
value.  `a1` through `a3` are not inputs.

## Exact state changes

The routine performs these operations in order:

1. It calls `0x80136078` with `a0 = transform->field_44`. That helper follows
   nested `+0x44` links to the terminal record and installs a
   projection-adjusted render transform. It does not compose the chain.
   Consequently the GTE rotation matrix and translation registers on return
   describe the terminal transform selected by the input record.
2. It reads GTE control registers 5, 6, and 7 (`TRX`, `TRY`, and `TRZ`) and
   writes the same three words to control registers 13, 14, and 15 (`RBK`,
   `GBK`, and `BBK`).  These are not no-op writes: they copy the translation
   vector into the background-color vector used as the additive term by the
   following MVMVA operation.
3. It copies the three words at input offsets `+0x14`, `+0x18`, and `+0x1C` to
   scratchpad addresses `0x1F800000`, `0x1F800004`, and `0x1F800008`.

The routine does not project vertices itself and does not modify the installed
rotation matrix or `TRX/TRY/TRZ` after the nested setup call.

## How the consumer proves the purpose

`BuildVisibleOrientedQuadList` loads the three scratchpad words once at
`0x80172E84..0x80172E8C`.  For each source record it adds them to the record's
three coordinate words, range-checks the resulting components, halves them,
loads them into `IR1..IR3`, and executes GTE MVMVA at `0x80172F38`.  Because
the command uses the rotation matrix with the background vector, the earlier
`TR -> BK` copy supplies the nested transform's translation while the
scratchpad words supply the visible-object offset added to each source point.
The resulting vector is then subjected to the near-plane test and four RTPS
projections used to build the compact visible-quad list.

This producer/consumer relationship is why `PrepareVisibleQuadTransform` is
more precise than a generic `SetTransform` name: it bridges the engine's
hierarchical transform representation to this particular visibility path's
GTE-plus-scratch convention.

## Confidence and limits

- **High:** function boundary, sole direct caller, argument source, nested
  `+0x44` transform call, exact GTE register copies, exact scratch destinations,
  and consumption by `0x80172E3C`.
- **High:** this is setup rather than vertex projection or packet emission.
- **Medium:** the source-level type names `VisibleQuadTransform` and
  `field_44`; the underlying structure has not been fully recovered.
- **Unresolved:** the original identifier and the full semantics of every
  field in the hierarchical transform records.

## Primary evidence

- `generated/SCES_028.45_full_126.c`: sole call at `0x801416A4`
- `generated/SCES_028.45_full_141.c`: complete body
  `0x80172DDC..0x80172E38` and consumer setup/MVMVA at
  `0x80172E3C..0x80172F38`
- `generated/SCES_028.45_full_122.c`: nested transform installer at
  `0x80136078`
- `docs/audits/semantic-801415f4.md`: owning packet-builder audit
- `docs/audits/semantic-80172e3c.md`: visible-list consumer audit
