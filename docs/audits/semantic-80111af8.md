# Semantic audit: `0x80111AF8`

## Result and exact boundary

`0x80111AF8` is the packed-resource bundle loader
`LoadPackedResourceBundle` (`known`).  Its exact generated range is
`R 80111AF8 114`, covering 276 bytes through `0x80111C0B`.  It opens a
32-byte frame, saves `ra` and `s0..s2`, and returns through the
`jr ra` / stack-restore pair at `0x80111C04..0x80111C08`.  The callable at
`0x80111C0C` is independent; no caller enters an interior block.

The routine accepts one meaningful argument, `a0`, a packed bundle descriptor.
`a1` is explicitly zeroed at entry and is used only as the second argument to
`0x80116A54`; incoming `a1..a3` are not bundle parameters.  No meaningful
scalar return is established after the final callee.

## Bundle format and published state

The input layout is proven by width and pointer arithmetic:

```c
struct PackedResourceBundle {
    int16_t entry_count;      // +0
    uint16_t reserved;        // +2, not read here
    uint32_t entry_sizes[];   // +4, entry_count elements
    // packed payload immediately follows the size table
};
```

The signed count from `a0+0` is copied to the global halfword at
`0x801A4B4C`.  A non-positive count skips both table loops but still performs
destination acquisition and invokes the payload callee.

For a positive count, the first loop writes a zero at
`0x801CF09C` (`0x801CF098 + 4`), then cumulatively adds each 32-bit size from
`a0+4`.  It writes the successive prefix totals into the following words.
Consequently the workspace rooted at `0x801CF098` contains one separately
published base word at `+0`, followed by `entry_count+1` offsets at `+4`.

## Callee interactions

The routine has exactly two calls:

1. `0x80111B80` calls `0x80116A54` with
   `a0 = *(uint32_t *)0x8019862C` and `a1 = 0`.  Its returned destination base
   is stored at `0x801CF098`.
2. If the count is positive, the second loop adds that returned base to every
   previously built prefix offset, converting the offset table at
   `0x801CF09C` into absolute per-entry boundaries/pointers.
3. `0x80111BE8` computes
   `payload = a0 + 4 + 4*entry_count`, loads the published base from
   `0x801CF098` as `a1`, and calls `0x80115B1C(payload, base)`.

The exact allocation/arena policy of `0x80116A54` and compression algorithm of
`0x80115B1C` remain separate audits.  This owner nevertheless unambiguously
parses a count plus size table, constructs relocated entry boundaries, and
hands the trailing packed payload and acquired destination to the second
callee.  “Load packed resource bundle” captures that orchestration without
claiming a specific codec or original asset type.

## Complete direct callers

There are 33 direct `jal 0x80111AF8` sites:

- `0x800F0450` in `0x800F0428`;
- `0x80104B0C` in `0x80104AC4`;
- `0x80104D14`, `0x80104D68`, `0x80104DB4`, `0x80104E00`,
  `0x80104E4C`, `0x80104E98`, `0x80104EE4`, `0x80104F30`,
  `0x80104F7C`, `0x80104FC8`, `0x80105020`, `0x80105074`,
  `0x80105088`, and `0x801050C0`, all in `0x80104CE4`;
- `0x80110A24` in `0x801109BC`;
- `0x8011C930`, `0x8011C980`, `0x8011C9D0`, and `0x8011CA20`,
  all in `0x8011C910`;
- `0x80122844` in `RenderRaceFrame` (`0x8012268C`);
- `0x80131AA0`, `0x80131B70`, `0x80131BBC`, `0x80131CC8`, and
  `0x80131DBC`, all in `0x8013199C`;
- `0x801577EC`, `0x8015781C`, `0x801578AC`, `0x801578F0`,
  `0x8015796C`, and `0x801579F4`, all in `0x801577B0`.

The repeated calls from resource/menu setup, race rendering, and scene setup
owners explain the high fanout: this is a generic bundle-format loader rather
than one asset's private decoder.

## Confidence limits

The boundary, caller set, signed count, 32-bit size table, prefix sums,
relocation by the returned base, payload position, global publications, and
callee arguments are exact instruction evidence.  The `reserved` label for
input bytes `+2..+3` merely records that this owner does not read them.  No
specific compression format, heap identity, or resource subtype is asserted.
