# Native intro skip: authenticated SCES-02845 control flow

## Result

`Always Skip Intro` replaces only the five direct JALs to the stock intro
presentation routine at `0x80115F34`. It leaves the earlier five STR file
lookups untouched, reproduces each JAL delay slot and link value, and does not
enable the runtime's global FMV auto-skip.

## Why the former sites hung

The calls at `0x801162CC`, `0x801162E0`, `0x801162F4`, `0x80116308`, and
`0x8011631C` target `0x80118EA4`. Despite the earlier working name, this
routine does not present a movie. It searches these files:

1. `\\PSYGLEED.STR;1`
2. `\\WO3.STR;1`
3. `\\PSYG.STR;1`
4. `\\CURLY44.STR;1`
5. `\\CURLY44B.STR;1`

For each file it stores the start sector in the table at `0x801D1B94` and the
end sector in the table at `0x801D1BA8`. The actual player routines later load
those values by `index * 4`. Bypassing the lookup calls therefore leaves the
player without valid sector ranges and accounts for the observed black hang.

## Authenticated presentation call set

Scanning the stock executable for `jal 0x80115F34` (`0x0C0457CD`) produces
exactly five direct calls:

| Call PC | Delay slot | Index | File |
|---|---|---:|---|
| `0x801163C0` | `addiu a0,zero,2` | 2 | `PSYG.STR` |
| `0x801163C8` | `move a0,zero` | 0 | `PSYGLEED.STR` |
| `0x801163D0` | `addiu a0,zero,1` | 1 | `WO3.STR` |
| `0x80100390` | `addiu a0,zero,3` | 3 | `CURLY44.STR` |
| `0x80100398` | `addiu a0,zero,4` | 4 | `CURLY44B.STR` |

The first three are the linear publisher/title sequence in main. The latter
two are the paired CURLY intro sequence. The return values are not consumed at
any of the five sites. Other race/event FMVs do not call this routine.

## Native replacement semantics

At each site the native callback first verifies both the stock JAL word and its
stock delay-slot word. When active, it:

1. writes the authenticated index to guest register `a0`;
2. writes `PC + 8` to guest register `ra`, as the skipped JAL would;
3. resumes at `PC + 8`, after the JAL and its delay slot.

If either word differs, the callback returns zero and the stock instruction
executes. No guest text, high RAM, file table, MDEC state, XA state, CDDA state,
or global runtime FMV policy is modified.

## Mechanical proof

Run:

```text
py -3 tools/verify_wipeout3_intro_skip.py
cmake --build build-direct-semantic100 --target wipeout3-intro-skip-test
ctest --test-dir build-direct-semantic100 -R wipeout3-intro-skip --output-on-failure
```

The verifier authenticates the stock executable hash, proves both complete JAL
sets, proves the loader calls are absent from `game.toml`, and checks package
wiring. The C test exercises inactive behavior, all five continuations,
delay-slot arguments, link values, and fail-closed instruction guards.
