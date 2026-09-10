# Semantic audit: `0x80172E3C`

## Result

`0x80172E3C` is a genuine ABI-callable function, not an internal control-flow
label.  A defensible descriptive name is `BuildVisibleOrientedQuadList`.
It walks a strided array of world-space records, rejects disabled/near/outside
entries, constructs an oriented quad for every survivor, and writes a compact
visible list to scratchpad RAM.

The old `ProjectPackClipCodesFar` label was too narrow.  Projection and the AND
of four screen clip codes are only the visibility test; the function also
rotates the quad, records its four projected corners, depth and source index,
and compacts survivors.

## Boundary and callers

- Start: `0x80172E3C`, with a full non-leaf prologue saving `ra`, `fp`, `s0-s7`
  and `gp` at scratchpad `0x1F8003D4`.
- End: normal epilogue through `0x801731D0`; overflow tail-transfers to the
  separately emitted `0x801731D4` return epilogue.
- Generated extent: `0x398` bytes (`0x80172E3C..0x801731D3`).  The overflow
  epilogue is a separate `0x3C`-byte generated range at `0x801731D4`.
- The retained base image contains one direct `jal`, at `0x801416BC`.
- The caller supplies `a0 = *(0x801E247C)`, `a1 = 28`, `a2 =
  *(int16_t *)0x801D62CA`, and `a3 = -caller_record->halfword_0C`.
- Calls at `0x80172F50` and `0x80172F5C` target the scalar angle helpers
  `0x8015C3E8` and `0x8015C318`; this confirms the function is non-leaf.

## ABI

The observed signature is approximately:

```c
int BuildVisibleOrientedQuadList(
    const void *records,  // a0
    int stride,           // a1; caller passes 28
    int count,            // a2
    int view_angle_bias); // a3
```

Each input record uses:

- `+0,+4,+8`: three fixed-point world coordinates;
- `+12`: packed orientation/extent fields used after the center projection;
- `+16`, bit `0x8000`: skip/disabled flag.

The function snapshots GTE translation components from scratchpad
`0x1F800000..08`.  For each enabled input it adds those components to the
record position, converts to the GTE input scale, and runs an MVMVA-like GTE
command (`0x12`).  Entries with transformed Z below `125` are rejected before
quad construction.

Return values are:

- `0..47`: number of accepted output records;
- `-1`: a 48th accepted record hit the fixed-capacity sentinel.

The implementation initializes the accepted counter to `-48`, increments it
only after an accepted record, and returns `counter + 48`.  At exactly zero it
branches to the `-1` return block, proving a capacity of 48 rather than a clip
code return value.

## Projection and clipping behavior

For an entry past the near-depth gate, the function combines its packed fields
with `view_angle_bias`, calls the two angle helpers, and derives four rotated
corner offsets.  It installs each corner as a GTE translation and executes
RTPS, storing four SXY words at the current scratch output pointer:

```text
+0x00  projected corner 0 (packed SXY)
+0x04  projected corner 1 (packed SXY)
+0x08  projected corner 2 (packed SXY)
+0x0C  projected corner 3 (packed SXY)
+0x10  source index (halfword)
+0x12  projected depth / 32 (halfword)
```

The scratch output begins at `0x1F800000` and advances by 20 bytes only for a
survivor.  The source pointer advances by `stride` for every input record.

Each projected corner receives the standard four-bit screen outcode using
width `*(int32_t *)0x80173A9C` and height `*(int32_t *)0x80173AA0`:

```text
bit 0  x > width
bit 1  x < 0
bit 2  y > height
bit 3  y < 0
```

The four codes are ANDed.  A nonzero result trivially rejects the whole quad;
zero retains it.  Thus the return value is a compacted visible-record count,
not the aggregate clip mask.

## Confidence limits

The control flow, ABI registers, record stride, scratch output layout, near
threshold, clip bits and return contract are directly evidenced by the emitted
instructions.  The asset-level identity of the 28-byte records (for example,
whether the caller uses them specifically for scenery sprites, lights, or
another oriented-quad effect) is not established, so the symbol deliberately
avoids an asset-specific noun such as `Scenery` or `Billboard`.
