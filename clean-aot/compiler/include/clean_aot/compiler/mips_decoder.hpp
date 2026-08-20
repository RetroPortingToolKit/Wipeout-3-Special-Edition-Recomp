#pragma once

#include "clean_aot/compiler/ir.hpp"

namespace clean_aot::compiler {

[[nodiscard]] Instruction decode_instruction(GuestAddress address, std::uint32_t raw);
[[nodiscard]] bool is_control_transfer(InstructionKind kind) noexcept;

} // namespace clean_aot::compiler
