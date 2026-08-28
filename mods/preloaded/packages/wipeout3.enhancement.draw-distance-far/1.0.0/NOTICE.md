# Draw Distance: Far provenance

This package contains project-authored host-native integration code only. The
behavior was recovered from Hueponik's published WipEout 3 Special Edition PAL
patch and checked against these authenticated executables:

- stock `SCES-02845`: `0213f5292fa1d995ebe61f42d5dbdb6614e481cb3840ac5f1521c2dfdccc0a4d`
- patched PAL reference: `7004d1400015fe1466131fa3f31b529cc6e544d30efe7cbfadc69dcb34b3d7ac`

The published Far preset uses longitudinal and lateral limits of 65535 and a
vertical limit of 32766. Its replacement guest engine precomputes scene lists
in high RAM. This package instead lets the authenticated stock scene-mask owner
run, then augments its result with the same signed geometry test in native
code. It copies no patch bytes, installs no guest engine, allocates no high RAM,
and does not read or write Track 4x4 state.
