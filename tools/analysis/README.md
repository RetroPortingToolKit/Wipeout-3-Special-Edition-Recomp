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
