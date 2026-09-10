# Semantic audit: `0x80134CE8` / `SubmitRumbleDecayRequest`

## Result

`0x80134CE8` is a conditional request setter for the decaying analog component
of a controller-rumble state record, now named `SubmitRumbleDecayRequest`:

```c
void SubmitRumbleDecayRequest(RumbleState *state,
                              int32_t amplitude_q8,
                              int32_t priority,
                              int32_t decay_q8);
```

The request replaces the active one only when neither its priority nor its
amplitude is lower. Rejected requests leave all state unchanged.

## Exact boundary and ABI

The complete owner is `[0x80134CE8,0x80134D24)`, exactly `0x3C` bytes and
matching the generated range table:

```text
80134CE8  lw    v0,0x10(a0)
80134CEC  nop
80134CF0  slt   v0,a2,v0
80134CF4  bnez  v0,0x80134D1C
80134CF8  nop
80134CFC  lw    v0,0x0C(a0)
80134D00  nop
80134D04  slt   v0,a1,v0
80134D08  bnez  v0,0x80134D1C
80134D0C  nop
80134D10  sw    a1,0x0C(a0)
80134D14  sw    a2,0x10(a0)
80134D18  sw    a3,0x14(a0)
80134D1C  jr    ra
80134D20  nop
```

The preceding independent setter returns at `0x80134CE0` with its delay slot
at `0x80134CE4`. The next owner creates a stack frame at `0x80134D24`.
This routine is a frameless leaf with no callee.

`a0` is the rumble-state pointer, `a1` is signed Q8 motor amplitude, `a2` is
signed request priority, and `a3` is the signed Q8 decay multiplier applied on
later ticks. The routine does not establish `v0`; all callers ignore its
compare residue, so it has no semantic return.

## Acceptance formula and stores

The exact signed comparisons are:

```c
if (priority < state->priority)
    return;
if (amplitude_q8 < state->amplitude_q8)
    return;

state->amplitude_q8 = amplitude_q8; // +0x0C
state->priority     = priority;     // +0x10
state->decay_q8     = decay_q8;     // +0x14
```

Equality is accepted. Consequently a new request must dominate or tie the old
request on both axes; high priority alone cannot replace a stronger amplitude,
and high amplitude alone cannot replace a higher priority. The three stores
occur only after both gates, in the order amplitude, priority, decay.

No field at `+0x00..+0x08`, global, hardware register, or caller memory outside
the supplied record is changed.

## Rumble-state proof

The adjacent initializer at `0x80134C60` creates a 24-byte state and zeros all
five payload words. The per-tick consumer at `0x80134D24` proves the fields:

- it reads `+0x0C`, shifts right by eight, clamps to `0xFF`, and passes that
  byte as the second actuator value to `0x80120C24`;
- `0x80120C24` writes the two actuator bytes into the per-port array at
  `0x801D59D8`;
- after output, the updater computes
  `state->amplitude_q8 = (state->amplitude_q8 * state->decay_q8) >> 8`;
- it clears amplitude and priority once the decayed amplitude falls below 50;
- the parallel `+0x04/+0x08` pair controls the first actuator as a countdown
  and priority, explaining why this owner manages the three-word analog/decay
  side of the same rumble record.

Common calls pass amplitude `0x8000`, priority `1`, and decay `0xCC`; other
observed fixed requests use `(0x5A00,1,0xCC)`, `(0x4B00,1,0xEE)`, and
`(0x8000,1,0xE6)`. These are coherent Q8 amplitudes and per-tick retention
factors rather than durations: `0xCC`, `0xE6`, and `0xEE` are approximately
0.797, 0.898, and 0.930.

## Complete caller and interior-target census

An exhaustive aligned instruction-word scan of the retained executable finds
exactly 12 direct calls:

```text
800EAF30 800EB4BC 800EC6B4
80119A30 8011A37C 8011A83C 8011DEE8
8014E718 80152684
80156574 801565BC 80156624
```

All target the true entry at `0x80134CE8`. There are no direct `j` or `jal`
targets at any interior address in `[0x80134CEC,0x80134D24)`. Caller records
are consistently selected from the per-racer/session array, frequently at
base-plus-`0x34`, and all callers treat the routine as a state-only operation.

## Sources checked

- `disc/SCES_028.45`, accounting for its 0x800-byte PS-X EXE header, for exact
  instructions, all caller windows, and exhaustive aligned target scans;
- `generated/SCES_028.45_full.ranges` for the exact 0x3C-byte boundary;
- neighboring initialization and tick-consumer code at `0x80134C60` and
  `0x80134D24` for record layout, actuator output, and decay semantics;
- `docs/audits/semantic-801562b8.md` for the independently recovered damage
  events that submit matching transient rumble requests.
