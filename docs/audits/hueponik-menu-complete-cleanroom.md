# Hueponik performance-menu complete cleanroom contract

Date: 2026-08-28

## Result

The authenticated unified PAL patch has exactly six 8 MiB performance-menu
settings:

1. `FRAME RATE`
2. `SHIP LOD`
3. `SHIP DIST`
4. `TRACK 4X4`
5. `DRAW DIST`
6. `BIRDS`

There is no seventh menu setting. The 2 MiB path deliberately exposes only
four rows: Frame Rate, Ship LOD, Ship Distance, and Birds. The 8 MiB path
rewrites the menu loop bound from four to six and substitutes six label,
choice-count, choice-table, and publisher-descriptor entries.

The shipping fixed preset requested for the direct mod is:

| setting | direct target | authenticated 8 MiB default |
| --- | --- | --- |
| Frame Rate | **100 Hz** | 50 Hz |
| Ship LOD | **Max** | Max |
| Ship Distance | **65K** | 65K |
| Track 4x4 | **Off / excluded** | On |
| Draw Distance | **Far** | Far |
| Birds | **High** | High |

The authentic 8 MiB default for Track 4x4 is therefore not acceptable for the
requested release. It must not be inherited accidentally.

This audit defines behavior, not an installation method. The direct release
must leave authenticated stock executable RAM and generated AOT identical. In
particular, no `[[recompiler.patch]]` implementation is acceptable for Ship
LOD, Ship Distance, or Birds: generated replacement words on stock guarded RAM
can fall back to an interpreter that executes the stock RAM words. These
settings need native instruction-site semantics just like PAL100.

## Reproducible evidence

The comparison inputs are:

- stock SCES-02845 executable SHA-256
  `0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`;
- authenticated Hueponik unified PAL executable SHA-256
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`;
- Hueponik repository commit
  `ac46a5dc500a9581ea55b8efb5c1a0d4be944aba`; and
- its primary-source README, which identifies automatic 2/8 MiB detection,
  separate saved settings, and the requirement for PAL-x2 timing at 100 Hz.

Run:

```text
py -3 tools/verify_hueponik_menu_contract.py
```

The verifier decodes both menu variants directly from the authenticated
patched executable. It checks every label, choice, default, halfword
publisher, selector store, and consumer word. It also proves that Track 4x4
publishes only replacement-engine state.

## Runtime menu storage and defaults

The resident menu state begins at `0x8017B8E4`. The option indices begin at
`0x8017B8EC`.

### 8 MiB layout

| address | setting | authentic initial index | authentic initial value |
| --- | --- | ---: | --- |
| `8017B8EC` | Frame Rate | 1 | 50 Hz |
| `8017B8F0` | Ship LOD | 21 | Max |
| `8017B8F4` | Ship Distance | 4 | 65K |
| `8017B8F8` | Track 4x4 | 1 | On |
| `8017B8FC` | Draw Distance | 4 | Far |
| `8017B900` | Birds | 2 | High |

The high-memory option data is relocated separately from its code. The menu
reads:

| table | runtime address | entries |
| --- | --- | ---: |
| labels | `80781E90` | 6 |
| choice-table pointers | `80781EA8` | 6 |
| choice counts | `80781EC0` | 6 |
| publisher-descriptor pointers | `80781ED8` | 6 |

Frame Rate has a null publisher descriptor because it has special post-menu
publication. The other five rows use the generic descriptor interpreter.

### 2 MiB layout

| address | setting | authentic initial index | authentic initial value |
| --- | --- | ---: | --- |
| `8017B8EC` | Frame Rate | 1 | 50 Hz |
| `8017B8F0` | Ship LOD | 1 | Stock |
| `8017B8F4` | Ship Distance | 0 | Stock |
| `8017B8F8` | Birds | 1 | Stock |

The 2 MiB tables are at `0x8017BC48` (labels), `0x8017BC38`
(choice-table pointers), `0x8017BC28` (counts), and `0x8017BC18`
(publisher-descriptor pointers).

The source README says the 2 MiB and 8 MiB settings are saved independently
on memory card. That persistence is menu infrastructure, not a semantic
consumer of the selected values. A fixed direct preset has no mutable menu
state to serialize and must not port or extend Hueponik's memory-card hooks.
If a later release exposes native choices, it should store them in the host
configuration namespace rather than changing the title's card format.

## Generic publisher descriptor

For every row except Frame Rate, the menu uses this exact descriptor shape:

```text
uint32 write_count
uint32 option_stride_bytes
uint32 values_base
uint32 destination[write_count]
```

For selected index `s`, it copies `write_count` consecutive halfwords from
`values_base + s*option_stride_bytes` to the listed destinations. Several
destinations are the low and high halfwords of live MIPS instructions. This is
why copying the menu's writes into generated AOT is not a valid direct port.
The direct implementation must model the selected instruction at its stock
instruction site while stock RAM stays unchanged.

## Frame Rate

### Choices and publication

Both memory modes expose:

| index `s` | label | `gSimVblankSkip` | Hueponik PAL-x2 boolean `p` |
| ---: | --- | ---: | ---: |
| 0 | 25HZ | 2 | 0 |
| 1 | 50HZ | 1 | 0 |
| 2 | 100HZ | 1 | 1 |

The post-menu publisher computes:

```text
gSimVblankSkip = (s < 1) + 1
p               = !(s < 2)
```

In the 8 MiB menu those stores are the words at `0x8017BB70` and
`0x8017BB80`; in the 2 MiB menu they are at `0x8017BB9C` and
`0x8017BBAC`. The 8 MiB menu is entered from the patched call at
`0x80123EF8`, and its controller aggregator replaces the stock call at
`0x80134740`.

`p` is not a stock global. It drives Hueponik's complete added timing graph.
Selector 2 is therefore defined by all of that graph plus PAL-x2 device
timing, not by the two stores alone. The separate PAL100 cleanroom audit owns
the consumer formulas and native hooks.

## Ship LOD

### Choices

The 8 MiB descriptor is at `0x80781D1C`, has two halfword destinations and a
four-byte option stride. Both destinations are immediates in stock owner
`func_801306E8`:

- `0x80130A20`, stock `lui a0,0x0015`;
- `0x80130A24`, stock `ori a0,a0,0xF8FF`.

Together they publish a signed high-detail squared-distance threshold. The
comparison at `0x80130A28` is `slt a0,a0,s1`. Ordinary numeric choices are
exactly `distance^2 - 1`. `0xFFFFFFFF` (label `0`) is signed `-1`, so high LOD
is never selected; `0x7FFFFFFF` (Max) keeps high LOD selected for every normal
distance.

| index | label | exact threshold |
| ---: | --- | ---: |
| 0 | 0 | `FFFFFFFF` |
| 1 | 600 | `00057E3F` |
| 2 | STOCK | `0015F8FF` (1200²−1) |
| 3 | 1800 | `0031703F` |
| 4 | 2400 | `0057E3FF` |
| 5 | 3000 | `0089543F` |
| 6 | 3600 | `00C5C0FF` |
| 7 | 4200 | `010D2A3F` |
| 8 | 4800 | `015F8FFF` |
| 9 | 5400 | `01BCF23F` |
| 10 | 6000 | `022550FF` |
| 11 | 6600 | `0298AC3F` |
| 12 | 7200 | `031703FF` |
| 13 | 7800 | `03A0583F` |
| 14 | 8400 | `0434A8FF` |
| 15 | 9000 | `04D3F63F` |
| 16 | 9600 | `057E3FFF` |
| 17 | 10200 | `0633863F` |
| 18 | 10800 | `06F3C8FF` |
| 19 | 11400 | `07BF083F` |
| 20 | 12000 | `089543FF` |
| 21 | MAX | `7FFFFFFF` |

The 2 MiB descriptor at `0x8017BCD8` exposes the subset `0`, `STOCK`,
`2400`, `3600`, `4800`, `6000`, `7200`, `9600`, `12000`, `MAX`.

### Direct boundary

One native instruction-site callback at `0x80130A20` can publish the selected
32-bit threshold into `a0` and continue at `0x80130A28`, thereby modeling both
stock constant-construction words without changing RAM. The target fixed
value is `0x7FFFFFFF`.

## Ship Distance

### Consumers

The 8 MiB descriptor at `0x80781D80` writes eight halfwords with a 16-byte
option stride. They control six semantic instruction sites in two stock
owners:

| stock site(s) | selected quantity |
| --- | --- |
| `8013085C`, `8013086C` | full squared-distance threshold in `s1` |
| `801308E4` | signed `abs(x) < radius` bound |
| `801308FC` | signed `abs(y) < radius` bound |
| `80130A0C`, `80130A10` | full squared-distance threshold in `v0` |
| `80152974`, `80152978` | full squared-distance threshold in `v0` |

The menu writes eight halfwords because the three 32-bit constants each use a
LUI/ORI pair. A direct plugin can collapse each adjacent pair to one native
site, but it must preserve the two separated `s1` construction sites or skip
the intervening stock code with an explicitly modeled block.

### 8 MiB choices

| index | label | radius | primary square | `s1` square |
| ---: | --- | ---: | ---: | ---: |
| 0 | STOCK | 3001 (`0BB9`) | `0080538F` (2900²−1) | `00895440` (3000²) |
| 1 | 24K | 3000 (`0BB8`) | `00895440` (3000²) | `7FFFFFFF` |
| 2 | 40K | 5000 (`1388`) | `017D7840` (5000²) | `7FFFFFFF` |
| 3 | 52K | 6500 (`1964`) | `0284AF10` (6500²) | `7FFFFFFF` |
| 4 | 65K | 8191 (`1FFF`) | `03FFC001` (8191²) | `7FFFFFFF` |

The menu label is approximately `radius * 8` in title units. The primary
square is published at both `0x80130A0C` and `0x80152974`; the `s1` square is
published at `0x8013085C`. Both signed radius sites receive the same radius.

### 2 MiB choices

| index | label | radius | primary square | `s1` square |
| ---: | --- | ---: | ---: | ---: |
| 0 | STOCK | 3001 | `0080538F` | `00895440` |
| 1 | 26K | 3250 | `00A12BC4` | `7FFFFFFF` |
| 2 | 29K | 3625 | `00C88291` | `7FFFFFFF` |
| 3 | 32K | 4000 | `00F42400` | `7FFFFFFF` |

### Direct boundary

Use native instruction-site callbacks at the six semantic sites above. The
fixed 65K route publishes radius `8191`, primary square `0x03FFC001`, and
`s1 = 0x7FFFFFFF`. No generated word or guest executable halfword may change.

## Track 4x4 — explicitly non-shipping

The 8 MiB descriptor is at `0x80781DBC`. It has no stock-title destination:

| value | `805FF7C4/C6` | `805FF7D4/D6` |
| --- | ---: | ---: |
| Off | `00000000` | `00000001` |
| On | `00000001` | `00000001` |

All four addresses belong to Hueponik's replacement high-RAM engine.
`0x805FF7C4` controls its 4x4/track-list interpretation and
`0x805FF7D4` requests a rebuild. There is no 2 MiB Track 4x4 row.

The direct release must:

- expose no Track 4x4 feature;
- allocate no substitute state for these addresses;
- never read or write `0x805FF7C4..0x805FF7D6`; and
- keep the Draw Distance direct plugin independent of this flag.

This is stronger than selecting Off in the old engine: the old engine is not
present, so there can be no Track 4x4 code or state leakage.

## Draw Distance

The 8 MiB descriptor at `0x80781E3C` writes five 32-bit fields as ten
halfwords. The replacement list builder also initializes an unexposed fixed
vertical limit of `32766` (`0x7FFE`).

| index | label | longitudinal | lateral | max-planar | mode | rebuild |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | LOW | 16000 | 14000 | 12000 | 2 | 1 |
| 1 | STOCK | 40000 | 34000 | 32000 | 1 | 1 |
| 2 | NEAR | 40000 | 34000 | 32000 | 0 | 1 |
| 3 | MID | 56000 | 48000 | 44000 | 0 | 1 |
| 4 | FAR | 65535 | 65535 | 65535 | 0 | 1 |

The three geometric tests are:

```text
longitudinal <= longitudinal_limit
lateral      <= lateral_limit
max(longitudinal, lateral) <= max_planar_limit
abs(deltaY)  <= 32766
```

Mode is semantically important:

- `2` (Low) rebuilds geometry-only lists and does not union stock-authored
  exceptional entries;
- `1` (Stock) restores stock lists and does no geometric augmentation; and
- `0` (Near/Mid/Far) unions the geometric result with the stock list.

The direct selector at stock owner return `0x801422F4` implements all five
profiles. Stock executes the original return unchanged. Near/Mid/Far union
geometry into the stock mask. Low replaces current-generation view bits with
the geometry-only result, so stock-only entries are intentionally omitted.
The release default remains Far.

## Birds

The 8 MiB descriptor at `0x80781E7C` and 2 MiB descriptor at `0x8017BC58`
both replace the complete word at `0x800FA224` by writing its two halfwords:

| index | label | exact modeled instruction | resulting `v0` |
| ---: | --- | --- | ---: |
| 0 | LOW | `lui v0,0` | `00000000` |
| 1 | STOCK | `ori v0,zero,0x3000` | `00003000` |
| 2 | HIGH | `lui v0,0x7FFF` | `7FFF0000` |

The authenticated stock instruction at that site is `lw v0,0x10(s0)`, but
Hueponik's menu label `STOCK` deliberately publishes the constant `0x3000`.
A faithful direct selector must reproduce the table above rather than infer
that `STOCK` means executing the original load.

The value is the first threshold in stock owner `func_800FA110`. High makes
the normal positive comparison choose the high bird-detail path; Low makes it
fall through to the second threshold/disable logic.

The direct fixed High boundary is one native instruction-site callback at
`0x800FA224`: set `v0 = 0x7FFF0000`, continue at `0x800FA228`, and leave the
stock instruction word in RAM and AOT.

## Menu and input detours are not part of the direct release

Hueponik's 2 MiB menu replaces the stock call at `0x80123EF8` with a call to
`0x8017B90C`. The 8 MiB patch moves that entry to `0x8017B920`. Its input
wrapper replaces the stock `jal 0x8011BD4C` at `0x80134740` with a call to
`0x8017BBA4`, then calls the stock owner internally. Start+Select toggles the
menu; direction presses cycle six rows and their choices.

The requested direct release explicitly does not ship this overlay/menu
engine. Therefore it should not port those two detours, its drawing strings,
controller aggregation, or card persistence. It should port only the selected
semantic effects through named native features.

## Implementation clusters and current status

| priority | cluster | exact direct boundary | status at audit time |
| ---: | --- | --- | --- |
| 1 | Frame Rate 100 | complete selector-2 timing graph + PAL-x2 host cadence | active PAL100 recovery; earlier hybrid rejected |
| 2 | Ship LOD Max | native block at `80130A20`, continue `80130A28` | current AOT replacement rejected; native migration required |
| 2 | Ship Distance 65K | six native semantic sites listed above | current AOT replacements rejected; native migration required |
| 2 | Birds High | native site `800FA224`, continue `800FA228` | current AOT replacement rejected; native migration required |
| 3 | Draw Distance | stock return site `801422F4` | all five choices implemented; default Far |
| excluded | Track 4x4 | no code, no state, no feature | must remain absent |

The selected target settings can share one small native visual-detail plugin,
but each callback must have a stock expected-word guard, activation gating,
exact register/PC semantics, and a focused unit test. The final static verifier
must reject any `[[recompiler.patch]]` at these sites and any reference to
Hueponik high RAM.

## Acceptance requirements

Static acceptance:

- the stock executable digest remains the authenticated stock digest;
- generated AOT and guarded RAM contain the same stock words;
- the instruction-site list contains every selected semantic site;
- no `0x805F/0x8060/0x8078` replacement-engine dependency remains;
- Track 4x4 addresses do not appear in shipping code; and
- the menu-contract verifier passes.

Live acceptance:

- visible ship detail matches Max rather than merely changing a counter;
- distant ships remain present at the recovered 65K boundary;
- Birds High takes the high branch in a scene that exercises birds;
- Far scene masks match the independent captured-state proof;
- Start+Select opens only the stock pause menu; and
- visual-detail callbacks generate no dirty-interpreter traffic.

Frame-rate acceptance remains separate and stronger: race clock versus wall
clock, true 100 updates/s, FMV/CDDA/SFX real time, and no audio faults.
