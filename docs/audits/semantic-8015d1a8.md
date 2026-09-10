# Semantic audit: `0x8015D1A8` / `ResetGraph`

## Identity and boundary

`0x8015D1A8` is Psy-Q `ResetGraph`. The generated range table proves the exact
owner `[0x8015D1A8, 0x8015D240)` (`0x98` bytes), immediately before the already
identified `DrawSync` at `0x8015D240`. Its eight blocks start at `0x8015D1A8`,
`D1D4`, `D1F0`, `D1F8`, `D204`, `D218`, `D21C`, and `D22C`; the epilogue ends
at `0x8015D23C`.

## ABI and behavior

The recovered ABI is `int ResetGraph(int mode)`. The routine preserves `mode`
in `s0` and reads the signed graph-debug byte at `0x80178F4A`. At debug levels
2 or greater it calls the function pointer at `0x80178F44` with the fixed
library diagnostic string at `0x8016F074` and the requested mode.

For `mode == 0`, it first calls `FillBytes(0x80178FB4, -1, 20)` to initialize
the library's 20-byte GPU state record, then forms command `0x03000000`. A
nonzero mode skips the fill and forms `0x03000001`. In both cases it loads the
GPU backend object from `0x80178F40`, calls its function pointer at `+0x10`
with that command, and returns the backend callback's value left in `v0`.
There is no direct GPU MMIO in this wrapper; hardware effects belong to that
installed backend callback.

## Call graph

There are five generated calls and five unique guest call sites, all targeting
the owner entry:

- `0x800EE314`
- `0x800EE828` (`mode = 0`)
- `0x800EE8E0` (`mode = 0`)
- `0x801161AC` (`mode = 1`)
- `0x80118D04` (`mode = 1`)

The outgoing direct call is `FillBytes` at `0x8015F56C`; the debug reporter
and GPU command executor are indirect calls through `0x80178F44` and
`*(0x80178F40) + 0x10`, respectively. An exhaustive generated-source search
finds no direct call or jump to any interior block. The canonical identity is
also supported by its position beside `DrawSync`, its graph-debug gate, and
the mode-dependent GPU reset commands rather than inferred from proximity
alone.
