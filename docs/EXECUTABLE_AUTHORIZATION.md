# Executable authorization status

The current deterministic receipt is
`clean-aot/catalog/evidence/executable-authorization.json`. It is intentionally
`partial-fail-closed`: it is evidence for future catalog/native generation, not
a selectable shipping catalog and not proof of whole-game executable coverage.

## Authenticated inputs

- Prepared PS-X EXE SHA-256:
  `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`
- Main text SHA-256:
  `d483d38423965f7995e3d13fc919c887cf63c3f7f2b9d618859c80771488b0c3`
- NFE entry 5 expanded SHA-256:
  `3dc9945ce03c83229b8cec74369e743b205bda2ed95eb7a8de775b54c0f10810`
- Normalized route-capture receipt SHA-256:
  `53c475bccb58e888a7d0b2ba521d64eb345959d08c6a8958a741d78a1d2334c6`
- Exact deterministic route receipt SHA-256:
  `60f9ad4a7c17c87cb80dd3e5a6b79bf8558fbc519414d919a17a924657982b5c`

The NFE route variant is reconstructed deterministically from authenticated
entry-5 bytes and hash-named captured slices. It differs at exactly four words:
`0x800DF188`, `0x800DF18C`, `0x800DF204`, and `0x800DF208`.

## Current closure

| Image | Authorized instructions | Authorized bytes | Unknown bytes | Unresolved indirects | Rejected direct edges |
|---|---:|---:|---:|---:|---:|
| Main EXE | 104,002 | 416,008 | 1,074,936 | 110 | 0 |
| NFE entry-5 route variant | 31,827 | 127,308 | 9,600 | 10 | 0 |
| Verified retail BIOS direct frontier | 3,013 | 12,052 | 512,236 | 27 | 0 |

The raw authorization graph records 120 dynamic indirect sites. The separate
identity-bound CFI receipt closes all of them against finite precompiled
universes: returns use call continuations, callbacks use authenticated
same-image function entries, and exact switch/constant sets use per-site target
sets. Unknown PCs, identities, target values, and executable writes remain
rejected.

Global content-epoch-aware closure proves 1,166 NFE-to-main direct calls that
the first receipt incorrectly rejected because it analyzed images separately.
Numeric addresses in the overlapping main/NFE range are not treated as proof
of an epoch transition. The remaining direct-edge table classifies body gaps,
overlapping-epoch ambiguity, delay-slot evidence gaps, and external targets.
The exact BIOS direct frontier starts at reset, includes the previously excluded
`0xBFC00150` target, and follows every statically direct successor over the
hash-pinned BIOS bytes. It has complete direct closure across 56 ranges and
stops fail-closed at 27 indirect sites. The generated manifest gives every
range an explicit entry and never treats unauthenticated disassembly as a root.
The deterministic 17,353-instruction execution receipt additionally proves the
runtime return target `0xBFC0703C`. Manifest generation preserves that root even
though its bytes coalesce with the surrounding authorized range.

The exact 273 main-image unknown ranges and 17 NFE unknown ranges are recorded
under each image's `unknown_ranges` array in the receipt. The NFE ranges are:

```text
[0x800BFAAC,0x800BFC2C)
[0x800BFC64,0x800BFD24) [0x800C2354,0x800C24D4)
[0x800C2E0C,0x800C2F50) [0x800C4020,0x800C4184)
[0x800C4190,0x800C4318) [0x800C7BD4,0x800C7C1C)
[0x800C7D00,0x800C7D24) [0x800C7DA0,0x800C7FAC)
[0x800C99A0,0x800C9B78) [0x800CABB8,0x800CADC8)
[0x800CCA58,0x800CCE1C) [0x800CEB50,0x800CED54)
[0x800D1640,0x800D1908) [0x800D39D0,0x800D4064)
[0x800E041C,0x800E0C24) [0x800E0E7C,0x800E0E80)
```

## Evidence policy

Authenticated bytes are authoritative. The prepared EXE header supplies the
main root. Route-observed NFE PCs may supply roots only after exact runtime-byte
reconstruction. Ghidra body ranges constrain static extension but never create
roots. Static decoding then proves direct successors and delay-slot ownership.
Any edge outside cross-evidence, ambiguous/control-bearing delay slot, unknown
indirect target, unknown PC, or executable write fails closed.
