#include "clean_aot/compiler/mips_decoder.hpp"

#include <cstdint>

namespace clean_aot::compiler {
namespace {

[[nodiscard]] std::uint32_t direct_target(GuestAddress address, std::uint32_t raw) noexcept {
    const std::uint32_t upper = (address + 4U) & 0xf0000000U;
    return upper | ((raw & 0x03ffffffU) << 2U);
}

[[nodiscard]] std::uint32_t branch_target(GuestAddress address, std::uint32_t raw) noexcept {
    const auto immediate = static_cast<std::int16_t>(raw & 0xffffU);
    const std::int64_t displacement = static_cast<std::int64_t>(immediate) * 4LL;
    const std::int64_t base = static_cast<std::int64_t>(address) + 4LL;
    return static_cast<std::uint32_t>(base + displacement);
}

} // namespace

Instruction decode_instruction(GuestAddress address, std::uint32_t raw) {
    Instruction instruction{address, raw, InstructionKind::Other, false, std::nullopt};
    if (raw == 0U) {
        instruction.kind = InstructionKind::Nop;
        return instruction;
    }

    const std::uint32_t opcode = raw >> 26U;
    switch (opcode) {
    case 0x01U: {
        const std::uint32_t function = (raw >> 16U) & 0x1fU;
        if (function == 0x10U || function == 0x11U) {
            instruction.kind = InstructionKind::ConditionalCall;
        } else {
            // The R3000A decodes every REGIMM rt value as a branch. Only the
            // two architectural link encodings above write the return address.
            instruction.kind = InstructionKind::ConditionalBranch;
        }
        instruction.has_delay_slot = true;
        instruction.direct_target = branch_target(address, raw);
        return instruction;
    }
    case 0x02U:
        instruction.kind = InstructionKind::DirectJump;
        instruction.has_delay_slot = true;
        instruction.direct_target = direct_target(address, raw);
        return instruction;
    case 0x03U:
        instruction.kind = InstructionKind::DirectCall;
        instruction.has_delay_slot = true;
        instruction.direct_target = direct_target(address, raw);
        return instruction;
    case 0x04U:
    case 0x05U:
        instruction.kind = InstructionKind::ConditionalBranch;
        instruction.has_delay_slot = true;
        instruction.direct_target = branch_target(address, raw);
        return instruction;
    case 0x06U:
    case 0x07U:
        instruction.kind = InstructionKind::ConditionalBranch;
        instruction.has_delay_slot = true;
        instruction.direct_target = branch_target(address, raw);
        return instruction;
    case 0x12U:
        if(((raw>>21U)&0x1fU)==0x08U&&(((raw>>16U)&0x1fU)==0U||((raw>>16U)&0x1fU)==1U)){
            instruction.kind=InstructionKind::ConditionalBranch;instruction.has_delay_slot=true;instruction.direct_target=branch_target(address,raw);return instruction;
        }
        break;
    default:
        break;
    }

    if (opcode == 0U) {
        const std::uint32_t function = raw & 0x3fU;
        const std::uint32_t rs = (raw >> 21U) & 0x1fU;
        const std::uint32_t rd = (raw >> 11U) & 0x1fU;
        if (function == 0x08U) {
            instruction.kind = rs == 31U ? InstructionKind::Return
                                         : InstructionKind::IndirectJump;
            instruction.has_delay_slot = true;
        } else if (function == 0x09U) {
            instruction.kind = rd == 0U ? InstructionKind::IndirectJump
                                        : InstructionKind::IndirectCall;
            instruction.has_delay_slot = true;
        }
    }

    return instruction;
}

bool is_control_transfer(InstructionKind kind) noexcept {
    switch (kind) {
    case InstructionKind::DirectJump:
    case InstructionKind::DirectCall:
    case InstructionKind::ConditionalBranch:
    case InstructionKind::ConditionalCall:
    case InstructionKind::IndirectJump:
    case InstructionKind::IndirectCall:
    case InstructionKind::Return:
        return true;
    case InstructionKind::Other:
    case InstructionKind::Nop:
        return false;
    }
    return false;
}

} // namespace clean_aot::compiler
