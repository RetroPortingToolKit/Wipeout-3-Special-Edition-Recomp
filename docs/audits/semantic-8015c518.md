# Semantic audit: `0x8015C518`

## Result

The leaf at `0x8015C518` is the confirmed fixed-point direction helper
`Atan2Q12`. Its argument order follows mathematical `atan2(y, x)`:

```c
int Atan2Q12(int y, int x);
```

It returns a signed project angle in `[-0x800, +0x800]`. One complete turn is
`0x1000`, so `0x400`, `0x800`, and `-0x400` denote +90, 180, and -90 degrees.
In the race coordinate convention, callers such as spatial audio pass `(X,Z)`,
making angle zero point along +Z and positive rotation point toward +X.

## Exact boundary and ABI

- `0x8015C518` initializes its two sign flags and immediately begins testing
  signed `a1`; it has no dependency on preceding register state.
- It is a true leaf: it creates no frame, changes no global state, and contains
  no call. All paths return through `jr ra` at `0x8015C684`, with a NOP delay
  slot at `0x8015C688`.
- The exact range is `0x8015C518..0x8015C68B`, `0x174` bytes. Three alignment
  NOPs occupy `0x8015C68C..0x8015C697`; a different large framed routine begins
  independently at `0x8015C698`.
- Inputs and output are signed 32-bit integers. `INT_MIN` is not specially
  guarded: two's-complement `negu` leaves it negative, so callers are expected
  to supply ordinary bounded geometry/audio components.

## Sign, axes, and quadrants

The function records `x < 0` and `y < 0`, then takes each magnitude with
`negu`. It first computes a first-quadrant base angle in `0..0x400`, then folds:

```c
if (x_was_negative)
    angle = 0x800 - angle;
if (y_was_negative)
    angle = -angle;
```

This produces the exact axis behavior:

| `(y,x)` | result |
|---|---:|
| `(0,0)` | `0` |
| `(0,+)` | `0` |
| `(+,0)` | `+0x400` |
| `(0,-)` | `+0x800` |
| `(-,0)` | `-0x400` |

Thus the negative-x branch is represented by positive `+0x800`, while nearby
third-quadrant results approach `-0x800`. This is a signed wrap choice, not an
error.

## Ratio selection and lookup

After taking magnitudes, the function compares `abs(y)` with `abs(x)`:

- when `abs(y) < abs(x)`, it computes
  `r = floor(abs(y) * 1024 / abs(x))` and takes `base = table[r]`;
- otherwise it computes
  `r = floor(abs(x) * 1024 / abs(y))` and takes
  `base = 0x400 - table[r]`.

The ordinary path uses a left shift by ten before signed division. To avoid
overflow for a numerator with any `0x7FE00000` bits set, the large-input path
instead divides by the denominator shifted right ten. This preserves bounded
table indexing but can lose low-order ratio precision for very large inputs.

`table` is 1025 consecutive signed halfwords at `0x801786D8`, indexed from
ratio 0 through 1024 inclusive. The retained table is monotone from `0` to
`512`; representative entries match the Q12-turn-scaled arctangent:

| ratio index | table value | direction |
|---:|---:|---:|
| 0 | 0 | 0 degrees |
| 256 | 160 | atan(1/4) |
| 512 | 302 | atan(1/2) |
| 1024 | 512 | 45 degrees (`0x200`) |

Its exact 2050-byte SHA-256 is
`b95a5540f30e1f0f40975157595d078bb07d3242d2c856a1c679e533e2f26233`.
The ratio selection plus quadrant folds prove atan2 behavior independently of
the symbol name.

## Complete direct callers

An aligned scan of every instruction word in the retained PS-X EXE finds 35
direct calls:

```text
800E3F6C 800E401C 800E44C4 800E9878 800E9888 800E9B5C 800E9B6C
800E9EC0 800E9ED0 800EA078 800EA088 800EA360 800EA370 800EA51C
800EA52C 800EEFDC 800EFB40 800EFD6C 800F32B4 800F32C4 800F8D28
800F8D8C 8012450C 80132E6C 80136B38 80136DCC 80137234 8013B730
8013C11C 8013C2C4 8014C788 8014C7A0 801556D0 8015A548 8015A560
```

No JAL targets an interior instruction. The call at `0x80136DCC` is
independently recovered in the spatial-audio audit: it passes midpoint X and Z,
subtracts the returned direction from listener heading, wraps the result into
`[-0x400,+0x400]`, and derives stereo pan. Other paired call clusters operate
on geometry components, supporting the shared signed-direction role.

## Catalog decision

`0x8015C518` is `Atan2Q12`, `status="confirmed"`, `emit=false`. The function is
fully recovered but remains guest-owned alongside the adjacent Q12 sine/cosine
helpers.

## Sources checked

- `disc/SCES_028.45`: authoritative instruction bytes and direct-call census.
- `generated/SCES_028.45_full_132.c`: complete leaf and adjacent boundaries.
- `generated/SCES_028.45_full.ranges`: exact `0x174` range.
- `docs/audits/semantic-80136d0c.md`: independent spatial `(X,Z)` direction
  consumer and heading/pan use.
- `docs/audits/semantic-8015c318.md`: neighboring `0x1000`-unit-turn Q12
  sine/cosine convention.
