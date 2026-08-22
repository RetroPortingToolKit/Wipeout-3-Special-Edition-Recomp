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
| Main EXE | 98,831 | 395,324 | 1,095,620 | 127 | 16 |
| NFE entry-5 route variant | 31,719 | 126,876 | 10,032 | 10 | 1,220 |

No indirect target currently satisfies the narrow local-literal proof rule.
Therefore the finite indirect-target arrays are empty and all 137 indirect
sites remain explicitly unresolved. Unknown PCs and executable writes must be
rejected.

The exact 270 main-image unknown ranges and 18 NFE unknown ranges are recorded
under each image's `unknown_ranges` array in the receipt. The NFE ranges are:

```text
[0x800BF800,0x800BF9B0) [0x800BFAAC,0x800BFC2C)
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
