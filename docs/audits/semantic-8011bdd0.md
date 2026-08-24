# `0x8011BDD0` menu-action input classifier

## Result

`0x8011BDD0..0x8011C2DB` is one callable, returning function.  It is named
`GetMenuActionState` in `symbols.toml`:

```c
int GetMenuActionState(int16_t action);
```

The routine converts one of twelve menu-action indices into either a
deadzone-qualified analog direction or the corresponding digital-pad mask.
It is not a generic direction-only predicate: indices 4..11 cover face,
start/select, and shoulder buttons, and the digital result is accumulated
across eligible racer/controller records.

## Exact boundary and ABI

The entry at `0x8011BDD0` creates a 32-byte frame and saves `s0..s2` and `ra`.
The single shared epilogue is `0x8011C2C0..0x8011C2DB`; it restores those
registers, sign-extends the low halfword of the accumulator into `v0`, and
returns.  Thus the exact size is `0x50C` bytes.  `0x8011C2DC` is the next
callable entry (a jump to `0x80174D48`) and is not part of this function.

At entry, `a0` is explicitly truncated/sign-extended to 16 bits.  The digital
path bounds it with `sltiu action,12`, so negative and out-of-range values do
not index the jump table and return zero unless the earlier analog test has
already succeeded.  The return is a signed integer: zero means inactive;
analog activation returns one; digital activation adds the selected nonzero
button bit for every eligible record.

## Analog-pad record and deadzone

The selected raw-pad pointer is one of the globals at `0x8018F088` and
`0x8018F084`; the choice is controlled by the mode state around
`0x801D284C`, including the call to `0x8011767C(2)` and the controller identity
comparison using `0x801D1B40`.  In either case the routine requires the
halfword at pad-record offset `+0x20` to equal `0x7300`.  In byte order this is
the standard raw pad header `{0x00, 0x73}`, identifying an analog controller.

It then advances to that header and reads bytes `+6` and `+7`, the left-stick
X and Y bytes.  Each byte is centered by subtracting `0x80`; its absolute
deviation must be at least `0x5D` (93), so the accepted outer ranges are
`0..35` and `221..255`.  Direction/action mapping is exact:

| action | analog condition |
|---:|---|
| 0 | Y < 128 (up) |
| 1 | Y >= 221 (down) |
| 2 | X < 128 (left) |
| 3 | X >= 221 (right) |

The positive-side rows imply the same 93-count threshold (`X/Y - 128 >= 93`).
The bytes at `0x80165CE4/0x80165CE5` form an analog-direction latch/suppression
gate: a recognized direction sets `0x80165CE4`; a nonzero `0x80165CE5`
suppresses the analog result, and an invalid analog header clears
`0x80165CE4`.  The routine falls through to digital mapping when analog did
not yield an accepted result.

## Digital action mapping

The jump table is the twelve words at `0x8016E0FC`.  Each case selects a bit
from halfword `+0x2E` of an eligible record in the table at `0x801910E0`, whose
stride arithmetic is exactly `0x3A4` bytes:

| action | mask | PSX control |
|---:|---:|---|
| 0 | `0x0010` | Up |
| 1 | `0x0040` | Down |
| 2 | `0x0080` | Left |
| 3 | `0x0020` | Right |
| 4 | `0x1000` | Triangle |
| 5 | `0x4000` | Cross |
| 6 | `0x0008` | Start |
| 7 | `0x0001` | Select |
| 8 | `0x2000` | Circle |
| 9 | `0x8000` | Square |
| 10 | `0x0400` | L1 |
| 11 | `0x0800` | R1 |

The loop bound is the unsigned halfword at `0x801A8628`.  A record normally
participates when its byte `+2` is nonzero; the record selected by the signed
global index at `0x801A5E44` is also admitted.  The mode/controller filters at
`0x801D284C` and `0x801D1B3C..0x801D1B41` can skip other records.  The selected
mask is added, rather than normalized, which explains why callers consistently
test the result only for zero/nonzero.

## Caller proof

A direct scan of the authenticated executable finds 49 `jal 0x8011BDD0`
instructions.  They are concentrated in menu/UI owners at
`0x80100C68..0x80100E74` and `0x80128830..0x8012B548`.  Their delay slots pass
literal action indices (predominantly directions 0..3 and actions 4..7), and
the immediately following branches test `v0` for zero/nonzero.  Representative
sites are:

- `0x80128830` / `0x80128878`: actions 0 and 1 drive opposing menu movement.
- `0x80128904` / `0x8012895C`: actions 2 and 3 decrement/increment a value.
- `0x80128C28` / `0x80128C38`: actions 5 and 6 are alternative accept/advance
  inputs.
- `0x8012AAC4` and `0x8012AB84`: action 4 is tested as a face-button command.

These call shapes, the literal jump-table masks, and the raw `0x73` analog-pad
layout jointly establish the menu-action classifier semantics.
