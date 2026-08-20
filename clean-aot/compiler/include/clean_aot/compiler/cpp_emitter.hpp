#pragma once

#include "clean_aot/compiler/ir.hpp"

#include <string>

namespace clean_aot::compiler {

class CppEmitter final {
public:
    // Emits a self-contained C++20 graph fixture. The generated stepper
    // publishes every statically resolved next PC, including external and
    // wrapped fallthroughs. It reports genuinely dynamic indirect/return
    // control flow by returning false and never invents a target.
    [[nodiscard]] static std::string emit(const Region& region);
};

} // namespace clean_aot::compiler
