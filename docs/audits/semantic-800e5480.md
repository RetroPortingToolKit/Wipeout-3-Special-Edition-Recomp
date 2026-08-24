# Semantic audit: `0x800E5480` / `InitializeIndexedRacerSpeedState`

## Result

`0x800E5480` is a small runtime-racer initializer now named
`InitializeIndexedRacerSpeedState`:

```c
void InitializeIndexedRacerSpeedState(RuntimeRacer *racers, int racer_index);
```

It selects one `0x16C`-byte runtime racer, derives an initial speed-like value
from two established racer parameters, and clears two associated transient
halfwords.

## Exact owner and ABI

Generated metadata assigns `0x800E5480` one exact `0x5C`-byte range. It is a
frameless leaf with no calls; its single return is at `0x800E54D4` with the
last store in the delay slot, and the next owner starts at `0x800E54DC`.
Therefore the complete function is `[0x800E5480,0x800E54DC)`.

The shift/add chain computes `racer_index * 364` exactly, proving the known
`0x16C` runtime-racer stride. `a0` is the array base and `a1` is used directly
as the index without bounds or sign checks. No scalar result is established,
and every caller ignores register residue, so the ABI is mutating and void.

## Exact arithmetic and state effects

For `r = racers[racer_index]`, the routine performs:

```c
int32_t from_signed_parameter = 9 * (int16_t)r.half_146;
uint32_t from_unsigned_parameter = (5 * (uint16_t)r.half_14A) >> 2;

r.half_132 = (int16_t)min(from_signed_parameter,
                          (int32_t)from_unsigned_parameter);
r.half_14C = 0;
r.half_12A = 0;
```

The load at `+0x146` is signed; the load at `+0x14A` is unsigned. The latter's
multiply cannot overflow a signed 32-bit word for a 16-bit input, so its
arithmetic right shift is exactly floor division by four. The comparison is
signed and there is no lower clamp. The selected value is truncated to a
halfword on store. No other racer field or global is touched.

The name uses `SpeedState` because `+0x132` is repeatedly consumed and adjusted
by racer motion/handling paths, while `+0x146` and `+0x14A` are racer tuning
parameters. It does not guess narrower meanings for the two cleared transient
fields.

## Callers, lifecycle context, and overlay duplication

An exhaustive aligned scan of the retained executable finds six unique JALs,
all to the entry:

```text
801044A0  80104720  801279B4  80127BC8  80127F0C  8014EC5C
```

No J/JAL targets an interior address. The generated corpus emits seven calls
because `0x8014EC5C` is represented twice by overlapping translations; the
other five are emitted once. This is one duplicated instruction word, not a
seventh semantic caller or another copy of the callee.

All sites supply a runtime-racer array base and racer index. The callers occur
in racer setup/reset transitions, commonly alongside clearing runtime words
at `+0xE4/+0x110`, halfword `+0x152`, and installing behavior callback
`0x800E2D00` at `+0xD8`. Two callers invoke this helper conditionally while
switching to that callback; the remaining setup paths invoke it directly.
This lifecycle context supports initialization rather than a general getter or
per-frame arithmetic helper.

## Sources checked

- `disc/SCES_028.45`, accounting for its `0x800`-byte PS-X EXE header, for
  exact instructions, arithmetic, unique callers, and interior-target scans;
- `generated/SCES_028.45_full.ranges` for the exact owner boundary;
- generated call emissions for the seven-emitted/six-unique overlay proof;
- `docs/audits/semantic-801562b8.md` for the independently established
  `0x16C` runtime-racer array identity and stride.
