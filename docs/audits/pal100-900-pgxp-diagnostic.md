# PAL x2 / 100 Hz + 900% + PGXP diagnostic

This is a focused acceptance receipt for the combined configuration already
selected by the title: 900% CPU overclock, PAL CRTC x2 at a 100 Hz host, and
PGXP geometry/perspective/depth enabled. It is deliberately separate from
runtime changes and from the normal-vs-Fast-Loading comparator.

## Capture matrix

Run one deterministic route with the same BIOS, PAL cue, renderer, display
mode, and input route for every row. Record the runtime debug receipts for
each endpoint in one JSON file:

| Phase | Required endpoint evidence |
|---|---|
| `boot` | BIOS/game start, CRTC mode, guest cadence |
| `title_selector` | selector choice and PAL-x2 state |
| `race` | loaded track, stable on-track continuation, frame/fingerprint window |
| `save_load_state` | committed `savestate_status` generation before and after restore |
| `memory_card` | card command/read/write order and data checksums |
| `cd_load` | CD command/sector/IRQ/DMA order and load completion |

Every row must report `pass: true`, a non-empty `evidence` reference, zero
`audio_underruns`, and `fmv_active: false`. FMV is a negative exclusion here:
it must be tested in a separate native-50-Hz/FM​V receipt, never hidden inside
this gameplay/load pass. Likewise, CDDA/XA audio must remain observable and
must not be treated as a skippable load interval; the zero-underrun check is a
host-audio health gate, not proof that FMV was exercised.

## Receipt gate

The checked-in validator consumes the durable JSON produced by the existing
debug harness:

```powershell
python tools/verify_pal100_900_pgxp_diagnostic.py artifacts/pal100-900-pgxp.json
```

The top-level fields are `overclock_percent: 900`,
`pal_crtc_multiplier: 2`, `host_hz: 100`, `pgxp: true`, and a `cases` array.
The validator requires exactly the six named phases above (additional cases
are allowed), while intentionally ignoring process-local trace sequence
counters. Use `tools/compare_guest_load_traces.py` separately for the
`startup-card` and `race-load` normal/accelerated gates.

This receipt is diagnostic evidence only. It does not enable PAL100 by
default, and a pass does not claim full FMV coverage or byte-for-byte
normal-vs-accelerated equivalence.
