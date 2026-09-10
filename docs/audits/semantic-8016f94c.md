# Semantic audit: guest `0x8016F94C`

## Result

`0x8016F94C` is a real renderer entry reached indirectly from
`DispatchPackedFacePrimitive`.  The defensible name is:

```text
ProjectAndSubmitPackedFaceGroups
```

It walks one to eight consecutive 64-byte face records.  For each record it
projects the packed vertex pairs through `0x80170254`; a nonzero common clip
code rejects the whole record.  Otherwise it walks the record's 24-byte
primitive subrecords and invokes the callback selected by the dispatcher.
The callback, rather than this routine, constructs and links the GPU packet.

## Boundary and reference evidence

- The entry begins with a complete private stack-frame prologue at
  `0x8016F94C`: it saves `$ra`, the old `$fp`, `$s0..$s7`, and `$gp` in a
  52-byte frame based at scratchpad `0x1F8003CC`.
- Its epilogue at `0x8016FA2C..0x8016FA6C` publishes the two renderer cursors,
  restores that entire frame, and returns through the inherited `$ra`.
  Therefore the true extent is `0x8016F94C..0x8016FA6C` (292 bytes).
- `0x8016FA70` is a separately rooted sibling renderer; it is not fallthrough
  from this function.  The base range catalog's hole from
  `0x8016F18C..0x8016FA70` explains why this boundary only exists in the
  retained static-overlay evidence.
- `0x8016FC28` synthesizes `0x8016F94C` into `$t4` and tail-jumps through it for
  selectors 0, 1, 6, and 7.  No direct `jal` is expected: this is an indirect
  callback entry with the dispatcher's original return address.

## Private ABI

The tail dispatcher supplies:

```text
$a0 = section/mesh owner
$a1 = decoded low-10-bit face-record index
$a2 = decoded group count in 1..8
$ra = the original caller's return address
scratch 0x1F800000..0x0F = four primitive callback addresses
```

The entry saves `$a1` as the starting index and `$a2` as the outer-loop count,
then calls the shared setup body at `0x801701B4`.  That helper converts the
index to a 64-byte record address, loads the owner's transform coordinates,
the packed geometry base, the renderer context, and the current output
cursors.  Because those values are deliberately returned in private registers
and scratchpad, this is not a safe standalone o32 host ABI; `emit = false` is
required.

## Geometry and callback loop

For every 64-byte face record the routine:

1. reads the pair count at `record+0x34` and the primitive-subrecord count at
   `record+0x36`;
2. derives the end of the corresponding 24-byte subrecord run;
3. calls `ProjectPackedVertexPairs` (`0x80170254`), which expands packed
   vertex-index pairs into 32-byte projected records in scratchpad and returns
   their shared RIGHT|LEFT|BOTTOM|TOP clip mask;
4. skips all callbacks when that mask is nonzero (trivial rejection);
5. otherwise, for each 24-byte subrecord, selects a 252-byte material/texture
   half from a 504-byte bank using bytes `+0x0E` and `+0x0F`, prepares four
   projected-record pointers at scratch offsets `+0`, `+32`, `+16`, and `+48`,
   and performs `jalr` through the callback loaded from `0x1F800000`;
6. advances the projected scratch cursor by 32 bytes and the subrecord cursor
   by 24 bytes for the next callback.

After each outer iteration it advances the face pointer by 64 bytes.  On exit
it stores the updated renderer cursors to `0x801E5668` and `0x801E5664` before
restoring the private frame.  There are no GTE commands, GPU command-word
stores, packet-cursor increments, or ordering-table writes in this function
itself.  Those side effects belong to `0x801700C4`, `0x8016FD34`, or
`0x8016FEFC`, as selected by `DispatchPackedFacePrimitive`.

## Symbol decision

- Add `0x8016F94C` as `[[func]] ProjectAndSubmitPackedFaceGroups`.
- Mark it `known`: its boundary, dispatch references, nested-loop strides,
  shared-outcode reject, material-bank selection, callback invocation, and
  cursor publication are instruction-level facts.
- Keep `emit = false` because setup and callback calls exchange live state via
  private registers and scratchpad.

## Evidence inspected

- `generated/overlays_static.c`, retained fragment rooted at `0x8016F94C`,
  including the complete `0x8016F94C..0x8016FA6C` implementation and captured
  callees `0x801701B4` and `0x80170254`.
- `generated/SCES_028.45_full.ranges`, showing the base-image coverage hole and
  the separate root at `0x8016FA70`.
- `docs/audits/semantic-8016fc28.md`, for the packed descriptor, selector
  matrix, tail-transfer contract, and callback identities.
- `docs/audits/semantic-80170254.md` and
  `docs/audits/semantic-801700c4.md`, for the projection-record layout,
  outcodes, and downstream packet behavior.
