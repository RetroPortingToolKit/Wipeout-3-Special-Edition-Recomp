# Clean AOT compiler tranche

This directory is an independent C++20 compiler foundation. It has no source
dependency on the legacy runtime or recompiler. The public model is deliberately
region-oriented: a region contains basic blocks, and edges connect block IDs to
guest addresses. There is no inferred function object in the model.

The current tranche provides:

- checked PS-X EXE parsing plus explicit bounded raw-image ingestion with a
  caller-supplied load address and entry PC;
- a checked MIPS-I direct-control decoder that distinguishes calls, conditional
  link branches, jumps, indirect transfers, and returns;
- deterministic reachable-region construction with delay-slot-aware blocks;
- explicit direct, conditional, fallthrough, call, return, and indirect edges,
  including external straight-line fallthrough at image/region boundaries;
- a standalone C++ edge-table emitter with a stable region-unique C dispatch
  symbol suitable for registration by the clean runtime.

The tests use only fabricated addresses and instruction words. They do not load
the product disc, BIOS, generated sources, or any other project-specific input.
The generated fixture is compiled as part of the normal build, and focused
tests cover malformed headers/ranges, delay-slot ownership, calls/returns,
loops, canonical ordering, address wrap, and fail-closed IR validation.

`clean_aot_compiler_cli` implements both the PS-X EXE argument shape and the
explicit `clean-aot-image-v1` raw-image shape used by the product pipeline. It
accepts only manifest-bounded load/code ranges and sorted explicit roots, emits
stable region symbols, and emits an exact variant entry table for the runtime.
