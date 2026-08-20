#pragma once

#include "clean_aot/compiler/ir.hpp"

#include <string>

namespace clean_aot::compiler {

class CppEmitter final {
public:
    // Emits a self-contained C++20 graph fixture. The generated stepper only
    // advances resolved edges; it reports unresolved indirect control flow by
    // returning false and never invents a zero program counter.
    [[nodiscard]] static std::string emit(const Region& region);
};

} // namespace clean_aot::compiler
