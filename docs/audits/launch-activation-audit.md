# WipEout 3 launch activation audit

Audit date: 2026-08-22. This is a source-backed audit of the checkout supplied
to this worktree. It deliberately distinguishes a title transformation being
present in a package/media description from that transformation being selected
and verified at launch.

## Result

No authenticated launch receipt can be issued from a checkout without the
user-owned `disc/` directory and resolved `mods/state.toml`. The exact title
verifier fails closed when Track 01, `SCES_028.45`, selector words, or title
strings are unavailable. PAL selector semantics are source-defined until the
exact media is supplied.

## Launch selection trace

| Requested behavior | Selection source | Activation status |
|---|---|---|
| Authenticated Hueponik/PAL title patch | User media, guarded by the PAL package target and exact Track 01/EXE hashes | Not verified without media |
| PAL x2 selector | PAL package selects `wipeout3.framerate.pal-x2`; plugin supplies host 100 Hz/CRTC x2 | Declared, not launch-proven |
| 900% overclock | `game.toml` `[video] cpu_overclock = 900` | Configured |
| 8 MB RAM | Framework hardware package selected by the resolved mod plan | Must be proven before BIOS/game init |
| Max-detail ship geometry | PGXP geometry, perspective, depth, supersampling, and HD texture configuration | Configured; runtime geometry receipt still required |

## Exact title-side semantics

`tools/verify_pal100_title.py` is a verifier, not a patcher. For the exact local
PAL executable it checks the target hashes, the `100HZ`/`50HZ` strings, and the
words at `0x8017BB8C–0x8017BBAC`. Those words implement:

```text
selector 0 -> gSimVblankSkip = 2, PAL-x2 state = 0
selector 1 -> gSimVblankSkip = 1, PAL-x2 state = 0
selector 2 -> gSimVblankSkip = 1, PAL-x2 state = 1
```

The runtime plugin supplies the host-side PAL-x2 rate/multiplier; it cannot turn
an unpatched PAL executable into the selector-2 title. Missing exact media must
therefore fail closed rather than infer activation.

## Required receipt to close the audit

After supplying the user-owned disc, record the verifier JSON, resolved
`mods/state.toml`, and launch log. The receipt must show target hashes,
selector-2 title words, selected PAL x2, live 8 MB initialization before
BIOS/game init, and 900%/PGXP geometry settings. Until then, do not describe
PAL x2, 8 MB, or a Hueponik title hack as active merely because metadata ships.
