# Semantic audit: owner of the call at 0x80133D1C

## Result

The `jal 0x8013535C` at `0x80133D1C` belongs to the argument-free routine
starting at `0x80133B64`.  The symbol map names that routine
`InitializeRaceViewportState` with `status = "known"`.

The source-range split at `0x80133D3C` is not a source-level function
boundary.  Execution falls into it with the original frame live.  Likewise,
`0x80133F60` is the shared tail/epilogue reached from earlier branches.  The
logical routine therefore spans `0x80133B64..0x80134013`; the next independent
prologue begins at `0x80134014`.

## Boundary and ABI evidence

- `0x80133B64` allocates a 56-byte frame and saves `ra`, `fp`, and `s0..s7`.
- No instruction reads an incoming `a0..a3` value before the routine replaces
  or passes those registers to callees.  Its observable result is global and
  record initialization, so its recovered prototype is `void fn(void)`.
- The generated `0x80133B64` range falls through at `0x80133D38` into
  `0x80133D3C`.  There is no new prologue and no return between them.
- Branches at `0x80133D30` and in the `0x80133D3C` continuation reach
  `0x80133F60`.  That range reloads the registers saved at `0x80133B68..88`,
  restores `sp` by 56 at `0x80134010`, and returns through the original `ra`.
- The retained image has four direct calls to the true entry: `0x8012BC98`
  (inside `0x8012BB58`), `0x8012C064` (inside `0x8012BFFC`), `0x80133B2C`
  (inside `0x801339A4`), and `0x80134308` (inside `0x80134014`).  There are no
  direct calls to `0x80133D3C` or `0x80133F60`.

## Recovered role

The first phase derives `index * 0x3A4`, demonstrating that the array rooted
at `0x80191114` consists of 932-byte records.  For every configured view it:

- calls `0x80134C60` on the record;
- enables the record and clears per-view state;
- installs display/projection fields at offsets `+0x370..+0x37A`, including
  the constants 320, 256, and 512 and two values selected from the tables at
  `0x80173B6C` and `0x80173BA0`;
- clears several record-local counters, flags, and four bytes at `+0x354`.

After the loop it publishes the viewport-array end at `0x8018F088`.  It reads
the active-view index from `0x8019BE44`, selects the matching 0x3A4-byte record,
and publishes that pointer at both `0x8018F084` and `0x801A5840`.  The call at
`0x80133D1C` then invokes `RebuildViewportClipCoefficients`, whose independent
audit proves that it rebuilds the global projection/clip coefficients from the
newly selected display state.

The continuations at `0x80133D3C` and `0x80133F60` assign remaining
racer/player viewport state and call `0x8013491C` during the final mapping
pass.  This supports the deliberately broad `InitializeRaceViewportState`
name.  More specific game-mode names would require semantics for the upstream
callers and the record types, so they are not claimed here.

## Sources checked

- `generated/SCES_028.45_full.ranges`: generated splits and sizes.
- `generated/SCES_028.45_full_119.c`: call sites at `0x8012BC98` and
  `0x8012C064`.
- `generated/SCES_028.45_full_122.c`: complete entry, call at `0x80133D1C`,
  internal continuations, epilogue, and call sites at `0x80133B2C` and
  `0x80134308`.
- `docs/audits/semantic-8013535c.md`: independent callee semantics.
