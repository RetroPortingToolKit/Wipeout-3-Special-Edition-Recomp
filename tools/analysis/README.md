# Offline binary-analysis helpers

These Ghidra headless scripts seed and export discovery hints for a raw MIPS
image. They are analysis tooling only: their output is not a correctness oracle,
an executable-coverage claim, or an input accepted blindly by the clean AOT
compiler.

`SeedMipsFunctions.java` requires a strictly sorted file of aligned hexadecimal
roots. `ExportFunctionInventory.java` writes a deterministic JSON inventory of
the resulting function bodies. Both refuse malformed or ambiguous inputs. The
product executable and generated Ghidra project remain ignored local evidence.

`normalize_runtime_capture.py` accepts the quarantined legacy runner's v2
overlay-capture manifest. It independently validates canonical addresses,
non-overlapping guest-RAM bounds, exact decoded sizes, byte hashes, and sorted
observed-PC sets. Its normalized receipt, optional PC/root lists, and optional
atomically extracted hash-named region images remain explicitly untrusted
discovery hints: they neither prove that every executable path was observed nor
authorize a shipping fallback.

`inventory_binary_delta.py` compares equal-sized authenticated images and emits
exact contiguous byte and aligned-word change receipts with per-range hashes.
It is intentionally agnostic about code versus data and makes no semantic
claim; it is used to bound independently supplied game patches before clean CFG
analysis.

`match_binary_provenance.py` finds maximal exact byte runs shared by captured
region images and authenticated source payloads. Repetitive anchors are bounded
and suppressed, all inputs and matches are hash-receipted, and local paths can
be normalized beneath an explicit root. A match proves only byte equality: it
does not prove a load/decompression transform, code boundaries, semantics, or
executable coverage.

`inventory_pbp.py --verify-expanded --extract-expanded` can materialize those
hash-verified logical PBP payloads into a new ignored directory for the matcher.
Stored entries and independently decoded bounded-LZSS entries use the same
content-addressed naming rule; neither output is accepted as executable merely
because it contains instruction-like bytes.

`inventory_runtime_variant.py` compares one normalized observed RAM region with
an explicit slice of an authenticated expanded source. It embeds the exact
byte/word delta and separately identifies observed PCs whose instruction words
differ. This can bound a finite runtime-patched variant, but the normalized
capture remains discovery evidence and does not establish complete execution.
`--runtime-offset` and `--comparison-bytes` can measure an aligned subrange
without manufacturing a standalone capture file; the complete region hash and
identity are still verified before the subrange is compared.

`ingest_runtime_target_trace.py` is the fail-closed bridge from a finalized
runtime indirect-target trace to the static compiler. Its v1 receipt must name
every source and destination image epoch, bind each epoch to exact bytes and a
variant identity, attest a complete gap-free event interval with zero drops,
and provide sorted unique aggregate observations for every closed JR/JALR
site. The adapter independently checks the authenticated source opcode, image
bounds, exact event accounting, and all identities. It emits a canonical
identity-bound closure plus one deterministic `clean-aot-compiler-manifest-v1`
file per targeted image. Guest-address overlap across epochs is allowed only
because each edge names its image identity; missing, duplicate, ambiguous,
unsorted, incomplete, or byte-mismatched evidence is rejected. This mechanism
does not interpret guest code and does not claim that a partial trace is
complete—the capture producer must explicitly finalize that claim.
Extended v1 telemetry is checked independently: active epoch identities,
executed PC edges, rejected attempts, statuses, drops, and contiguous sequences
cannot alter the accepted JR/JALR observation counts used for static closure.

`audit_content_static_coverage.py` builds the deterministic whole-media ledger
used to measure content closure. It cross-checks the verified ISO inventory,
all exact PBP expansions, the NFE expansion, executable authorization, finite
target proofs, and runtime-variant epochs. Every cataloged payload appears in
the output even when its code/data role is unknown. Only identities supported
by the supplied evidence are marked executable; unknown payloads, code ranges,
callbacks, direct edges, runtime epochs, and absent compiled regions each hold
their own completeness gate closed.
The byte-backed pass recognizes only exact bounded TIM and Sony VAG streams as
data and uses
a conservative MIPS ABI-shape scan solely to prioritize executable candidates.
All-zero and unrecognized payloads remain unknown-role.
The dominant named-record family is exposed only as a validated 16-byte
uppercase identifier envelope plus a separately hashed unknown-role body.
Unknown instances are also grouped by exact expanded SHA-256 so repeated media
content is reviewed once, without weakening the executable-risk gate.

`cluster_content_payloads.py` emits the focused identity clustering receipt.
It verifies the parent audit's instance and unique-identity accounting before
grouping by container extension, exact expanded size, four-byte magic,
occurrence frequency, and structural classification. Cluster labels remain
non-semantic and all unknown-role and executable-risk gates remain closed.

`build_game_region_shards.py` partitions the authenticated main executable and
NFE route-variant graphs into deterministic closed compiler ranges. It consumes
only identity-bound authorization and finite-target receipts, propagates every
unsafe edge backward to a fixed point, preserves delay-slot atomicity, and emits
both compiler manifests and a fail-closed partial-coverage receipt. Its focused
tests run with `python -m unittest
tools.analysis.tests.test_build_game_region_shards`.

`authorize_executable_regions.py` also closes exact direct-edge corridors rooted
by authenticated main-image targets and NFE-to-main calls. Corridor decoding is
bounded to the selected image identity, rejects reserved R3000A encodings,
stops at every JR/JALR, hashes every
recovered range, and excludes overlapping bytes from the wrong static image.
The emitted `wo3se.overlapping-executable-epoch-dispatch.v1` contract records
five capture-selected NFE sites and one load-base site that requires runtime
selection by authenticated active-image SHA-256. Literal A0/B0/C0 constructions
are finite external exits; every other unresolved indirect remains rejected.

`build_indirect_cfi.py` derives finite per-image function-entry and
call-continuation universes from authenticated callgraph edges intersected with
the generated shard set. `JR $ra` uses only continuation entries; argument and
locally proved memory-loaded callbacks use only same-epoch function entries.
It also recognizes bounded `SLTIU`/indexed-word switch tables, exact constant
sets, authenticated renderer pointer tables, and preserved inherited callbacks,
emitting per-site universes only when every target corridor is executable-
authorized. The shard builder turns those universes into mandatory precompiled
dependencies. Unknown values, unauthenticated table targets, unclassified
computed transfers, and cross-image callbacks remain rejected.

`audit_unknown_code_ranges.py` binds every remaining unknown range to exact
authenticated bytes, counts only legal R3000A/COP0/COP2 words, and independently
checks compiled direct transfers for incoming roots. Legal-looking bytes never
create authority; ranges without an authenticated incoming edge remain unknown.
