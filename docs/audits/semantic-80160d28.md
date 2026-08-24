# Semantic audit: `0x80160D28`

## Result

`0x80160D28` is the canonical LibCD `CdInit` routine. It retries the lower CD
interrupt/hardware initializer, installs LibCD's internal completion callbacks
on success, and returns a Boolean success result.

## Boundary and ABI

The generated manifest gives exact range `0x80160D28..0x80160DB7` (0x90 bytes).
The next independent owner is the low-level initializer at `0x80160DB8`.
Entry creates a 24-byte frame and saves `$s0/$ra`; the epilogue at
`0x80160DA8..0x80160DB7` restores both and returns. There is no alignment tail
or semantic spill into the next owner.

The canonical ABI is:

```c
int CdInit(void);
```

No argument register is read before being repurposed for diagnostics. The
routine returns exactly 1 after successful initialization and 0 after all
attempts fail.

## Initialization and CD state

`$s0` starts at 4. The routine calls `0x80160DB8`; a result of 1 succeeds.
Otherwise it decrements `$s0` and repeats while the value is not -1, proving a
maximum of five attempts rather than four.

On success it installs:

- `0x80160DF4` into sync-callback word `0x8017B124`;
- `0x80160E1C` into ready-callback word `0x8017B128`;
- `0x80160E44` into the CD data/interrupt callback word `0x801792C8`;
- zero into companion state word `0x801792CC`.

These callback words connect directly to the separately audited
`CdSyncCallback` and `CdReadyCallback` exchange routines and the lower LibCD
event dispatcher. The successful path then returns 1.

After five failures it passes the retained string at `0x8017F26C` to `Printf`
at `0x8015CF48` and returns 0. The failure path does not install or clear the
four callback/state words above.

## Calls and targets

The only direct callees are the low-level initializer `0x80160DB8` and failure
diagnostic `Printf`. Generated continuation labels at `0x80160D38`, `40`, `4C`,
`88`, `98`, `A4`, and `A8` are ordinary internal blocks; no external call or
branch targets an interior address.

There are exactly seven generated and seven unique executable `jal` callers,
with no overlay duplication:

`0x80112CB8`, `0x80112D94`, `0x80112E7C`, `0x80113178`, `0x801132CC`,
`0x80113754`, and `0x80116284`.

All seven target the true entry `0x80160D28`; there are no data-derived false
callers.
