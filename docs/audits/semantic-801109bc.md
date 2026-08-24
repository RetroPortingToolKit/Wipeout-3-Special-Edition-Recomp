# Semantic audit: `0x801109BC`

## Result and logical owner

`0x801109BC` is the frontend model/resource loader now named
`LoadFrontendModelResource`. The true owner is
`[0x801109BC, 0x80110DF0)`, exactly `0x434` bytes. It opens a 64-byte frame,
saves `ra` and `s0..s6`, and has one epilogue at
`0x80110DC0..0x80110DEC`. `LoadFrontendTexture` begins independently at
`0x80110DF0`.

The range manifest repeats `R 801109BC 434` 21 times: once at the real entry
and after 20 `F` rows from `0x80110C4C` through `0x80110D7C`. Those later
addresses are the case arms and join block of the owner's packed-record type
switch, not callable owners. Some call conversion helpers; others only advance
the record cursor by a type-specific constant. Generated alias wrappers
re-enter the same host owner at those blocks. The complete bytes occur only
once in the retained EXE.

## ABI and returned selection

The recovered ABI is:

```c
void *LoadFrontendModelResource(
    void **resource_heads,
    int16_t *resource_count,
    uint32_t selected_byte_offset,
    uint32_t ignored_a3);
```

Original `a3` is never read. The third argument is used as a byte offset, not
scaled locally: it is published into the shared model context and the function
returns `*(void **)((uint8_t *)resource_heads + selected_byte_offset)`.

## Resource and model setup

The routine snapshots the current texture-descriptor count at `0x80173AD2`,
publishes that halfword at `0x8017ED24`, and initializes two resource-state
words at `0x801748AC/0x801748B0` to `-1`. It then performs this pipeline:

1. Acquire the current archive entry with policy zero, pass it through
   `LoadPackedResourceBundle` and `RegisterTextureBundleEntries`, and release
   all tracked temporary heap blocks. This registers the bundle's frontend
   textures before model construction.
2. Load a resource by the snapshotted texture index through
   `LoadResourceBySignedIndex(index, &size)`, append the returned pointer at
   `resource_heads[*resource_count]`, and increment the signed count.
3. Walk every nonnull head in the caller array and its `+0x24` linked chain.
   Store shared context `0x801CF7CC` at node `+0x20` and publish the caller's
   selected byte offset at context `+0x44`.
4. Initialize context halfword `+0` to `0x4000` and halfwords
   `+2/+4/+6/+0x0A/+0x0C/+0x0E` to zero. Field `+8` is not written here.
5. Reacquire the current archive entry with policy one, obtain its output size,
   and call the large private model builder at `0x800E5CA0` with archive data,
   size, the selected offset, fixed workspace `0x8017C564`, the saved texture
   base index, and variant `1`. Then release tracked temporary blocks again.

These data flows prove a texture-backed linked model/resource graph. “Frontend”
is supported by the sole caller and the immediately preceding sibling
`PrepareFrontendImageResource`; “model” is supported by the linked nodes,
vertex-X mutation, initialized transform-like context, and typed primitive
stream below.

## Optional mirrored-record conversion

If byte `0x801732B0` is zero, the routine skips all postprocessing. When it is
nonzero, it walks every resource head and linked node:

- byte `node+0x10` supplies a coordinate count, rounded upward to even; the
  signed X halfword of every coordinate at `node+0x18` is negated at an
  eight-byte stride;
- signed halfword `node+0x12` supplies the number of variable packed records
  beginning at `node+0x1C`;
- record type byte `1..23` selects a 23-row jump table at `0x8016D8BC`.
  Cases apply the confirmed `SwapRgb24RedBlue`, `ByteSwapTwoU16Values`,
  `SwapPackedRecordU16FieldsByType`, and `SwapType4Or8QuadUvPairs` helpers,
  plus two adjacent private converters, then advance by an exact size between
  12 and 56 bytes. Unknown types execute no converter or cursor advance before
  the loop counter proceeds.

The X negation and UV/vertex pair reordering preserve mirrored geometry and
winding, matching the same flag's independently proved horizontal TIM-pixel
reversal in `LoadFrontendTexture`. The operation mutates caller-owned model
and packed primitive data in place.

## Callers and interior targets

There is exactly one direct entry call, `0x801228C0`, in the retained frontend/
race transition owner. It passes the resource-head array, count pointer, and a
global selected byte offset, then saves this routine's returned selected head
for the following frontend setup call.

No J or JAL from outside the owner targets an interior instruction. All 20
generated translations are reached only through the owner's indirect record
type switch or its internal joins. They therefore add neither functions nor
unique callers.

## Sources checked

- `disc/SCES_028.45`, excluding its 0x800-byte PS-X EXE header, for exact
  disassembly, caller/interior census, body duplication, and jump table;
- `generated/SCES_028.45_full_27.c`, `.ranges`, and dispatch metadata for
  owner continuity and repeated translations;
- audits for `PrepareFrontendImageResource`, `LoadPackedResourceBundle`,
  `RegisterTextureBundleEntries`, `LoadResourceBySignedIndex`,
  `LoadFrontendTexture`, and the packed-record conversion helpers.
