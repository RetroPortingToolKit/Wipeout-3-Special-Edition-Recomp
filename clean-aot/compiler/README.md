# Clean AOT compiler tranche

This directory is an independent C++20 compiler foundation. It has no source
dependency on the legacy runtime or recompiler. The public model is deliberately
region-oriented: a region contains basic blocks, and edges connect block IDs to
guest addresses. There is no inferred function object in the model.

The current tranche provides:

- checked PS-X EXE parsing for the executable header and text image;
- a small MIPS-I decoder for direct control-flow construction;
- deterministic reachable-region construction with delay-slot-aware blocks;
- explicit direct, conditional, fallthrough, call, return, and indirect edges;
- a standalone C++ edge-table emitter suitable for synthetic fixtures.

The tests use only fabricated addresses and instruction words. They do not load
the product disc, BIOS, generated sources, or any other project-specific input.
