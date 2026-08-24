# Semantic audit: signed Q12 sine and cosine

## Result

The three tightly coupled routines at `0x8015C318`, `0x8015C354`, and
`0x8015C3E8` implement the game's 4096-unit-turn trigonometry:

| Address | Catalog name | Exact range | Role |
|---|---|---:|---|
| `0x8015C318` | `SinQ12FromSignedAngle` | `0x3C` | signed wrapper and odd symmetry |
| `0x8015C354` | `LookupSinQ12Phase` | `0x90` | private normalized sine lookup |
| `0x8015C3E8` | `CosQ12FromSignedAngle` | `0xA0` | signed-input, even cosine lookup |

One turn is `0x1000` angle units and the signed return scale is Q12: `0x1000`
represents one. Thus `0x400`, `0x800`, and `0xC00` are quarter-, half-, and
three-quarter-turn boundaries.

## Boundaries and ABI

`SinQ12FromSignedAngle` creates its own 24-byte frame at `0x8015C318`, saves
`ra`, calls only `0x8015C354`, restores its frame, and returns at
`0x8015C34C..0x8015C350`. The helper starts independently at `0x8015C354` and
returns at `0x8015C3DC..0x8015C3E0`; the NOP at `0x8015C3E4` is alignment
padding. `CosQ12FromSignedAngle` begins at `0x8015C3E8`, is a true leaf, and
returns at `0x8015C480..0x8015C484`. The unrelated GTE routine begins at
`0x8015C488`. These instructions prove the exact nonoverlapping ranges listed
above and agree with the generated range manifest.

All three accept their angle/phase in `a0` and return a signed integer in
`v0`. The sine helper's two direct calls are exactly `0x8015C324` and
`0x8015C338`, both inside the signed wrapper; it has no external caller.

## Sine normalization and quadrant folding

The wrapper takes the absolute magnitude of negative inputs with `negu`, masks
the magnitude with `0xFFF`, calls the helper, and negates its result again for
an originally negative input. In source-like form:

```c
int SinQ12FromSignedAngle(int angle) {
    if (angle < 0)
        return -LookupSinQ12Phase((-angle) & 0xFFF);
    return LookupSinQ12Phase(angle & 0xFFF);
}
```

The helper folds normalized phase `p` as follows, where `q[n]` is the table at
`0x8017A74C`:

```text
0x000..0x400 ->  q[p]
0x401..0x800 ->  q[0x800 - p]
0x801..0xC00 -> -q[p - 0x800]
0xC01..0xFFF -> -q[0x1000 - p]
```

## Cosine normalization and quadrant folding

Cosine first makes a negative input nonnegative, masks with `0xFFF`, and folds
the phase directly without calling the sine helper:

```text
0x000..0x400 ->  q[0x400 - p]
0x401..0x800 -> -q[p - 0x400]
0x801..0xC00 -> -q[0xC00 - p]
0xC01..0xFFF ->  q[p - 0xC00]
```

This proves even symmetry for signed inputs and is equivalent to a
quarter-turn-shifted sine lookup. Two's-complement `INT_MIN` remains `INT_MIN`
after `negu`, but the subsequent low-12-bit mask produces phase zero, so both
routines still return their phase-zero values deterministically.

## Quarter-wave table proof

The only coefficient source is 1025 consecutive signed halfwords beginning at
`0x8017A74C`, covering both endpoints `q[0]..q[1024]`. In the retained image:

- values are monotone nondecreasing from `0` to `4096`;
- representative entries are `q[256]=1567`, `q[512]=2896`,
  `q[768]=3784`, and `q[1024]=4096`;
- its exact 2050-byte SHA-256 is
  `bab3b71ba7f79dfb2b5cebb71f8382a79606a958d72031bc11b88066c36033bd`.

The endpoint values and quadrant address arithmetic establish Q12 sine/cosine
semantics independently of names or generated host code.

## Direct callers

An aligned little-endian opcode scan of the retained executable finds exactly
91 direct calls to sine:

```text
800E50F8 800E9D6C 800E9DD8 800E9F78 800E9FBC 800EA8A0 800EA8B0
800EA8C0 800EFDDC 800F0130 800F021C 800F0238 800F25B4 800F25E4
800F2614 800F5524 800FB2CC 800FB490 800FB638 800FB8BC 8010CA40
8010CD94 8011978C 8011D5D4 8011D628 8011D69C 8011D6F4 8011D76C
8011D7C4 8011D83C 8011D894 8011EA0C 8011EA38 80125910 80125BD8
8012DBA0 8012F1DC 8012F2B4 8012F844 8012FEA4 80135078 801350C8
80135114 801355CC 801355DC 801355EC 801357D0 801357E0 801357F0
801359D0 801359E0 801359F0 80135E28 80135E38 80135E48 80139BC4
80139BFC 80139EBC 80139EF4 80149640 80149678 80149724 8014975C
8014A028 8014A08C 8014A0F0 8014A148 8014A1B4 8014A220 8014A2AC
8014A310 8014A374 8014A3CC 8014A438 8014A4A4 8014C880 8014C8A8
80151748 801519BC 80152B7C 80153FF8 80157230 80157258 80157280
80157E10 80157E58 80157EA0 80157EE8 8015A664 8015A68C 80172F5C
```

The same scan finds exactly 81 direct calls to cosine:

```text
800E9DB0 800E9E00 800E9FA0 800E9FE4 800EA8D0 800EA8E0 800EA8F0
800EFDE8 800EFE10 800EFE1C 800F23E0 800F25CC 800F25FC 800F262C
800FB2EC 800FB4C0 800FB668 800FB8EC 8010E5AC 8010E5D8 801195F4
80119620 8011D5C8 8011D634 8011D68C 8011D704 8011D75C 8011D7D4
8011D82C 8011D8A4 8011EA00 8011EA44 8012593C 80125C08 8012DB94
80135088 801350D8 80135124 801355FC 8013560C 8013561C 80135800
80135810 80135820 80135A00 80135A10 80135A20 80135E58 80135E68
80135E78 80139BB0 80139BE0 80139EA8 80139ED8 80149634 80149684
80149718 80149768 8014A01C 8014A080 8014A0E4 8014A154 8014A1C0
8014A22C 8014A2A0 8014A304 8014A368 8014A3D8 8014A444 8014A4B0
8014C88C 8014C8BC 8014C8C8 80157E34 80157E7C 80157EC4 80157F0C
8015A670 8015A6A0 8015A6AC 80172F50
```

The paired calls at `0x80172F50` and `0x80172F5C` are independently identified
by the `0x80172E3C` audit as rotation terms used while transforming oriented
object corners, corroborating the trigonometric roles.

## Evidence

- `disc/SCES_028.45`, direct MIPS disassembly, table extraction, and exact JAL
  scans;
- `generated/SCES_028.45_full_132.c`, complete generated control flow for all
  three routines;
- `generated/SCES_028.45_full.ranges`, exact ranges;
- `docs/audits/semantic-80172e3c.md`, independent paired rotation-call
  semantics.
