Game edits come from hueponik's WipEout 3 Special Edition patches:
https://codeberg.org/hueponik/wip3out-patches

This package does not redistribute the disc. It stores sparse guarded disc overlays converted from the published xdelta against a verified SCES-02845 Track 01 dump; the boot EXE's edits arrive through the same overlays when the BIOS loads it. The wipeout3.framerate.skip2* plugins force gSimVblankSkip=2 from SimVblankGate entry (pre-CPS) plus a VBlank backup. 1.3.0 ships only hueponik's plain NTSC build (ntsc: 60 Hz, stock 2 MB RAM) as one hidden, default-on feature; the 8 MB full builds and the Region option of 1.2.0 are gone.
