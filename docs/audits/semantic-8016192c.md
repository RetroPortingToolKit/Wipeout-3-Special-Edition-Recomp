# Semantic audit: `0x8016192C`

## Result

Recommended symbol: `AccessSio1ControlState` (`known`, `emit = false`).

This is a real three-argument callable dispatcher, not one of the many
generated entries inside its jump tables.  It provides the title's common
read/write/lifecycle interface to the second serial port (SIO1).  `a0` selects
one of six operation families, `a1` selects a member of that family, and `a2`
is a value or side selector.  The function returns an unsigned register value,
a Boolean, a previous software value, zero for successful/no-op setters, or
`-1` for an invalid baud divisor/table selection.

## Exact boundary and callers

- Entry: `0x8016192C` (32-byte frame, saved `s0`, `s1`, and `ra`).
- Final common epilogue: `0x80161DE8..0x80161DFF`; the delay slot at
  `0x80161DFC` restores the frame.
- Exact size: `0x4D4` bytes.  `generated/SCES_028.45_full.ranges` records
  `R 8016192C 4D4`.
- `0x80161E00` begins a separate leaf with no shared frame and is also a
  distinct generated function/range.
- There are 40 distinct direct `jal 0x8016192C` sites in the main executable:
  four at `0x80113D34/48/5C/74`, then 36 in the serial-link owner region
  `0x80114F20..0x80115820`.  No direct call targets any interior selector arm.

The manifest additionally emits `F` entries for the first- and second-level
jump-table destinations (`0x80161964`, `0x80161A28`, `0x80161A58`,
`0x80161A78`, `0x80161A9C`, `0x80161B14`, `0x80161B3C`, `0x80161B98`,
`0x80161C00`, `0x80161D2C`, `0x80161DB4`, `0x80161DBC`, `0x80161DD8`, and
`0x80161DE8`).  Each is paired with the same owner range
`R 8016192C 4D4`; these are internal computed-jump labels with live owner
register/frame state, not callable ABIs.

## Hardware identity

The global pointer loaded from `0x80179360` is initialized in the executable to
`0x1F801050`, the SIO1 register block.  The accessed halfwords identify the
interface independently of callers:

- `+0x04`: SIO status (`STAT`);
- `+0x08`: serial mode (`MODE`);
- `+0x0A`: serial control (`CTRL`);
- `+0x0E`: baud reload (`BAUD`).

The software shadow begins at `0x801EC3A8`; setters update the shadow and copy
the affected halfword to the hardware block.  The constant `0x001FA400` is
used to convert a requested rate to an exact baud reload value.

## Selector matrix

### `a0 = 0`: query

`a1` values 0 through 6 respectively return: raw `STAT`; a compact CTRL-bit
state made from bits 1 and 5; the shadowed mode value; `0x1FA400 / shadow_baud`;
whether `STAT & 0x100` is set; one of two software words selected by `a2`; or
the Boolean nonzero state of one of two software words selected by `a2`.
Out-of-range `a1` returns zero.

### `a0 = 1`: configure

- `a1 = 0`: replace CTRL bits 1 and 5 from `a2 & 1` and `a2 & 2`.
- `a1 = 1`: write `a2` to the shadow and hardware MODE halfword.
- `a1 = 2`: require `a2` to divide `0x1FA400` exactly, write the quotient to
  BAUD, and set CTRL bit `0x10`; return `-1` when it is not exact.
- `a1 = 3`: require table selector `a2` in 1..8, replace CTRL bits `0x300`
  with the signed-halfword table result, and return that result; invalid values
  return zero.
- `a1 = 4` is an explicit no-op.

### `a0 = 2`: lifecycle/control

`a1 = 0` enters a critical section (`0x801671D0`, syscall 1), enables CTRL
bits `0x50`, and restores MODE/CTRL/BAUD from the RAM shadow.  If the syscall
reported a newly entered section, it balances it through `0x80165550`
(syscall 2).  `a1 = 1` sets CTRL bit `0x10`.  Values 2 and 3 enter the same
critical section, clear CTRL bit `0x400` or bits `0x820`, respectively, and
zero the corresponding software state pair before the shared balancing path.

### `a0 = 3..5`: small controls

- `a0 = 3, a1 = 0`: set or clear CTRL bit `0x20` according to `a2`.
- `a0 = 3, a1 = 1`: return whether `STAT & 0x100` is set.
- `a0 = 4, a1 = 0`: replace a private software word with `a2` and return its
  previous value.
- `a0 = 5, a1 = 0`: call the service initializer at `0x8015CF78` with argument
  5 and return zero.  Other subselectors are no-ops returning zero.

## Confidence limits

The SIO1 hardware identity, exact boundary, selector behavior, direct callers,
and critical-section calls are instruction-level facts.  Names of the private
RAM state pairs and the higher-level serial-link protocol remain unresolved;
the symbol therefore describes the proven hardware-facing contract without
claiming a specific PsyQ source symbol.
