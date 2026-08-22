# Reference provenance and implementation boundary

This project uses external material to define behavior and acceptance tests, not
as source to transplant into the clean implementation. Every implementation
change must be independently written, reviewed, and covered by an observable
contract. Local reference checkouts are never build dependencies or package
inputs.

The term **cleanroom** in this repository describes separation from the tainted
Claude-era branches and from emulator/recompiler implementation code. It is not
a claim that all public PlayStation hardware facts were reconstructed without
documentation.

## Pinned references

| Reference | Identity | Permitted use here | Boundary |
| --- | --- | --- | --- |
| PSX-SPX documentation | `psx-spx/psx-spx.github.io@b791ca25b7833b356b28917383f231078762a75d` | Hardware behavior, directed test cases, terminology | Its own README says it is not a legal clean-room document and that no republication license was acquired. No text, code, or diagrams are copied into product source. Claims derived from it remain behavioral-reference claims. |
| GooseStation builder | `hueponik/goosestation-builder@d9a14b7de510fa9e5810737a18fe7da51d5443d9` | Observable 100-Hz and PGXP comparison oracle | GPL-2.0 builder scripts and transformations are not imported. The checkout pins DuckStation `a233ec1fb7f46181e77e6d2a52446ad6f0a7c54f`; that code is not linked or copied. |
| RetroArch replay/recording frontend | Ubuntu package `1.22.2+dfsg-2ubuntu1` (`/usr/bin/retroarch` SHA-256 `b74b401106ecac95029737f859ef784ad9ac8f5a57651c39ff9769e370312fe5`); replay-format source inspected at `libretro/RetroArch@69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576` | Deterministic input playback and lossless behavior-oracle capture | GPL-3.0 frontend code is not copied or linked. The original replay patcher changes only values in an existing version-2 event stream and fails closed on malformed structure. Captures remain ignored; the metadata-only PALx2 route receipt is `docs/evidence/goosestation-palx2-route-v1.json`. |
| hueponik WipEout patches | `hueponik/wip3out-patches@ac46a5dc500a9581ea55b8efb5c1a0d4be944aba` (`dev`) | Player-side application of the exact PAL xdelta and behavioral acceptance | Only the patch identity is recorded. The patch and resulting disc image are not distributed in this repository. |
| Wip3out HD Texture Pack | `princepeter64/Wip3out-HD-Texture-Pack@039a0690448047401cef251594327a25fcc73fad` | Permitted visual source and texture-identity clue | No machine-readable license was present; the project owner attested explicit permission for this non-commercial work. The metadata-only per-file manifest is SHA-256 `6578cc4f9e35adccacb751abe3e29c1d640acc6b624b881e0556ab9485ad4c9d`. Its 292 present PNGs are not copied into Git, and the external replacement-key algorithm remains untrusted until independently reproduced. |
| wipeout-rewrite | `phoboslab/wipeout-rewrite@d48f01c8f00e7f4820a9c2ead0dedd4fb33427d3` | Asset-format behavior clue only | No license file was present in the pinned checkout. Its decoder implementation is not copied or linked. A separately written bounded decoder is tested with independent synthetic fixtures and exact local-media size/terminator evidence. |
| Ghidra headless | `ghidra_12.1.3_PUBLIC_20260817.zip`, SHA-256 `93a5d11a9ad510622acaaf908c556a7b9b764d338e78a7567f3689bf5081fd54` | Offline function/boundary discovery on the exact local executable | Apache-2.0 analysis tooling; never a build or package dependency. Exported roots and bodies remain explicitly untrusted until the clean compiler and runtime coverage validate them. |
| Xdelta | `jmacd/xdelta@v3.2.0`, Windows archive SHA-256 `af8ef036cb077a48df080c9a8ac1be4a6e7511c32d11f8bec89b6803a9e52576` | Player-side application and local verification of the pinned hueponik patch | Apache-2.0 external preparation tool; not linked, bundled, or required by the shipping runtime. |
| b3sum | `BLAKE3-team/BLAKE3@1.8.7`, Windows binary SHA-256 `fce0c2406a1e92b49fedd02a48a67dc5ba71c3a85eb80a2f950ed985f90e76e2` | Independent BLAKE3 media and patch verification | CC0/Apache-2.0 external verification tool; not linked, bundled, or required by the shipping runtime. |

## Reference-handling rules

- Do not copy implementation code, patches, generated transformations, prose,
  or diagrams from an oracle into `clean-aot/`.
- Record the exact reference commit and the tested observation when a behavior
  contract depends on an external implementation.
- Prefer independently generated fixtures and hardware/disc observations over
  source-shaped tests.
- Keep title media, BIOS images, texture PNGs, font sources, captures, and
  generated executable artifacts outside Git.
- A passing comparison against DuckStation or GooseStation is evidence, not an
  implementation design requirement and not proof of static-code coverage.
- Claude-era fixes and conclusions remain hypotheses until reproduced from the
  clean lane with independent evidence.
