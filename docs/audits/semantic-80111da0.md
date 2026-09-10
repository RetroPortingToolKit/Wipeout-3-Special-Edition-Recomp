# Semantic audit: `0x80111DA0`

## Result

`0x80111DA0` is the confirmed bundle consumer
`RegisterTextureBundleEntries`. It consumes the relocated bundle produced by
`LoadPackedResourceBundle`, converts each entry into a texture descriptor,
appends every successful descriptor to the shared texture table, and releases
the bundle's backing allocation.

Its recovered ABI is:

```c
void RegisterTextureBundleEntries(RelocatedResourceBundle *bundle);
```

The relevant input layout is:

```c
struct RelocatedResourceBundle {
    uint32_t allocation_key; // +0, passed to the arena-release routine
    void *entries[];         // +4, global signed entry_count elements
};
```

The count is the signed halfword at `0x801A4B4C`, published by the immediately
upstream packed-bundle loader. It is not stored in the relocated bundle.

## Exact boundary and the pre-prologue

The exact interval is `[0x80111DA0,0x80111E84)`, or `0xE4` bytes, matching
the generated range. The preceding owner restores its frame and returns at
`0x80111D88..0x80111D9C`. The next independent routine begins at
`0x80111E84` by repeating the same global-count prelude for a different entry
converter.

This owner begins before its conventional frame setup:

```text
80111DA0  lui   v0,0x801A
80111DA4  lh    v1,0x4B4C(v0)
80111DA8  addiu sp,sp,-0x30
...
80111DB8  move  s5,v0
```

The first two instructions are not padding or the predecessor's tail. The
loaded `v1` controls the initial `blez` at `0x80111DD0`, and the same live
`v0 = 0x801A0000` is preserved in `s5` and reused to reload the loop bound.
Entering at the apparent stack prologue would leave both values undefined.
All 24 calls target `0x80111DA0`; an exhaustive aligned scan finds no call to
`0x80111DA8` or any other interior address.

The function's single epilogue restores `ra` and `s0..s6`, returns at
`0x80111E7C`, and restores the 48-byte frame in the delay slot at
`0x80111E80`. Generated labels at `0x80111DD8`, `0x80111DF0`,
`0x80111E00`, `0x80111E0C`, `0x80111E30`, `0x80111E48`, and
`0x80111E5C` are internal continuations retaining this frame and saved state.

## Complete caller census

The 24 direct callers are:

```text
800F0458 80104B14 80104D1C 80104D70 80104DBC 80104E08
80104E54 80104EA0 80104EEC 80104F38 80104F84 80104FD0
80105028 80105090 801050C8 80110A2C 8012284C 80131B78
80131DC4 80157824 801578B4 801578F8 80157974 801579FC
```

Every site obtains `v0` from a preceding `jal 0x80111AF8` and passes it as
`a0` in this call's delay slot. The independently audited
`LoadPackedResourceBundle` parses count/sizes, acquires and fills backing
storage, relocates its per-entry boundaries to pointers, and leaves the
relocated bundle result for this consumer. This uniform producer-consumer
pair proves the input's bundle role more strongly than any one call site.

The calls span menu/resource setup, race rendering, and scene setup. No caller
uses `v0` afterward, and the function establishes no public scalar result.

## Entry conversion and registration

If the signed global count is non-positive, the conversion loop is skipped.
Otherwise the routine walks `bundle->entries` at four-byte stride. For each
entry it calls:

```c
descriptor = func_80110DF0(bundle->entries[i], 1, 0);
```

`0x80110DF0` is an `0x8B0`-byte resource converter that allocates a
38-byte descriptor, parses image/texture fields, performs the downstream
image-transfer operations, and returns either that descriptor or null. The
fixed `(1,0)` arguments select this owner's conversion variant. This audit
does not assign source enum names to those flags.

Null results are ignored. For each non-null descriptor, the routine reads the
unsigned halfword count at `0x80173AD2`, stores the pointer at:

```text
0x801A4B90 + 4*old_count
```

and writes `old_count+1` back to `0x80173AD2`. There is no capacity check,
duplicate suppression, rollback, or signed-overflow guard in this owner.

The table's texture role is independently corroborated by
`EmitFontGlyphPacket`, which indexes `0x801A4B90` and reads CLUT/TPAGE fields
from the selected 38-byte descriptor. Thus “texture bundle entries” is a
behavioral identity grounded in both producer and consumer data flow.

## Bundle release

After the loop—or immediately when the signed entry count is non-positive—the
routine always calls:

```c
func_80116DBC(*(void **)0x8019862C, bundle->allocation_key);
```

The callee searches the arena's linked allocation records by the second
argument, coalesces/unlinks the matching record, and reports the not-found
path through its error helper. This is the release of the temporary packed
bundle backing storage after all successful per-entry descriptors have been
constructed. It also means `bundle` must be non-null even when the count is
zero or negative: the unconditional final load reads `bundle+0`.

There are exactly two callees in this owner: `0x80110DF0` in the loop and
`0x80116DBC` at completion.

## Compact behavioral equivalent

```c
void RegisterTextureBundleEntries(RelocatedResourceBundle *bundle) {
    int16_t n = *(int16_t *)0x801A4B4C;
    for (int i = 0; i < n; ++i) {
        void *descriptor = func_80110DF0(bundle->entries[i], 1, 0);
        if (descriptor != NULL) {
            uint16_t index = *(uint16_t *)0x80173AD2;
            *(void **)(0x801A4B90 + 4u * index) = descriptor;
            *(uint16_t *)0x80173AD2 = index + 1;
        }
    }
    func_80116DBC(*(void **)0x8019862C, bundle->allocation_key);
}
```

The loop's machine implementation uses a high-half increment and arithmetic
shift to maintain a signed integer index; the equivalent above expresses its
observed result for the signed-positive count domain.

## Evidence

- `disc/SCES_028.45`: authoritative instruction bytes, prelude ownership,
  complete direct-JAL census, and zero interior targets;
- `generated/SCES_028.45_full.ranges`: exact `0xE4` owner interval;
- `generated/SCES_028.45_full_28.c`: complete translated loop, registration,
  release call, and epilogue;
- `docs/audits/semantic-80111af8.md`: immediately upstream bundle producer;
- `docs/audits/semantic-80105cdc.md`: downstream consumer proving the global
  descriptor table's texture role.
