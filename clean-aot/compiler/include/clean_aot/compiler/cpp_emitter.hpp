#pragma once

#include "clean_aot/compiler/ir.hpp"

#include <span>
#include <string>
#include <string_view>

namespace clean_aot::compiler {

class CppEmitter final {
public:
    // Emits a self-contained C++20 graph fixture. The generated stepper
    // publishes every statically resolved next PC, including external and
    // wrapped fallthroughs. It reports genuinely dynamic indirect/return
    // control flow by returning false and never invents a target.
    [[nodiscard]] static std::string emit(const Region& region);

    // Emits a function containing only the selected whole basic blocks. The
    // complete region is validated before selection, so cross-shard edges keep
    // their exact guest targets and delay slots cannot be split.
    [[nodiscard]] static std::string emit_shard(
        const Region& region,
        std::span<const BlockId> block_ids,
        GuestAddress shard_identity,
        std::string_view symbol_suffix = {});
};

} // namespace clean_aot::compiler
