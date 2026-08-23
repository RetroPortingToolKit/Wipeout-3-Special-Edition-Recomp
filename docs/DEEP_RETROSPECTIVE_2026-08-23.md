# WipEout 3 SE clean-room deep retrospective

Date: 2026-08-23

This review supersedes optimistic conclusions made during interactive testing.
It reviews the clean upstream restart through root commit `37bff52` and
psxrecomp commit `96b36080`. It does not promote a binary or claim completion
of the remaster goal.

## Bottom line

The clean restart recovered a trustworthy stock baseline and produced several
real correctness improvements, but there is no promotable remaster binary.
The current measured v5 binary is an experimental performance proof built from
an older code-generation identity. It misses the 99.493634 Hz PALx2 target in
39-40 of 44 measured windows and expands generated `.text` by 3.244x. The
current committed source has not been measured as a matching product binary.

The owner's architectural diagnosis was substantially correct. The title is
statically recompiled, but its hot path still pays emulator-style work at a
very fine granularity: dispatch admission, code-validity scans, I-cache fetch
checks, cycle publication, interrupt polling, and R3000A pipeline bookkeeping.
At 900% CPU the host must retire 304.8192 million raw guest cycles per wall
second. The 9.000x counter proves the raw/native conversion ratio; it does not
prove the host is meeting that throughput.

## Evidence that survives review

| Candidate | Steady median | p05 | Raw median | PE / `.text` | Verdict |
|---|---:|---:|---:|---:|---|
| v3 | 51.7 FPS | not authoritative here | 158.39 M/s | earlier architecture | clean evidence of a single-thread throughput ceiling |
| clean v4 | 82.0 FPS | 76.9 | 251.22 M/s | 41.90 MiB / 33,124,816 B | useful dispatch/diagnostic improvement, still slow |
| clean v5 repeat 1 | 94.2 FPS | 89.5 | 288.60 M/s | 112.78 MiB / 107,457,808 B | real gain, unacceptable text growth and still slow |
| clean v5 repeat 2 | 94.6 FPS | 88.9 | 289.83 M/s | same PE | reproducible confirmation |

The fair v5-over-v4 gain is 15.4%, not the earlier contaminated 25.1%
comparison. The measured v5 PE is codegen `0d67212f`, SHA-256
`8DEBAF630F8E792180E6E065F8EBC623821FD317159BAD88D065D3D3E004121A`.
Current committed source hashes to `e655f7bf`; therefore the measured PE and
overlay cache are evidence only.

The following work is worth preserving:

- The audited upstream reset and protected stock executable establish that the
  disc, BIOS, base renderer, countdown geometry, FMV, card, and race can work.
- CPU work, native device time, PALx2 VBlank scheduling, SPU, CD/CDDA, and card
  timing are now separate domains. The 900% ratio is exact rather than a fake
  UI setting.
- Transactional state loading decodes before mutation and rolls back late
  failures. This is a genuine correctness improvement, although older v5 state
  files are intentionally incompatible with the v6 timing contract.
- Early compiled-game routing, shipping diagnostic removal, and fail-closed
  generation-qualified code validation materially improve v4 without changing
  guest semantics.
- PGXP write barriers now cover more execution paths and safely invalidate
  unsupported operations. That is a safety foundation, not FULL/MAX PGXP.
- The telemetry parser makes source-log hashes and separate timing axes durable.
  It exposed the false attribution rather than validating it.

## Performance architecture findings

The remaining v5 profile used 605 steady-FMV main-thread stack tops from 78
existing freeze dumps. Sampling may have periodic bias, but the distribution
was stable across both runs:

- cycle batch flush: 14.21%;
- compiled-game dispatch: 11.57%;
- interrupt check: 8.76%;
- cycle-count query: 7.77%;
- generation/cache validation helpers: 6.77%;
- instruction-cache fetch: 4.63%.

The strongest bounded next optimization is not another clock shortcut. The
"cached" compiled-game admission still binary-searches 23,336 entries and
walks every covered 4 KiB page to recompute a generation sum on every clean
dispatch. Dispatch plus validation accounts for at least 18.35% of sampled
tops. A global watched-text mutation epoch can make the unchanged path O(1),
while an epoch change retains exact per-range/page validation and fails closed.
A generated direct/page index can remove the binary search. This has a simpler
correctness proof than deferring interrupt boundaries and enough estimated
headroom to close the measured p05 gap if the sample share converts to runtime.

The next ranked costs are a cheap generated branch poll before the full
flush/IRQ/cycle path, a constant-address I-cache hit gate, and compact
block/function timing specialization. Each must be isolated in a same-host,
same-cache A/B. The v5 per-instruction force-inline design must not be the final
architecture: representative hot functions grew about 3.6x, total `.text`
grew by 74.32 MiB, and host instruction/branch counts grew roughly 3.25x/4x.

A bounded, uncommitted v6 prototype shows that the timing specialization can
be made compact: it groups at most timing-transparent straight-line bodies,
keeps each `base -> deps -> LDS` transition in order, splits at fetch leaders,
labels, loads, control flow, delay slots and timing-sensitive emitted bodies,
and transactionally rejects to original per-site slow steps. Transparency is
fail-closed metadata from the body actually emitted, so product/widescreen
rewrites are barriers rather than being trusted from their raw opcode. Its one
shared no-clone helper object is 2,304 bytes of `.text`, with a worst-case mask
table below 1.97 MiB. Focused timing and codegen tests pass, but dynamic speed
is unknown and typical runs are at most four instructions. It deserves a later
isolated size/performance A/B, not priority over direct dispatch and not a
commit or product build during this review.

Using modern PC CPUs does not mean naively parallelizing the serial R3000A
instruction stream. It means exploiting static knowledge: direct entry maps,
epoch-qualified code trust, straight-line timing summaries, constant-address
memory/I-cache paths, and device publication only at observable barriers.

## Counter and benchmark findings

The current on-screen `Game FPS` is meaningful: it counts simulated VBlank
edges per wall-time window. The following adjacent values are not equivalent
proofs:

- `cpu_effective=9.000x` is the ratio of raw retired CPU charges to native
  simulated CPU time. It can remain exactly 9.000x while the whole game runs at
  half wall speed.
- SPU and CDDA "effective" rates are calculated against simulated native time,
  not wall time. They can report approximately 44.1 kHz / 75 sectors per
  simulated second while audible output is slow, starved, or stuttering.
- Render/display FPS is presentation throughput and must not be substituted for
  guest VBlank rate.

Wall-domain SPU production, CDDA production, audio queue depth, underruns,
callback starvation, and cumulative A/V drift are still missing. The owner's
reports that CDDA speed separated from gameplay speed cannot be accepted or
rejected from the current audio counters.

Benchmark process discipline was initially poor. A `--help` probe silently
opened the launcher and contaminated the first v4 comparison; earlier work
also opened the launcher repeatedly, learned `--no-launcher` late, and once
closed the user's active game. Host-wide choking that also affected Plex is not
valid evidence against the guest runtime, but it proves interactive anecdotes
cannot replace isolated runs. A new uncommitted runner adds `--no-launcher`, a
same-image process guard, a fresh card directory, child-only termination, and
binary/log hashes. It is not accepted yet because it must reject a natural
early exit and record wall-domain audio/host-load context.

Headless fresh-boot FMV is a CPU-throughput gate only. It does not prove the
OpenGL renderer, 100 Hz mod set, CDDA output, race play, or post-race replay.
Those require separate integrated scripted workloads. The replay state is a
reusable start/stop fixture, never a blocker or an excuse to pause the goal.

## PGXP review

The exact local behavior oracle is GooseStation builder tag `v0.6.9.11609`,
upstream commit `a233ec1fb7f46181e77e6d2a52446ad6f0a7c54f`. Its implementation
is reference-only and must not be copied.

Current PGXP is not FULL or MAX and should remain disabled in candidate builds.
The audit found concrete correctness defects in addition to missing features:

- tolerance checks omit absolute value, so negative deltas bypass the limit;
- signed negative screen coordinates are left-shifted, invoking C++ undefined
  behavior;
- GTE register 15 shifts the machine SXY FIFO but leaves stale shadow entries
  12/13;
- the "precise" NCLIP path truncates 16.16 fractions before the cross product
  and is gated by widescreen rather than PGXP culling;
- same-value DMA/MDEC/host writes can retain stale memory provenance;
- the screen-coordinate fallback/cache is effectively always enabled with
  different replacement/default semantics and can affect 2D primitives;
- lines, polylines, perspective color, depth, full CPU/memory propagation,
  normalized settings, and runahead continuity are absent.

The dependency order is correctness first: central provenance invalidation and
FIFO/tolerance fixes; complete CPU/memory dataflow; GTE projection/culling; a
coherent per-primitive precision contract; texture/color/cache; depth and
backend capabilities; normalized settings; then time-travel state and WipEout
capture acceptance. The 8 MiB memory contract must exist before PGXP memory is
made dynamic.

## Goal status after review

| Goal axis | Status |
|---|---|
| Verified stock boot/FMV/card/countdown/race | protected stock baseline exists; not re-proved by a current remaster PE |
| Genuine 9x CPU and PALx2 100 Hz | arithmetic and clock-domain separation are correct; wall throughput fails |
| Native SPU/CD/card timing | simulated domains are separated; wall audio acceptance is missing |
| Trustworthy telemetry | game-speed evidence improved; CPU/audio labels and wall metrics remain incomplete |
| Coherent 8 MiB | contract audited; implementation absent |
| FULL/MAX PGXP | absent and current partial implementation has correctness bugs |
| 4:3 through 32:9 / common resolutions | not accepted in the clean current line |
| Bezels, HD fonts, translated text | absent; licensed/user-supplied asset boundary remains |
| Integrated verified deliverable | none; current source and measured binary identities do not match |

## Required order when implementation resumes

1. Finish the contamination-proof benchmark contract and wall-audio telemetry.
2. Build and measure a binary matching the current source before attributing
   any further change.
3. Implement and exhaustively test O(1), epoch-qualified direct clean dispatch;
   retain exact slow revalidation after executable writes, restore, image
   changes, and epoch overflow.
4. Isolate the branch/barrier poll and I-cache fast gates if direct dispatch
   alone does not provide uncapped PALx2 headroom.
5. Replace/revert v5's text-exploding timing inline experiment with a compact,
   effect-metadata-driven block specialization and prove code-size bounds.
6. Require two clean 45-second CPU receipts plus interactive FMV, race, CDDA,
   countdown/image, and replay receipts before promotion.
7. Implement coherent 8 MiB across CPU, DMA, state, dirty code, overlays,
   digest/netplay gates, and PGXP.
8. Build FULL/MAX PGXP in the correctness dependency order above.
9. Complete aspect/resolution/backend, bezel/font/text, and final integrated
   acceptance only after the execution foundation is stable.

No current binary should be called final, current, or promoted on the basis of
this review.
