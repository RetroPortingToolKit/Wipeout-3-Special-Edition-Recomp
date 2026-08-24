# Semantic audit: `0x80117C08`

## Result

`0x80117C08` is `ResolveModeDependentIndex`, an exact 92-byte leaf which
maps an integer index according to a selector.  Its only state dependency is
the eight-byte permutation at `0x8016DD60`.

## Boundary and ABI

The retained manifest range is `R 80117C08 5C`.  The function begins with an
eight-byte stack allocation, contains six basic blocks and no calls, and ends
with `jr ra` at `0x80117C5C`; the delay slot at `0x80117C60` restores `sp`.
The next independent function begins at `0x80117C64`, proving the half-open
boundary `[0x80117C08,0x80117C64)`.

The recovered ABI is:

```c
uint32_t ResolveModeDependentIndex(int32_t index, int32_t mode);
```

`a0` is the index, `a1` is the selector, and `a2`/`a3` are never read.  `v0`
is the sole result.  The complete behavior is:

```c
uint8_t permutation[8] = {0, 5, 1, 4, 2, 3, 6, 7};

if (mode == 2) return index + 8;
if (mode == 0) return index + 14;
if (mode == 1) return permutation[index];
return 1;
```

The additions use ordinary 32-bit MIPS arithmetic.  The mode-1 byte load has
no local bounds check, so `index` in `0..7` is a caller-side invariant.

## State behavior

Two `lwl`/`lwr` pairs read exactly eight bytes from `0x8016DD60`; two matching
unaligned stores place a snapshot in the local stack frame.  The mode-1 path
indexes that snapshot.  The EXE bytes are the permutation
`00 05 01 04 02 03 06 07`.  The function performs no global store and calls
no helper.  Thus the temporary frame is a table snapshot, not a saved-register
frame, and the function has no state mutation beyond its private stack.

The conservative name records the proven index transformation.  The caller
set uses both constant and computed selectors, but does not by itself prove a
domain-specific meaning for each selector.

## Direct callers

There are exactly 23 retained direct JAL sites, grouped by their generated
owner:

- `0x800FF154`: `0x800FF384`
- `0x80100094`: `0x80100504`
- `0x801015C0`: `0x80101628`, `0x801016C0`
- `0x801018F0`: `0x80101C80`, `0x80101CEC`, `0x80101D5C`, `0x80101E5C`
- `0x80101F14`: `0x80101F38`, `0x80101FA0`
- `0x8010205C`: `0x801020A0`, `0x80102140`, `0x80102178`, `0x801021B8`, `0x801022B8`
- `0x80102770`: `0x801027B0`
- `0x80113EB4`: `0x80114098`
- `RunRaceSession` (`0x80116128`): `0x801165DC`, `0x80116658`
- `0x80123060`: `0x80123210`
- `0x801296FC`: `0x80129820`, `0x80129884`
- `0x8012995C`: `0x8012A3CC`

The list totals 23 and contains no interior target into the function body.
