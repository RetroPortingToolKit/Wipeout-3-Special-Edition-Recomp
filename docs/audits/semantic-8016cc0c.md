# Semantic audit: guest `0x8016CC0C`

## Result

`0x8016CC0C` is **data, not a callable function**. It begins a four-element
little-endian `uint16_t` table with the values `{150, 250, 300, 350}`. The
immediately following object at `0x8016CC14` is the NUL-terminated diagnostic
string `Autopilot jump speed [%d]`.

The former generated range at `0x8016CC0C` therefore coalesced a large data
section into supposed code. The prior AOT compiler supplied strong negative
evidence: `func_8016CC0C` contained only `psx_unknown_dispatch`, and the dispatch
table has no entry anywhere from `0x8016CC0C` through `0x8016F17F`.

## Exact code/data boundary

The preceding real callable is `0x8016C86C..0x8016CBFF` (length `0x394`). Its
last two instructions are:

```text
8016CBF8  jr     ra
8016CBFC  addiu  sp, sp, 0x30       # delay slot
```

Three explicit NOP words occupy `0x8016CC00..0x8016CC0B`. Data begins exactly
at `0x8016CC0C`:

```text
8016CC0C  96 00 FA 00 2C 01 5E 01   # 150, 250, 300, 350
8016CC14  41 75 74 6F 70 69 6C 6F   # "Autopilo..."
```

The authenticated PS-X EXE mapping is file offset
`0x800 + (0x8016CC0C - 0x80010000) = 0x15D40C`. The data seed continues across
many independently recognizable strings and tables, including animation,
cheat, track-loading, movie, menu, and music-name data. Treating its bytes as
MIPS is not a plausible alternate control-flow interpretation.

## Preceding callable: `PollMultitapControllerSlots`

`0x8016C86C` is a genuine address-taken function. The only retained literal
pointer to it is the fifth entry of the function table at `0x8017BE1C`:

```text
8017BE1C  8016C5DC
8017BE20  8016C624
8017BE24  8016C6FC
8017BE28  8016C7A8
8017BE2C  8016C86C
```

There is no direct `j` or `jal` to `0x8016C86C` in the executable. Its callable
ABI is consequently recovered from the body and the sibling SIO stages rather
than a direct call site:

- `$a0` is the private pad/SIO transaction descriptor and is preserved in
  `$s1`; fields used include the receive buffer at `+0x3C`, received-byte index
  at `+0x44`, and a transfer buffer at `+0x0C`.
- It returns signed status in `$v0`: zero on success, a negative status
  propagated from the byte-transfer helpers, or `-3` when a bounded poll times
  out.
- It has an ordinary 48-byte frame, saves `ra` and `s0..s5`, and returns only
  through the shared epilogue at `0x8016CBDC`.

The protocol evidence fixes its purpose to the multitap/controller polling
path. The preceding stage at `0x8016C7A8` recognizes a response whose first
byte has high nibble `8`; `0x8016C86C` then performs up to four transfers,
advancing per-slot storage by `0xF0` bytes. It uses the same SIO byte-exchange
helpers at `0x801698A8` and `0x80169AB8`, waits for the serial status transitions,
checks the controller protocol byte `0x22`, updates the descriptor's received
byte count, and copies the final byte into its receive buffer. The descriptive
name `PollMultitapControllerSlots` does not claim an original source identifier.

## Symbol decisions

- Add `[[func]] 0x8016C86C` as `PollMultitapControllerSlots`, status `known`.
- Add `[[object]] 0x8016CC0C` as `kAutopilotJumpSpeedValues`, status
  `confirmed`.
- Do not add a function or block symbol at `0x8016CC0C`.
- Do not count `R 8016CC0C 2574` as executable coverage. A future range cleanup
  should remove this seed rather than split it into invented functions.

## Evidence inspected

- `disc/SCES_028.45`: exact bytes and instruction words.
- `generated/SCES_028.45_full.ranges`: the erroneous `0x2574` seed and the
  correct preceding `0x394` range.
- `generated/SCES_028.45_full_140.c`: complete preceding function and the
  `psx_unknown_dispatch` placeholder.
- `generated/SCES_028.45_dispatch.c`: callable entries through
  `0x8016CBDC`, followed directly by `0x8016F180`.
