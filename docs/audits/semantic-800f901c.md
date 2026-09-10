# Semantic audit: `0x800F901C` / `InitializeOrRefreshEffectSlot`

## Result and ABI

`0x800F901C` initializes or refreshes one indexed record in a 0xCC-byte effect
pool, and is now named `InitializeOrRefreshEffectSlot`:

```c
void InitializeOrRefreshEffectSlot(
    int slot, uint8_t owner_id, int mode, uint8_t variant,
    Object *source, EffectCallback callback0,
    EffectCallback callback1, EffectCallback callback2);
```

The first four arguments use `a0..a3`; `source` and the three callbacks are at
incoming stack offsets `+0x10..+0x1C`. No caller consumes a scalar result.

## Exact owner and slot admission

The generated owner `[0x800F901C,0x800F96D0)` is exact, length `0x6B4`. It
creates a 128-byte frame, saves `ra`, `fp`, and `s0..s7`, and returns through
`0x800F96A0..0x800F96CC`. The next independent owner starts at `0x800F96D0`.

The pool base is loaded from `0x8018811C`; shift/add arithmetic selects
`base + slot*0xCC`. Slot state byte `+5` controls admission:

- state zero takes the full initialization path;
- state three takes the refresh path at `0x800F9584`;
- every other state returns without mutation.

There is no explicit slot bound, null-source, or mode-range validation.

## Common binding and mode-specific setup

Both admitted paths bind `source` at slot `+0x18`, use source halfword `+0x34`
to enter its 24-byte geometry table, and advance until geometry flag byte
`+0x0F` has bit zero set; the selected geometry pointer is stored at slot
`+0x1C`. They mark byte `+2` active and derive randomized position/motion
fields from source words `+0x10/+0x14/+0x18` using `NextRandom15`.

Fresh initialization stores `owner_id` at `+6` and dispatches modes 1 through
4. The cases use different random displacement ranges, byte flags, halfwords
`+0xA0/+0xA2/+0xA4`, and geometry/distance helpers; mode four can seed one
slot from another slot's position. Unrecognized modes skip those specialized
fields but still reach the common finalization. State-three refresh repeats
the mode-three-style random position/flag setup without replacing the three
callback words.

On the fresh path, the caller callbacks are published in this exact order:

```text
incoming +0x14 -> slot +0xC0
incoming +0x18 -> slot +0xC8
incoming +0x1C -> slot +0xC4
```

The routine calls `0x800F96D0(slot)` after fresh mode setup, then stores the
callbacks. The refresh path stops after refreshing randomized fields. This
owner mutates pool records only; lower helpers own RNG and geometry math.

## Callers, targets, and overlay accounting

Exactly six executable calls target the true entry:

```text
800F8A98 800F8B18 800FA7DC 800FAAE8 800FB1BC 800FBE6C
```

Callers pass pool indices, compact owner/mode/variant bytes, an object pointer,
and three code pointers used as effect lifecycle callbacks. They occur across
several effect creation/update owners but are six unique guest instructions.
No external direct J/JAL targets an interior address.

An exact-body scan finds only `0x800F901C`; overlay overlap therefore creates
no duplicate implementation or duplicate logical caller. The name remains
effect-generic because the mode cases serve multiple visual/physics effects
and do not prove one source-level effect class.

## Sources checked

- `disc/SCES_028.45`, excluding its `0x800` header, for exact MIPS, caller ABI,
  state writes, target census, and duplicate-body scan;
- `generated/SCES_028.45_full.ranges` and generated owner for the exact extent;
- independently audited RNG and geometry helpers called by the mode cases.
