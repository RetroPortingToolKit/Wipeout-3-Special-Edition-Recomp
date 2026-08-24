# Semantic audit: `0x80111E84`

## Result

`0x80111E84` is the texture-bundle consumer now named
`RegisterSimpleTextureBundleEntries`. It is a close companion of the audited
`RegisterTextureBundleEntries` at `0x80111DA0`, but converts entries through
the smaller TIM loader at `0x801116A0`.

```c
void RegisterSimpleTextureBundleEntries(RelocatedResourceBundle *bundle);
```

It consumes the relocated bundle, appends every successfully created texture
descriptor to the shared descriptor table, and frees the bundle's backing
allocation.

## Exact boundary and ABI

The entry deliberately begins before its conventional stack prologue:

```text
80111E84  lui   v0,0x801A
80111E88  lh    v1,0x4B4C(v0)
80111E8C  addiu sp,sp,-0x30
```

The preloaded count controls the first `blez`, and the live `v0` is retained
for later count reloads. The sole epilogue restores `ra/s0..s6` and returns at
`0x80111F5C`, restoring the 48-byte frame at `0x80111F60`. The next independent
entry starts at `0x80111F64` with the same kind of prelude for another bundle
variant. Thus the exact owner is `[0x80111E84,0x80111F64)`, `0xE0` bytes,
matching generated metadata.

Only `a0` is consumed. Callers ignore `v0`, and no path establishes a semantic
return value. `bundle` must be nonnull because `bundle+0` is read on the
unconditional final release path even when the count is non-positive.

## Bundle format and loop

The input layout is shared with `RegisterTextureBundleEntries`:

```c
struct RelocatedResourceBundle {
    void *allocation_key; // +0
    void *entries[];      // +4, global signed count elements
};
```

The signed halfword at `0x801A4B4C` supplies the entry count. A non-positive
count skips conversion. Otherwise the routine walks `bundle+4` at four-byte
stride and calls:

```c
descriptor = func_801116A0(bundle->entries[i], 1);
```

The loop reloads the signed global count each iteration; mutating that global
during conversion would change the bound. The machine loop maintains its
index in the high half of a word and recovers it by arithmetic shift, but its
positive-count result is an ordinary signed index loop.

## Texture conversion and registration

Direct disassembly of `0x801116A0..0x80111AF7` proves that it is a smaller TIM
texture converter:

- it accepts standard TIM modes 0 (4-bit indexed) and 1 (8-bit indexed), and
  returns null for other low-three-bit modes;
- it allocates a `0x26`-byte descriptor through `AllocateHeapBlock`;
- it auto-places supported CLUT/image rectangles, derives TPAGE/CLUT and UV
  fields, and uploads rectangles through `0x800EEC80`;
- its second argument is truncated to signed 16 bits and contributes the ABR
  bits (`argument << 5`) in the descriptor TPAGE; this owner always passes 1.

For every nonnull result, the owner reads unsigned descriptor count
`0x80173AD2`, writes the pointer to `0x801A4B90 + 4*count`, and increments the
halfword count. Null conversions are skipped. There is no capacity check,
duplicate suppression, or rollback.

The name says `Simple` only to distinguish this two-argument, `0x458`-byte
converter from the feature-richer `LoadFrontendTexture` path used by the
neighboring registrar; it does not claim that the resource itself is simple.

## Bundle release

After conversion, including when the count was initially non-positive, the
routine calls:

```c
FreeHeapBlock(*(HeapArena **)0x8019862C, bundle->allocation_key);
```

This releases/coalesces the temporary allocation holding the relocated bundle.
Descriptor allocations survive because each is separately owned by the
texture heap/table. The routine has exactly two direct callees:
`0x801116A0` in the loop and `FreeHeapBlock` at completion.

## Callers and interior targets

An exhaustive aligned scan of the retained EXE finds exactly eight direct
calls:

```text
8010507C 8011C938 8011C988 8011C9D8
8011CA28 80131BC4 80131CD0 801577F4
```

Every caller immediately follows `LoadPackedResourceBundle` at `0x80111AF8`
and forwards its result in `a0`, proving the producer/consumer ownership chain.
No external direct `j` or `jal` targets any instruction inside
`0x80111E88..0x80111F63`; all eight calls use the true pre-prologue entry.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for exact
  instructions and exhaustive caller/interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0xE0 boundary;
- `docs/audits/semantic-80111af8.md`, `semantic-80111da0.md`, and
  `semantic-80116d1c-80116dbc.md` for the producer, paired registrar, and
  release primitive;
- direct disassembly of converter `0x801116A0` and all eight caller windows.
