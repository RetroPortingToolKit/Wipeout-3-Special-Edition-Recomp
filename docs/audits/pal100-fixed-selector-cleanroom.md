# PAL100 fixed-selector cleanroom map

Date: 2026-08-28

## Result

The PAL100 timing behavior can be implemented directly against the stock
SCES-02845 executable without loading Hueponik's code island or high-RAM
engine at runtime. It is not equivalent to setting `gSimVblankSkip = 1`.
The exhaustive fixed-rate port has four parts:

1. guarded instruction-site replacements for the stock frame-rate arithmetic;
2. fixed `PAL-x2 = 1` arithmetic at the stock motion and loop sites; and
3. native replacements for the scheduler initialization block at
   `0x801234F0` and the two shared high-rate blocks at `0x800F15DC` and
   `0x800F1D18`; and
4. all 41 authenticated direct scheduler-state substitutions plus the omitted
   formula/owner blocks recovered by a full stock-to-Hue executable scan.

The final verifier classifies all 146 changed words below the Hue code island:
138 are timing-owned and mapped to a guarded native owner; the remaining eight
are explicitly identified non-timing loader, 8 MB, menu, or visual changes.
There are zero unclassified changed words and 100 PAL instruction sites. The
two-site reduction is the atomic iteration-owner correction described below;
it does not reduce authenticated timing-word coverage.

For a fixed selector-2 build, the title's two stock initialization paths also
publish the selector's normal every-VBlank simulation cadence once: change the
`2` loaded immediately before the stores at `0x800EE2A4` and `0x800EEC00` to
`1`. This is not the rejected VBlank-time RAM forcing; the stock title remains
the owner and its temporary swap/restore path stays intact.

The authenticated comparison inputs were:

- stock executable SHA-256
  `0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`;
- Hueponik PAL executable SHA-256
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`;
- the corresponding generated trees in this worktree and the canonical
  sibling checkout; and
- the authenticated title contract in
  `tools/verify_pal100_title.py` from the canonical sibling checkout.

No overlay/island byte range is proposed for reuse.

## Important correction: ten consumers, not nine

`0x80175064` is track-name-table padding in the authenticated stock executable.
Hueponik repurposes that otherwise unused stock padding as a boolean PAL-x2
state in its patched executable; it is not a stock timing variable. Selector 2
publishes `p = 1` only in that patched layout. There are ten live reads of the
repurposed boolean in the patched executable:

| consumer | stock owner / source site | fixed `p = 1` meaning |
| --- | --- | --- |
| `80174A88` | owner `800E4578`, detour at `800E4644` | shift X delta right by one additional bit |
| `80174B08` | owner `800E4578`, detour at `800E4654` | shift Y delta right by one additional bit |
| `80174B88` | owner `800E4578`, detour at `800E4660` | use total shift `7 + p = 8` for Z delta |
| `80174FC8` | `RenderRaceFrame` `8012268C`, detour at `801234F0` | initialize PAL100 scheduler constants |
| `801750CC` | owner `80119138`, detour at `801192E4` | double the per-iteration quantity and remove stock sign extension |
| `80175184` | internal continuation of `801234F0` | publish the phase-wrap flag directly |
| `801751C4` | internal continuation of `801234F0` | use phase modulus `2 << p = 4` |
| `80175448` | internal continuation of `801234F0` | set `5068 = rate`, `5434 = 0` |
| `80175608` | owner `800F04E8`, detours at `800F15DC` and `800F1D18` | always take the high-rate shared path |
| `80175684` | internal continuation of `801234F0` | set `5070 = 73`, `5430 = 2*rate + 8` |

The tenth read at `0x80175684` was absent from the previous nine-site list.
The word resembling a store at `0x800D30CC` is relocation-table data, not a
live publisher. The live publisher is `0x8017BBAC` in
`UpdatePerformanceTuningMenu`.

Six of the ten consumers are internal pieces of one newly introduced
scheduler initialization chain. Their stock semantic owner is therefore the
single `RenderRaceFrame` block at `0x801234F0`; they are not independent stock
functions and must not be exposed as callable ABI entries.

## Exact guarded stock replacements

These replacements do not need Hueponik's native helper. They are implemented
as trusted instruction-site callbacks while guest executable RAM remains stock.
Generated-only AOT substitutions are invalid here: the runtime text guard sees
the stock executable, marks the differing generated page dirty, and sends that
page through the interpreter, where the stock word executes. That produces a
hybrid of absent word replacements and still-active callbacks.

The two replacements at `0x801234C8` and `0x801234FC` are branch delay slots.
Their direct implementation hooks the owning branches at `0x801234C4` and
`0x801234F8`, applies the replacement delay-slot result, and redirects to the
exact taken/fall-through continuation.

### Fixed selector publication

| address | expected stock | fixed PAL100 | meaning |
| --- | --- | --- | --- |
| `800EE290` | `24040002` | `24040001` | first stock initializer publishes every-VBlank simulation |
| `800EEBF0` | `24030002` | `24030001` | second stock initializer publishes every-VBlank simulation |

### Fixed frame-rate arithmetic, owner `0x800E98CC`

| address | expected stock | fixed PAL100 | meaning |
| --- | --- | --- | --- |
| `800E99C4` | `240200C8` | `24020190` | constant 200 -> 400 |
| `800E99C8` | `00031843` | `00000000` | remove `v1 >>= 1` |
| `800E99D8` | `24030002` | `24030001` | multiplier 2 -> 1 |
| `800E9A04` | `00052843` | `000528C3` | final signed shift 1 -> 3 |

The resulting fixed expression is the authenticated patched expression, not
a guessed host-speed multiplier:

```text
x = 400 - state_110
term = x*x*(-5)
result = 0x6400 - ((x << 8) + (term >> 3))
```

The direct implementation owns `0x800E99C4..0x800E99CC` atomically at the
`0x800E99C4` callback. The preceding `lw` has already published its value to
`gpr[3]` before the generated hook; combining the patched constant, patched
`nop`, and stock subtraction avoids two AOT exits inside this load/use owner.

### Frame counter bounds, `RenderRaceFrame` owner `0x8012268C`

| address | expected stock | fixed PAL100 |
| --- | --- | --- |
| `801234C0` | `28420002` | `28420001` |
| `801234C8` | `24020002` | `24020001` |
| `801234DC` | `28420007` | `28420003` |
| `801234E8` | `24020006` | `24020002` |
| `801234FC` | `24020002` | `24020001` |

This changes the live counter range from stock `2..6` to `1..2` and resets it
to 1 on the `-1` event.

### Motion integration, owner `0x800E4578`

The island first performs the stock shift by 7 and then shifts once more by
`p`. With `p = 1`, the same result fits at the existing stock shift sites:

| address | expected stock | fixed PAL100 |
| --- | --- | --- |
| `800E4640` | `000A51C3` (`sra t2,t2,7`) | `000A5203` (`sra t2,t2,8`) |
| `800E4650` | `000B59C3` (`sra t3,t3,7`) | `000B5A03` (`sra t3,t3,8`) |
| `800E4660` | `000529C3` (`sra a1,a1,7`) | `00052A03` (`sra a1,a1,8`) |

The stock add/store instructions at `0x800E4644..0x800E466C` remain intact.

### Iteration scaling, owner `0x80119138`

The `0x801750C8` island block is implemented by one guarded callback at the
owner site `0x801192E4`. The callback validates the stock words through
`0x801192F0`, then performs the complete semantic unit for `p = 1`:

```text
s4 = s4 + 1
v0 = v0 << 1
a0 = sign_extend_16(load16(s5 + 0x73D0))
a0 = (signed(s4) < signed(a0))
store16(s1 + 0x146, v0)       # branch delay slot
continue at a0 ? 0x80119210 : 0x80119300
```

This intentionally matches the patched 32-bit increment rather than restoring
the stock signed-16 extension. It must not be split into callbacks at
`0x801192E8` or `0x801192F0`: neither address nor their successor PCs are
re-enterable compiled continuations. A nonzero callback return there leaves the
owning AOT block and redispatches through the interpreter, so the later callback
is not guaranteed to run and the three-site implementation is not atomic.

The live bisection confirmed that contract. With all other PAL callbacks held
constant, either `0x801192E8` alone or `0x801192F0` alone reproduced the
`FFFFFFFF` indirect-call fatal. Removing the split group survived, and the
single `0x801192E4` owner callback survived moving gameplay while preserving the
island arithmetic. The only callback exits are the existing stable block
continuations `0x80119210` and `0x80119300`.

## Fixed scheduler formulas

Let:

```text
p     = 1
rate  = uint16[0x80173AF8]       # clamped to 1..2 above
K     = 0x4000 << (rate - 1)
scale(x) = (x*K + 0x8000) >> 16
phase_mod = 4
race_phase_mod = 2
```

The native replacement at stock block `0x801234F0` must perform the following
state update before continuing at the stock instruction `0x801234F8`:

```text
uint16[0x801750A4] = rate
uint32[0x80173AFC] = 2
uint32[0x801750A8] = K
uint32[0x801750AC] = uint32[0x801756C4 + 4*(2 + rate - 1)]

race_total = native_race_phase + rate
native_race_delta = race_total >> 1
native_race_phase = race_total & 1

new_phase = uint32[0x801750B0] + rate
wrap = (new_phase >= 4)
uint32[0x801750B0] = new_phase & 3
uint32[0x801750B4] = wrap
uint32[0x8017506C] = wrap
uint32[0x80175068] = rate
uint32[0x80175434] = 0
uint32[0x80175070] = 73
uint32[0x80175430] = 2*rate + 8
```

When `wrap` is true, the patched chain also increments its private random
counter at `0x80175774`. The detours at `0x800E4880/0x800E4894` combine that
counter with stock word `0x80173A8C`. A direct implementation must keep the
counter as native scheduler state; `0x80175774` is replacement-engine storage,
not stock state, and must not be read or written by the direct port.

The table access is recorded as a formula because the PAL patch has values
`{0x1111,0x1000,0x0862,0x081E}` at `0x801756C4`. The island decrements the raw
rate at `0x80174FE0` before the index at `0x801753C4`, so the PAL100 route uses
index `2 + (rate - 1)`: rate 1 selects `0x0862`, rate 2 selects `0x081E`.
The known-good full-Hueponik PAL100 savestate independently records
`selector=1`, `rate=1`, `K=0x4000`, and `coefficient=0x0862`. A direct
implementation should own these named constants, not copy the island table.

The scheduler's phase-wrap flag drives less-frequent work. The other derived
formulas used at stock sites are:

```text
coefficient(x) = (x * uint32[0x801750AC]) >> 16
rate_scale(x)  = scale(x)
loop_scale(x)  = (x * rate) >> 2
race_delta_cs  = 2 * native_race_delta
```

### Race-clock owner correction

The authenticated patched executable also changes `0x8010D8E4` from stock
`lw v1,0x3AF8(v0)` (`8C433AF8`) to `lw v1,0x56F4(v0)` (`8C4356F4`). The
following stock instruction shifts the value left once and publishes it at
`0x801CE214`; `0x8010DECC` later adds that delta to each racer's total time.
For fixed selector 1/rate 1, the separate divide-by-two accumulator above
produces `0,1,0,1...`, hence published race deltas `0,2,0,2...`: exactly 100
centiseconds across 100 PAL100 VBlanks. Loading raw rate 1 here instead would
publish `2,2,2,2...`, advancing the visible race timer at 200 centiseconds per
wall second. The direct mod therefore owns this stock site as an 18th guarded
instruction replacement; it does not expose or write Hueponik's `0x56F4` RAM.

## Native interior-block replacements

Two paths do not fit as safe one-word changes and should be implemented as
native internal-block replacements with explicit continuation PCs:

1. `0x801234F0` in `RenderRaceFrame`: apply the scheduler formulas above,
   restore `v1 = [0x80173AE8]` and `v0 = -1`, then continue at `0x801234F8`.
2. `0x800F15DC` and `0x800F1D18` in owner `0x800F04E8`: for fixed `p = 1`,
   both always enter the shared high-rate path. Load the record selected by
   `uint8[s0+0x15E]` from the 16-byte-stride table at `0x8017BBD0`, preserve
   the two signed halfwords at `s0+0x128` and `s0+0x122`, and continue through
   the semantic equivalent of the patched shared block. Both fixed-rate paths
   converge at stock continuation `0x800F1D5C`; they do not return to the two
   instructions that the detours replaced.

The shared-path algorithm is bounded and can be translated without retaining
the island:

```text
record = 0x8017BBD0 + 16*uint8[s0+0x15E]
packed = uint16[s0+0x128] | (uint16[s0+0x122] << 16)
if packed != record.word[2]:
    record.word[0] = t2
    record.word[1] = t4
    record.word[2] = packed

repeat rate times:
    record.word[0] = mix(record.word[0], 0xD745, int16[s0+0x124], 0x0146)
    record.word[1] = mix2(record.word[1], 0xF798,
                         record.word[0], 0x2CD9,
                         int16[s0+0x124], 0x0070)

uint16[s0+0x128] = func_8014B6A4(record.word[0], 16)
uint16[s0+0x122] = func_8014B6A4(record.word[1], 16)
record.word[2] = uint16[s0+0x128] | (uint16[s0+0x122] << 16)
continue at 0x800F1D5C
```

`mix` and `mix2` must preserve the exact MIPS `mult`/HI/LO recombination in
the authenticated blocks `0x80175700..0x80175734` and
`0x80175500..0x80175534`; using ordinary 32-bit multiplication without that
recombination is not equivalent.

Replacing the whole `0x800F04E8` owner would be unnecessarily broad; it is a
`0x1D90`-byte function. Interior-block hooks are the bounded unit.

## Original timing-detour subset and stock owners

This is the original 39 external-detour subset. It was useful but not a
complete timing surface: the authenticated executable also has direct
scheduler-state load substitutions and inline formula blocks listed below.
Relocation-table words and island-internal jumps are excluded. `expected` is
the stock word; `patched` is evidence only and is not proposed for runtime use.

| stock site | stock owner | expected | authenticated patched detour |
| --- | --- | --- | --- |
| `800E2D40` | `800E2D00` | `898A0007` | `0805D491` -> `80175244` |
| `800E2F38` | `800E2D00` | `00641821` | `0805D481` -> `80175204` |
| `800E2FBC` | `800E2D00` | `08038BFA` | `0805D4A1` -> `80175284` |
| `800E2FE4` | `800E2D00` | `00621021` | `0805D4A3` -> `8017528C` |
| `800E37F4` | `800E30FC` | `00021383` | `0C05D401` -> `80175004` |
| `800E3860` | `800E30FC` | `00021383` | `0C05D401` -> `80175004` |
| `800E4580` | `800E4578` | `08039163` | `0805D4C8` -> `80175320` |
| `800E45FC` | `800E4578` | `00021103` | `0805D2B2` -> `80174AC8` |
| `800E460C` | `800E4578` | `00031903` | `0805D2D2` -> `80174B48` |
| `800E4628` | `800E4578` | `00094903` | `0805D3D3` -> `80174F4C` |
| `800E4644` | `800E4578` | `004A1021` | `0805D2A1` -> `80174A84` |
| `800E4654` | `800E4578` | `006B1821` | `0805D2C1` -> `80174B04` |
| `800E4660` | `800E4578` | `000529C3` | `0805D2E1` -> `80174B84` |
| `800E4880` | `800E4814` | `0C041292` | `0C05D501` -> `80175404` |
| `800E4894` | `800E4814` | `0C041292` | `0C05D501` -> `80175404` |
| `800F15DC` | `800F04E8` | `8E633AFC` | `0805D581` -> `80175604` |
| `800F1D18` | `800F04E8` | `96030128` | `0805D581` -> `80175604` |
| `8010D8E4` | race-clock delta owner | `8C433AF8` | `8C4356F4` -> private half-phase quotient |
| `801192E4` | `80119138` | `26830001` | `0805D432` -> `801750C8` |
| `80119FC4` | `80119D50` | `8602013C` | `0C05D591` -> `80175644` |
| `8011A010` | `80119D50` | `8602013C` | `0C05D591` -> `80175644` |
| `8011A040` | `80119D50` | `08046816` | `0805D598` -> `80175660` |
| `8011A048` | `80119D50` | `9602013C` | `0C05D591` -> `80175644` |
| `8011C2DC` | `8011C2DC` | `8CA20000` | `0805D352` -> `80174D48` |
| `8011DD7C` | `8011DD7C` | `27BDFFC8` | `0805D560` -> `80175580` |
| `801234F0` | `8012268C` | `8CA33AE8` | `0805D3F1` -> `80174FC4` |
| `80124AE8` | `80124A58` | `00431021` | `0805D421` -> `80175084` |
| `801396CC` | `801396CC` | `3C02801E` | `0805D301` -> `80174C04` |
| `8013A600` | `8013A5C4` | `00431021` | `0805D3E1` -> `80174F84` |
| `8013BED0` | `8013BDEC` | `00431021` | `0805D311` -> `80174C44` |
| `8013BED8` | `8013BDEC` | `00852021` | `0805D321` -> `80174C84` |
| `8013BEE0` | `8013BDEC` | `8E240018` | `0805D441` -> `80175104` |
| `8013BEF0` | `8013BDEC` | `2442FFFF` | `0805D3C1` -> `80174F04` |
| `80154088` | `80153C1C` | `24420080` | `0805D4C1` -> `80175304` |
| `80159070` | `80158FA4` | `00651823` | `0805D391` -> `80174E44` |
| `80159078` | `80158FA4` | `01435021` | `0805D361` -> `80174D84` |
| `8015908C` | `80158FA4` | `00852023` | `0805D3A1` -> `80174E84` |
| `80159094` | `80158FA4` | `01044021` | `0805D371` -> `80174DC4` |
| `801590A4` | `80158FA4` | `00022983` | `0805D3B2` -> `80174EC8` |
| `801590AC` | `80158FA4` | `01224821` | `0805D382` -> `80174E08` |

The delay slots also differ at these sites and must be included in any native
block contract: `800E37F8`, `800E3864`, `800E4584`, `800E4600`, `800E4610`,
`800E462C`, `800E4648`, `800E4658`, `800E4664`, `800E4884`, `800E4898`,
`800F15E0`, `800F1D1C`, `801192E8`, `80119FC8`, `8011A014`, `8011A044`,
`8011A04C`, `8011DD80`, `801396D0`, and `801590A8`.

## Formula classes for the detour inventory

The 39 sites reduce to a small semantic API:

- owners `800E2D00`, `800E30FC`, `800E4578` at `800E4580`, `8011C2DC`,
  `8011DD7C`, `80124A58`, `801396CC`, and `8013BDEC` at `8013BEF0` use the
  scheduler `wrap` flag to execute or suppress temporal work;
- owner `800E4578` at `800E45FC/460C/4628` uses
  `(x * coefficient) >> 16`;
- owners `8013A5C4`, `8013BDEC`, `80153C1C`, and `80158FA4` use
  `scale(x) = (x*K + 0x8000) >> 16` (the `80154088` specialization adds
  `K >> 9`);
- owner `80119D50` uses `(x * rate) >> 2` at its four sites;
- owner `80119138` doubles the loop quantity; and
- owner `800F04E8` uses the shared high-rate path unconditionally.

This grouping is the direct-mod boundary. Recreating the 39 jumps would only
repackage the hack; implementing these named operations in stock owners or
native block hooks is a semantic port.

## Exhaustive stock-to-Hue timing inventory

The final audit linearly compares authenticated stock SHA-256 `0213f529...`
against authenticated Hue SHA-256 `7004d140...` over
`0x800E0000..0x801749FC`. It does not infer completeness from the old detour
list. The 138 timing words reduce to these exact semantic groups:

| group | changed words | native owner sites | fixed-selector meaning |
| --- | ---: | ---: | --- |
| direct `3AF8 -> 56F4` loads | 40 | 40 | load the native divide-by-two quotient `0,1,0,1...` |
| direct `3AF8 -> 5434` load | 1 | 1 | load zero for fixed `p=1` |
| original detours/delay slots and exact reductions | 70 | 49 | wrap, coefficient, K scale, loop scale, mixer, bounds |
| omitted inline/owner blocks | 27 | 9 | wrap decrement, factor 73, event flag, signed shift, decay, reset |
| **timing total** | **138** | **99 diff owners** | every changed timing word has an owner |

The direct half-phase consumers are:

```text
800E2F0C 800E4718 800E4760 800E5110 800E73DC 800EFA30
800F0078 800F01B0 8010D8E4 8011A3A8 8011A984 801280AC
80128390 8014CD44 8014EA2C 801505BC 80150950 80151568
80151C44 80151C8C 8015242C 801531D0 801531F4 80153960
80153984 80153E34 80153E98 80153F38 80153F90 8015404C
80154A58 80154E64 801587C8 801589E0 80158CDC 80159224
80159B3C 8015AA88 8015BB74 8015BB98
```

All 40 use the runtime's non-exiting load-substitution action. The callback
supplies the native quotient in the original target GPR, then generated code
runs the stock load's timing, memory access, load-delay, PGXP, and cosim path
before substituting that value for the memory result.

At each address the authenticated replacement preserves the load opcode,
base, and destination register and changes only immediate `0x3AF8` to
`0x56F4`. The `0x800F1DA4` replacement is separately
`8E843AF8 -> 8E845434`; fixed `p=1` publishes zero at `0x5434`.

The newly recovered multiword groups are:

| native owner | authenticated changed words | exact semantic reduction |
| --- | --- | --- |
| `800E39D4` | `800E39D4` | subtract `a1` from `a0` only on scheduler wrap |
| `800E3BB4` | `800E3BB4..800E3BC8` | replace stock `77*rate` with `73*rate` |
| `800E484C` | `800E484C..800E4858` | consume the wrap-gated random event boolean |
| `80119FC4` | `80119FC4/FC8/FD0` | scaled signed loop value, comparison, and preserved `at` result |
| `8011A0BC/C4` | two words | `shift=mode-1`, signed arithmetic right shift |
| `8011A12C/134` | two words | second copy of the same signed formula |
| `8011A650` | `8011A650..8011A674` | on wrap subtract `0x26`; otherwise jump to `8011A700` |
| `8012BBF4` | `8012BBF4/8012BC04` | reset native random/event state while preserving stock store |

The eight explicitly excluded non-timing changes are `800E5CA8` and
`800E70E4` (data-pointer relocation), `800FA224` (birds detail), `80123EF8`
(Hue menu/engine call), `8014BAD0` and `8015FA80` (8 MB behavior), `80162474`
(startup loader), and `80166598` (loader/data pointer). The verifier fails if
either executable changes, a timing word loses an owner, or any ninth
unclassified change appears.

## Savestate continuity

The direct selector's host-owned scheduler is part of the machine timeline,
not derived cache. Savestates therefore carry a bounded `BS_SEC_MODSTATE`
record for the selected PAL plugin. Schema 1 serializes, in little-endian field
order, the activation/cadence boundary flags, all 16 `Wip3PAL100State` words,
and all 256 three-word high-rate records (3,144 payload bytes).

The runtime retains the resolved-mod fingerprint comparison and then
preflights the complete native-state provider set before applying CPU, RAM,
VRAM, or device sections. Provider ID, schema version, payload size, selector
invariants, phase bounds, and the exact enabled-provider set are fail-closed.
A pre-native-state checkpoint is refused explicitly instead of silently
resetting PAL phase and desynchronizing gameplay timing.

## Loading/progression negative evidence

The authenticated stock and PAL executables are byte-for-byte identical at:

- initialization stores `0x800EE2A4` and `0x800EEC00` to
  `gSimVblankSkip` (`0x8017F02C`);
- the `0x800EDFC8` VBlank/simulation gate, including the live load at
  `0x800EE00C`; and
- the temporary skip swap/restore owner at `0x800EED58`.

Hueponik does not patch any of those sites. Therefore a stock-title PAL-x2
stall is not evidence that another word change is missing there. The title
side progression change absent from a skip-only candidate is the shared
fixed-rate path rooted at `0x800F15DC/0x800F1D18`, plus the scheduler state it
consumes. This is the highest-priority native block to add after the inline
word table.

## Recommended implementation boundary

Use guarded instruction-site callbacks for the four arithmetic replacements,
five counter bounds, three motion shifts, three loop words, and two stock gate
initializers above. Implement a small native scheduler state object for `rate`,
`K`, `coefficient`, `phase`, and `wrap`. Replace only the three interior blocks
`0x801234F0`, `0x800F15DC`, and `0x800F1D18`, then migrate the remaining
formula-class sites owner by owner. Keep generated AOT and runtime executable
RAM identical so these hot title pages remain compiled.

Do not publish `0x80174A84..0x801756B0`, do not copy its constants as an
opaque byte range, and do not retain `0x80175064` as mutable menu state in the
fixed build. PAL100 activation should make `p = 1` an implementation constant;
the host PAL CRTC multiplier remains the separate device-cadence half of the
feature.

The direct implementation therefore never reads or writes `0x80175064`.
Its named scheduler state is native C state; only the stock-owned rate/mode
fields at `0x80173AF8..0x80173AFC` remain guest-visible.
