# Semantic audit: `0x80134D24`

## Result and exact owner

`0x80134D24` is the per-tick rumble consumer now named
`UpdateRumbleStateForPort`. The generated range is exactly
`[0x80134D24, 0x80134DEC)`, `0xC8` bytes. It creates a 24-byte frame, saves
`ra/s0`, and returns through the sole epilogue at `0x80134DDC..0x80134DE8`.
The preceding `SubmitRumbleDecayRequest` ends at `0x80134D23`; the independent
owner at `0x80134DEC` follows immediately.

There are no false callable splits. Generated dispatch labels at
`0x80134D4C..0x80134DDC` are ordinary internal branch blocks retaining the
same frame. No external J/JAL targets any of them, and the complete owner body
occurs once in the retained executable.

## ABI and state layout

The recovered ABI is:

```c
void UpdateRumbleStateForPort(RumbleState *state, int port);
```

It establishes no semantic return. The independently initialized and produced
24-byte state has this exact consumed layout:

```text
+0x00  enabled gate
+0x04  digital-motor countdown
+0x08  digital-request priority
+0x0C  signed Q8 analog-motor amplitude
+0x10  analog-request priority
+0x14  signed Q8 per-tick analog retention/decay
```

## Output and state evolution

Both actuator outputs begin at zero. If `state->enabled` is nonzero:

- the digital output is boolean `state->countdown != 0`;
- a negative analog amplitude produces zero;
- otherwise analog output is `amplitude_q8 >> 8`, clamped to `0xFF`.

The function calls the leaf at `0x80120C24`, which masks `port` to three bits
and stores the two bytes at `0x801D59D8 + 2*(port & 7)`. Output occurs every
call, including disabled states, so stale actuator bytes are replaced by zero.

After output, disabled states return immediately: their countdown, priorities,
amplitude, and decay are frozen. Enabled states evolve as follows:

```c
if (state->countdown != 0) {
    if (--state->countdown == 0)
        state->digital_priority = 0;
}

state->amplitude_q8 =
    (state->amplitude_q8 * state->decay_q8) >> 8;

if (state->amplitude_q8 < 50) {
    state->amplitude_q8 = 0;
    state->analog_priority = 0;
}
```

The multiply uses signed operands, the low 32-bit product, and arithmetic
right shift. Threshold 50 is in the stored Q8 domain. The decay word itself
and enabled gate are never changed here.

## Real callers versus generated emissions

An aligned scan of the retained EXE proves exactly two distinct call
instructions:

```text
80123E30  port 0
80123E70  port 4
```

Both compute a 24-byte rumble-state address from session/player indices and a
common base at `0x80191114`. The first passes port zero; the second explicitly
passes port four. The callee's `port & 7` behavior makes both distinct slots.

Generated sources contain **458** call emissions: the two retained main-image
calls plus 456 emissions in `overlays_static.c`. Those overlay emissions are
retranslations of the same two guest call PCs across many overlay images, not
456 additional executable caller addresses. The semantic caller count is
therefore two.

## Relationship to request producers

`SubmitRumbleDecayRequest` at `0x80134CE8` conditionally replaces fields
`+0x0C/+0x10/+0x14`; its observed requests use Q8 amplitudes such as `0x8000`
and retention factors `0xCC/0xE6/0xEE`. The adjacent digital request setter and
initializer own the parallel countdown/priority and enable state. This updater
is the common consumer that publishes both channels and retires their request
priorities as each effect ends.

## Sources checked

- `disc/SCES_028.45`, after the 0x800-byte PS-X EXE header, for exact MIPS,
  retained callers, interior targets, and body duplication;
- `generated/SCES_028.45_full_39.c`, `.ranges`, and `overlays_static.c` for
  owner continuity and generated-emission accounting;
- `docs/audits/semantic-80134ce8.md` plus direct disassembly of `0x80120C24`
  for request and actuator-array semantics.
