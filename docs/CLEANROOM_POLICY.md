# Cleanroom policy

This branch rebuilds the remaster from independently reproducible evidence.
Previous work is preserved for comparison, but it is not an implementation
source or acceptance oracle.

## Trusted starting points

- Product repository: cleanroom root
  `e217914e7c33ada690067eb8a2d213f86ae0c8c0`.
- New generic compiler/runtime source: original files created under
  `clean-aot/`, with no source parent in a prior PS1 recompiler tree.
- Third-party dependencies: accepted only at an exact upstream commit with a
  recorded license and independently recomputed content identity.
- User media: accepted only after a local identity check. Disc images, BIOS
  dumps, generated executables, save data, and captures are never committed.
- External projects may be used as behavioral references. Their code is not
  copied into this implementation.

"Trusted" here means suitable as a reproducible baseline, not proven correct.
Baseline behavior and every feature still require measured acceptance evidence.

## Quarantined comparison corpus

The following exact states may be inspected to form hypotheses, locate test
cases, or compare output. No code, asset, test, benchmark result, diagnosis, or
documentation claim crosses into this branch merely because it exists there.

- Game `origin/master`: `768cdbffc777ccc4a04d463ab4f98643395ddf37`
- Game remaster branch: `94ec511203064539cd56e75afaa91c1110b90ee1`
- Dirty canonical checkout at that branch (preserved in place)
- Framework feature branch / PR 169:
  `2641f7c59bc18c5cbe64578de45e016c708a6c62`
- Framework snapshot `mstan/psxrecomp` at
  `d56bd0247c472f7987e61162fcd7b812b65c4e7f` and every ancestor/descendant in
  that repository. An immutable history audit found WipEout, Mega Man X5/X6,
  Tomba, Ape Escape, Crash Bash, runtime compilation/interpreter paths, and
  always-on oracle/debug machinery throughout the available DAG. It is a
  reproducible legacy runner, not an independent implementation base.
- `mstan/recomp-ui` at
  `036b963ebd0c6ea51da1a3c29b9aca8600b0f0e3` until it independently passes
  the dependency/provenance and release-link boundary.
- Game PRs 5, 6, and 7, existing build trees, reports, dumps, fonts, texture
  variants, patches, scripts, and handoff documents derived from that work

An item may be reimplemented only after the clean baseline reproduces the
underlying behavior or defect. The new change must have an independently
written test or captured oracle that fails before the change and passes after
it. Asset ingress additionally requires source, license/permission, transform,
and content-hash provenance.

## Evidence gates

1. Media provenance: identify the user's unmodified SCES-02845 Redump BIN/CUE,
   validate the exact pinned hueponik PAL patch, and produce a patched image in
   an ignored workspace without redistributing either input or output.
2. Correctness baseline: boot and run a complete race from a deterministic
   input route while recording executable/media hashes, native-versus-fallback
   dispatch, guest cycles, VBlanks, internal game frames, audio samples, frame
   hashes, and host wall time.
3. Performance retention: compare interleaved baseline/candidate runs on the
   same fixed race. A change does not count unless correctness is unchanged and
   the relevant measured metric improves or remains inside its declared noise
   bound.
4. Feature acceptance: prove 4:3, 16:9, 21:9, and 32:9 composition and native
   projection; PGXP-class geometry and perspective interpolation; HD textures
   and all font classes; 100 Hz timing with 100 distinct game frames; stable
   CDDA/SPU audio; and a full-race plus extended soak.
5. Delivery: build from a fresh clone with only user-provided media, then run
   from a relocated package containing no copyrighted disc or retail BIOS data.

## Architecture boundary

The target is region-based static recompilation. Normal code paths must execute
compiled regions with guest values resident in host temporaries across basic
blocks. CPU state is materialized only at explicit exits and observation
boundaries. Cycle accounting is exact at device-visible boundaries, not
implemented as an opaque helper after every guest instruction.

The shipping runtime contains no general interpreter, runtime compiler,
capture-and-compile path, or compatibility dispatch. Offline setup identifies
every executable image/overlay variant by load range and content hash and emits
all variants ahead of time. Runtime executable writes may select a known
precompiled variant; an unknown executable identity fails closed with a compact
coverage receipt.

The quarantined legacy lane may decode, interpret, capture, or expose debug
commands only to discover routes and test inputs. Its output is explicitly not
a correctness oracle and none of those facilities may enter the release link.
