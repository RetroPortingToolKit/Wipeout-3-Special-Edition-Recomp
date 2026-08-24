# Semantic audit: guest `0x80141D6C`

## Result

`0x80141D6C..0x80141E5B` is one genuine callable function. Its retained role is
to allocate and initialize the descriptor for the race shadow texture atlas and
upload the atlas' embedded pixels to a fixed VRAM rectangle. The conservative
descriptive name is `InitializeShadowTextureAtlas`.

The apparent source-path parameter is dead in this build. The sole caller passes
`kShadsBt4SourcePath` in `$a0`, but the callee neither reads nor preserves that
register before replacing it with the arena pointer used by the allocator. This
is consistent with a legacy file-loading interface whose retained build embeds
the actual texture payload; it is not evidence that the path is opened here.

## Boundary and callers

The generated range is exactly `R 80141D6C F0`. The entry creates a 40-byte
frame, saves `$s0`, `$s1`, and `$ra`, and the return at `0x80141E54` restores
that frame. There are no internal alternate entries. The dispatch table owns
the entry plus resumptions at `0x80141D94` and `0x80141E44`.

There is exactly one retained direct call, in `InitializeRaceRuntimeResources`:

```text
80124928  lui    a0, 0x8017
8012492C  addiu  a0, a0, -0x1E90   # 0x8016E170, "...\\shads.bt4"
80124930  jal    0x80141D6C
80124934  addiu  a1, zero, 1       # delay slot
```

No retained indirect target table points at this address. The caller publishes
the returned pointer at `0x801879E0` and has no other use for `$v0`.

## ABI and dataflow

The most faithful recovered prototype is:

```c
ShadowTextureDescriptor *InitializeShadowTextureAtlas(
    const char *unused_source_path,
    int16_t mode);
```

The exact descriptor type name remains descriptive. At entry, `$a1` is
sign-extended through `$s1`; `$a0` is overwritten at `0x80141D80` before any
use. The function calls `0x8011686C(arena_at_0x8018F098, 38)` and retains its
return in `$s0`. It initializes halfwords in that 38-byte allocation, including:

- `+0x00 = 0` and `+0x02 = (mode << 5) | 0x1C`;
- atlas coordinates/dimensions `+0x0E = 800`, `+0x10 = 256`,
  `+0x12 = 32`, and `+0x14 = 112`;
- paired bounds/UV-like values at `+0x16..+0x24`: `128, 0, 255, 0, 128,
  111, 255, 111`;
- `+0x04`, derived from the existing `+0x06` and `+0x08` halfwords using the
  standard PSX page-coordinate bit packing visible in the instructions.

The function returns the allocation unchanged in `$v0`. It has no failure
branch after allocation, so the arena service's allocation contract is assumed.

## Fixed embedded upload

Immediately before the second call the function builds a `RECT`-compatible
record on its stack:

```text
x = 800, y = descriptor[+0x10] = 256
w = descriptor[+0x12] = 32
h = descriptor[+0x14] * 2 = 224
```

It calls `0x800EEC80` with `$a0 = &rect` and `$a1 = 0x8003484C`. The callee is
the SDK-style `LoadImage` path used elsewhere with the same `(RECT *, pixels *)`
shape. Thus the authoritative behavior is a 32-by-224-word upload from a fixed
embedded payload to VRAM `(800,256)`, rather than a read from the `.bt4` path.
The `shads.bt4` literal, sole-call context, and fixed atlas upload jointly make
the shadow-resource purpose substantially stronger than a generic display
descriptor label, while the exact original C type and identifier remain
unrecovered.

## Symbol decision

Add `[[func]] pc = 0x80141D6C` as `InitializeShadowTextureAtlas`, `status =
"known"`, and `emit = false`. The boundary, sole caller, dead `$a0`, live `$a1`,
allocation size, constants, source pointer, upload rectangle, and return value
are instruction-level facts. `known` rather than `confirmed` acknowledges that
the function and descriptor's original product names are inferred.

## Evidence inspected

- `generated/SCES_028.45_full_117.c`: sole call at
  `0x80124928..0x80124934` and publication of `$v0`.
- `generated/SCES_028.45_full_126.c`: complete function body and both calls.
- `generated/SCES_028.45_full_101.c`: `0x800EEC80` GPU upload implementation.
- `generated/SCES_028.45_full.ranges`: exact `0xF0`-byte function range.
- `generated/SCES_028.45_dispatch.c`: entry and continuation ownership.
- `docs/audits/semantic-8016e170.md`: authenticated path bytes and reference
  census.
