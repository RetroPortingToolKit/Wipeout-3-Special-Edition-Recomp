# Hueponik 8 MB title handshake: direct-native implementation

Date: 2026-08-28

## Result

The framework's unique 8 MiB RAM window is now paired with Hueponik's complete
authenticated pre-island title handshake. Two native instruction-site callbacks
execute the exact changed MIPS semantics while the stock SCES-02845 executable
remains byte-identical in guest RAM and generated AOT:

| address | authenticated stock | authenticated Hueponik | native effect |
| --- | --- | --- | --- |
| `8014BAD0` | `8C449804` (`lw a0,-67fc(v0)`) | `24040001` (`addiu a0,zero,1`) | `a0 = 1` |
| `8015FA80` | `00052BC0` (`sll a1,a1,15`) | `00052D40` (`sll a1,a1,21`) | `a1 <<= 21` |

The comparison inputs are stock SHA-256
`0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`
and Hueponik ac46a5d SHA-256
`7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`.

## Boundary

This feature depends on the framework 8 MB RAM package. It does not install or
read Hueponik's resident menu, high-RAM code/data island, mutable option state,
or Track 4x4 replacement engine. The other six changed non-timing pre-island
words remain owned by their separate direct feature or are deliberately absent
because their loader/menu payload is absent.

Run `py -3 tools/verify_wipeout3_hue_8mb_title.py` and the CTest target
`wipeout3-hue-8mb-title` before a live candidate probe.
