# PGXP-free widescreen release verification

Verification date: 2026-08-27

This release layer targets the current `origin/master` title tree and runtime
commit `b6aedc800a0a7375ba7c31344c03a3b9d401edfd`.  The runtime change is isolated
in [mstan/psxrecomp#247](https://github.com/mstan/psxrecomp/pull/247).

## Scope and invariants

- Nine title-owned branch sites opt into exact native NCLIP sign handling.
- Four billboard FLAG patches remove only the title's screen-edge rejection.
- The adaptive projection margin is 34 guest pixels with a 64-pixel guard.
- PGXP geometry correction, CPU mode, depth, and perspective texturing remain
  disabled.  Generated code contains zero PGXP calls.
- A 4:3 presentation remains bit-exact: the exact-NCLIP override is dormant and
  projection squash is 1/1.
- There is no disc-hash bypass or embedded user media.

## Media identity and generation

Generation used an owner-supplied stock SCES-02845 Track 01 with these hashes:

| Digest | Value |
| --- | --- |
| Size | 94,660,944 bytes |
| MD5 | `b9d48f139544051893f45eed62780e42` |
| SHA-1 | `a7bb5cdafdf97ba1cd99cc832439f57ce0c39a8a` |
| SHA-256 | `003bdc41068252e791ea7dd99ba1b94814dc48b967e57a7cdc4bf6e1cf4cb1e9` |
| CRC32 | `a0e9e5c2` |

Normal generation, without `--skip-validation`, emitted 61 shards and 29,482
functions.  Applying the repository's `pal_track01.xdelta3` to that stock image
produced SHA-256
`397bd67f6aac9f5f15de2c13d9df66ac22cac0215024921794b2d8d08a33da12`,
which exactly matches the independently supplied PAL100 image.

## Clean builds and focused tests

- Clean generated Debug build: pass.
- Clean generated Release build: pass.
- Optimized Release build with telemetry compiled in: pass.
- Fresh-clone configure, OpenBIOS regeneration, and Release host build: pass.
- Runtime `recompiler_patch_test`: pass.
- Runtime `gte_register_access_test`: pass.
- Configuration shadow validation and all nine expected instruction-word checks:
  pass.
- Generated-code census: nine exact NCLIP guards, zero PGXP references.
- Fractional-DPI 4:3 fallback: a nominal 960x720 client mapped to a 2161x1620
  physical surface on the verification host.  The runtime now tolerates that
  single rounding pixel and produces `configured=0 active=0 margin=0` with
  squash `1/1`.

## Visual matrix

The optimized Release executable was driven into live races, not attract or
replay mode.  Exact client sizes were exercised for both Talon's Reach and
Altima VII:

| Track | 4:3 (960x720) | 16:9 (1280x720) | 21:9 (1400x600) |
| --- | --- | --- | --- |
| Talon's Reach | pass, adaptive off | pass, adaptive on | pass, adaptive on |
| Altima VII | pass, adaptive off | pass, adaptive on | pass, adaptive on |

Craft, terrain, HUD, lights, billboards, and effects remained present.  No edge
wedges, missing world geometry, or black presentation bands were observed.
The final telemetry was `configured=0 active=0 margin=0 squash=1/1` at 4:3,
and `configured=1 active=1` with nonzero projection margin at both wide aspects.

## PAL100 and CDDA proof

The final Release executable ran the declarative
`wipeout3.enhancement.framerate` package with `build="pal"` and the small
`wipeout3.enhancement.framerate-pal100` host activation package.  A continuous
60.140-second in-race sample measured:

- 6,012 delivered VBlanks, or 99.9667 Hz;
- 2,651,880 CDDA frames, or 44,095.111 frames/s (0.0111% from 44.1 kHz);
- zero new trace/output underruns, overflow drops, or SPU CD under/overflows;
- zero poll-hack VBlank raises; and
- active adaptive widescreen for the entire sample.

The counter-delta verifier is `tools/verify_widescreen_runtime.py`.  It excludes
boot/menu transients by sampling all counters before and after each bounded race
window.

## Reliability soaks

Each accepted track/aspect combination received a continuous five-minute moving
race soak.  The bounded controller profile alternates steering every five
seconds and applies a 0.1-second acceleration pulse per period.  This keeps the
race and scenery moving while avoiding the destructive full-throttle wall loop.
The harness samples live race and widescreen state every 30 seconds, requires
strictly advancing guest frames, and rejects cadence drift or any new audio
fault.

| Track / aspect | Seconds | VBlank Hz | CDDA drift | Margin | Audio faults |
| --- | ---: | ---: | ---: | ---: | ---: |
| Talon's Reach 16:9 | 300.093 | 50.0045 | 0.0090% | 117 | 0 |
| Talon's Reach 21:9 | 300.141 | 49.9965 | 0.0026% | 184 | 0 |
| Altima VII 16:9 | 300.062 | 50.0130 | 0.0282% | 117 | 0 |
| Altima VII 21:9 | 300.125 | 50.0058 | 0.0072% | 184 | 0 |

All four final composed frames showed an intact craft in the active race; shield
remained between 82.45 and 92.25.  A separate 60.140-second live resize back to
960x720 measured 50.0166 Hz, zero audio faults, and the required identity state
`configured=0 active=0 margin=0`.

Altima savestate restoration produced a bounded CDDA warm-up transient when the
first counter snapshot was taken after only two seconds.  That receipt was
rejected.  Final Altima receipts use a 20-second pre-baseline settling interval;
the interval is outside the measured window and both five-minute deltas contain
zero SPU CD underflow frames.

Machine-readable receipts and periodic/final composed captures are beneath the
ignored `artifacts/` directory so user-media test output is never committed.
The final process was scoped and stopped by exact PID 19980 after verifying its
executable path; the PID was then verified absent.

## Reproduction notes

Builds and generation require the user's own verified SCES-02845 media.  Enable
`wipeout3.graphics.widescreen` for adaptive widescreen.  For PAL100, also enable
`wipeout3.enhancement.framerate-pal100` and select the `pal` build of
`wipeout3.enhancement.framerate`.  The PAL100 package declares the dependency;
it does not duplicate the title transform.
