# Semantic audit: `0x801562B8`

## Result

The true callable owner at `0x801562B8` is now named
`ApplyRacerDamageEvent`. It converts a gameplay event into a signed damage
delta (or accepts an explicit delta), applies racer/session modifiers, updates
the racer's Q8 damage accumulator and integer percentage, and drives warning,
hit-timer, and disabled-state transitions.

```c
void ApplyRacerDamageEvent(uint32_t event_code,
                           int racer_index,
                           int32_t explicit_delta);
```

The routine has no semantic scalar return; all twelve callers ignore `v0` and
the epilogue does not establish one.

## Exact owner and alias proof

The owner opens a 64-byte frame at `0x801562B8`, saves `ra/fp/s0..s7`, and all
paths converge on the single restore/return at `0x80156B5C..0x80156B88`. The
next independent callable begins at `0x80156B8C`. The exact owner is therefore
`[0x801562B8, 0x80156B8C)`, `0x8D4` bytes.

Generated metadata repeats that owner range under entries `0x80156374`,
`0x801563AC`, `0x801563E8`, `0x80156424`, `0x8015645C`, `0x80156494`,
`0x801564CC`, `0x80156504`, `0x8015653C`, `0x80156584`, `0x801565CC`,
`0x801565DC`, `0x801565EC`, `0x80156634`, `0x801567AC`, `0x801567BC`, and
`0x801567C8`. These are jump-table cases or internal continuations: none has a
prologue or independent epilogue, and no direct JAL targets any of them.

## Entry gates and racer records

The session/racer record array begins at `0x801910E0` with stride `0x3A4`.
Before processing, the function returns early when byte `record + 0x58` is
nonzero, predicate `0x80134B60` returns nonzero, or mode word `0x801D1B80`
equals 1. No damage state changes on those exits.

Runtime racer objects come from `0x801D7038`, use stride `0x16C`, and provide
state/flags plus a scaling-class byte at `+0x15D`.

## Event dispatch and explicit deltas

When `explicit_delta != 0`, dispatch is bypassed and that signed value is used
directly. Otherwise `event_code < 0x84` indexes the 132-entry table at
`0x8016EB4C`. Only these codes have non-default cases:

```text
01 02 03 05 0C 0D 0E 0F 10 80 81 82 83
```

All other codes select zero. Ordinary cases load configured delta words at
`0x80178368..0x80178398`. Several also call
`0x80134CE8(record+0x34, magnitude, 1, duration)` to install a matching
transient racer effect. Cases `0x80..0x83` are special session/collision
events; they can derive negative deltas (`-160`, `-200`, `-240`, or `-280`)
and therefore reduce accumulated damage.

Positive deltas may raise runtime racer halfword `+0x134` to at least the
current `+0x136` value. Scaling then uses the racer's `+0x15D` class and table
`0x801761F8`, vacancy/player/mode predicates, and special handling for events
`0x80` and `0x83`. A runtime flag at `+0xE4` can suppress positive damage
outside the `0x83` case.

## Damage, percentage, and state effects

The core per-racer arrays are:

- `0x801EA798[index]`: signed Q8 damage accumulator;
- `0x801EA75C[index]`: integer damage percentage (`Q8 >> 8`);
- `0x801EA844[index]`: previous percentage snapshot;
- `0x801E9DA4[index]`: disabled/destruction state;
- `0x801E9D68[index]` and `0x801E9E04[index]`: event timers.

The selected signed delta is added to the Q8 accumulator and negative results
are clamped to zero. The shifted percentage is then published. A global
HUD/session mode can also clear another racer's visible percentage.

At 100 percent or above, if the disabled-state word is still `-1`, the routine
clamps the visible percentage to 100 and begins a transition. For the
active/occupied racer path, state becomes 100, runtime flags at `+0xE4` are
cleared, and callback `0x800EA0C8` is installed at session record `+0x39C`.
For the alternate path, state becomes 15. Selected events and flags also arm
120-frame timers.

Finally, when the previous percentage was below 76 and the new percentage is
in `[76,99]`, and racer-status predicate `0x80134964` accepts the slot, the
function calls `0x80137DFC(0x25, 0xFF, 0x0F, 0x0F, 0x100)` for a threshold
warning/effect. Current percentage is then copied to the previous snapshot.

The term "damage" is supported by the disabled threshold, clamping, positive
hit events, negative recovery cases, and warning crossing. The name retains
`Event` because the first argument selects side effects as well as magnitude.

## Direct callers

The retained EXE contains exactly twelve direct JALs to the true entry:

```text
800EAF40 800EB474 800EBB18 800EBB28 8011A504 80150CD8
80151E10 801526E8 80155A60 80156C9C 80159510 8015ACCC
```

Observed constant event codes include `0x81`, `0x82`, `0x80`, `0x0C`, `0x03`,
`0x0D`, `0x01`, and `0x02`; most callers pass zero for the explicit delta,
while physics/contact paths can supply a computed value. This corroborates the
event-plus-override ABI.

## Sources checked

- `disc/SCES_028.45`, respecting its 0x800-byte PS-X EXE header, for direct
  instruction decoding, jump-table extraction, and exhaustive JAL scans;
- `generated/SCES_028.45_full.ranges` for owner/alias ranges;
- direct caller windows and the audited racer-slot predicates at `0x8013491C`
  and `0x80134964`.
