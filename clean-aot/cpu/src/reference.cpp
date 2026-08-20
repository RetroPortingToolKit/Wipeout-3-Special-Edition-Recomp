#include "r3000a/reference.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace r3000a::reference {
namespace {

[[nodiscard]] constexpr std::int32_t signed_bits(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t unsigned_bits(std::int32_t value) noexcept {
    return std::bit_cast<std::uint32_t>(value);
}

[[nodiscard]] constexpr std::int32_t sign_extend16(std::uint16_t value) noexcept {
    const std::int16_t narrow = std::bit_cast<std::int16_t>(value);
    return static_cast<std::int32_t>(narrow);
}

[[nodiscard]] constexpr std::uint32_t add_offset(std::uint32_t address,
                                                  std::int32_t offset) noexcept {
    return address + unsigned_bits(offset);
}

[[nodiscard]] constexpr std::uint32_t branch_target(std::uint32_t pc,
                                                     std::uint16_t immediate) noexcept {
    const std::uint32_t displacement =
        static_cast<std::uint32_t>(sign_extend16(immediate)) << 2u;
    return (pc + 4u) + displacement;
}

[[nodiscard]] constexpr std::uint32_t arithmetic_right_shift(std::uint32_t value,
                                                              std::uint32_t amount) noexcept {
    if (amount == 0u) {
        return value;
    }
    const std::uint32_t shifted = value >> amount;
    if ((value & kWordSignBit) == 0u) {
        return shifted;
    }
    return shifted | (std::numeric_limits<std::uint32_t>::max() << (32u - amount));
}

[[nodiscard]] constexpr bool is_control_transfer(Op op) noexcept {
    switch (op) {
    case Op::J:
    case Op::Jal:
    case Op::Jr:
    case Op::Jalr:
    case Op::Beq:
    case Op::Bne:
    case Op::Blez:
    case Op::Bgtz:
    case Op::Bltz:
    case Op::Bgez:
    case Op::Bltzal:
    case Op::Bgezal:
    case Op::Bc0f:
    case Op::Bc0t:
    case Op::Bc2f:
    case Op::Bc2t:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool is_coprocessor0_operation(Op op) noexcept {
    return op == Op::Mfc0 || op == Op::Mtc0 || op == Op::Rfe ||
           op == Op::Bc0f || op == Op::Bc0t;
}

[[nodiscard]] constexpr bool is_coprocessor2_operation(Op op) noexcept {
    switch (op) {
    case Op::Mfc2:
    case Op::Cfc2:
    case Op::Mtc2:
    case Op::Ctc2:
    case Op::Bc2f:
    case Op::Bc2t:
    case Op::Cop2Command:
    case Op::Lwc2:
    case Op::Swc2:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool instruction_fields_valid(const Instruction& instruction) noexcept {
    const auto op = static_cast<std::uint8_t>(instruction.op);
    constexpr std::uint32_t target_mask = (std::uint32_t{1} << 26u) - 1u;
    return op <= static_cast<std::uint8_t>(Op::Swc2) &&
           instruction.rs < 32u && instruction.rt < 32u && instruction.rd < 32u &&
           instruction.shamt < 32u && instruction.target <= target_mask &&
           instruction.coprocessor < 4u;
}

[[nodiscard]] DecodeResult invalid_decode(std::uint32_t raw, std::string error) {
    DecodeResult result;
    (void)raw;
    result.error = std::move(error);
    return result;
}

} // namespace

std::optional<std::uint32_t> Cop0State::read(std::uint8_t reg) const noexcept {
    switch (reg) {
    case 0:
        return index;
    case 1:
        return random;
    case 2:
        return entry_lo;
    case 4:
        return context;
    case 8:
        return bad_vaddr;
    case 10:
        return entry_hi;
    case 12:
        return status;
    case 13:
        return cause;
    case 14:
        return epc;
    case 15:
        return prid;
    default:
        return std::nullopt;
    }
}

bool Cop0State::write(std::uint8_t reg, std::uint32_t value) noexcept {
    switch (reg) {
    case 0:
        index = value;
        return true;
    case 2:
        entry_lo = value;
        return true;
    case 4:
        context = value;
        return true;
    case 8:
        return false;
    case 10:
        entry_hi = value;
        return true;
    case 12:
        status = value;
        return true;
    case 13:
        // Only the two software interrupt-pending bits are writable by
        // MTC0; hardware-owned Cause fields remain live state.
        cause = (cause & ~kCauseSoftwareInterruptMask) |
                (value & kCauseSoftwareInterruptMask);
        return true;
    case 14:
        epc = value;
        return true;
    case 1:
    case 15:
    default:
        return false;
    }
}

Memory::Memory(std::uint32_t base, std::size_t size)
    : base_(normalize_address(base)), bytes_(size, 0u) {}

void Memory::fill(std::uint8_t value) noexcept {
    std::fill(bytes_.begin(), bytes_.end(), value);
}

std::optional<std::size_t> Memory::offset(std::uint32_t address,
                                          std::size_t width) const noexcept {
    const std::uint32_t physical = normalize_address(address);
    if (physical < base_) {
        return std::nullopt;
    }
    const std::uint64_t delta = static_cast<std::uint64_t>(physical - base_);
    const std::uint64_t end = delta + static_cast<std::uint64_t>(width);
    if (end > static_cast<std::uint64_t>(bytes_.size())) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(delta);
}

std::optional<std::uint8_t> Memory::load8(std::uint32_t address) const noexcept {
    const std::optional<std::size_t> at = offset(address, 1u);
    if (!at.has_value()) {
        return std::nullopt;
    }
    return bytes_[*at];
}

std::optional<std::uint16_t> Memory::load16le(std::uint32_t address) const noexcept {
    const std::optional<std::size_t> at = offset(address, 2u);
    if (!at.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(bytes_[*at]) |
           (static_cast<std::uint16_t>(bytes_[*at + 1u]) << 8u);
}

std::optional<std::uint32_t> Memory::load32le(std::uint32_t address) const noexcept {
    const std::optional<std::size_t> at = offset(address, 4u);
    if (!at.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(bytes_[*at]) |
           (static_cast<std::uint32_t>(bytes_[*at + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes_[*at + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes_[*at + 3u]) << 24u);
}

bool Memory::store8(std::uint32_t address, std::uint8_t value) noexcept {
    const std::optional<std::size_t> at = offset(address, 1u);
    if (!at.has_value()) {
        return false;
    }
    bytes_[*at] = value;
    return true;
}

bool Memory::store16le(std::uint32_t address, std::uint16_t value) noexcept {
    const std::optional<std::size_t> at = offset(address, 2u);
    if (!at.has_value()) {
        return false;
    }
    bytes_[*at] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes_[*at + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    return true;
}

bool Memory::store32le(std::uint32_t address, std::uint32_t value) noexcept {
    const std::optional<std::size_t> at = offset(address, 4u);
    if (!at.has_value()) {
        return false;
    }
    bytes_[*at] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes_[*at + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    bytes_[*at + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    bytes_[*at + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    return true;
}

DecodeResult decode(std::uint32_t raw) {
    const std::uint8_t opcode = static_cast<std::uint8_t>((raw >> 26u) & 0x3Fu);
    const std::uint8_t rs = static_cast<std::uint8_t>((raw >> 21u) & 0x1Fu);
    const std::uint8_t rt = static_cast<std::uint8_t>((raw >> 16u) & 0x1Fu);
    const std::uint8_t rd = static_cast<std::uint8_t>((raw >> 11u) & 0x1Fu);
    const std::uint8_t shamt = static_cast<std::uint8_t>((raw >> 6u) & 0x1Fu);
    const std::uint8_t funct = static_cast<std::uint8_t>(raw & 0x3Fu);
    const std::uint16_t immediate = static_cast<std::uint16_t>(raw & 0xFFFFu);
    const std::uint32_t target = raw & 0x03FFFFFFu;

    auto result = [&](Op op) {
        DecodeResult decoded;
        decoded.instruction = Instruction{op, rs, rt, rd, shamt, immediate, target, raw};
        return decoded;
    };
    auto unavailable_coprocessor = [&](std::uint8_t coprocessor) {
        DecodeResult decoded = result(Op::UnavailableCoprocessor);
        decoded.instruction->coprocessor = coprocessor;
        return decoded;
    };
    auto require = [&](bool condition, const char* error) -> std::optional<DecodeResult> {
        if (condition) {
            return std::nullopt;
        }
        return invalid_decode(raw, error);
    };

    switch (opcode) {
    case 0x00: {
        switch (funct) {
        case 0x00:
            if (const auto invalid = require(rs == 0u, "SLL has nonzero rs"); invalid) {
                return *invalid;
            }
            return result((raw == 0u) ? Op::Nop : Op::Sll);
        case 0x02:
            if (const auto invalid = require(rs == 0u, "SRL has nonzero rs"); invalid) {
                return *invalid;
            }
            return result(Op::Srl);
        case 0x03:
            if (const auto invalid = require(rs == 0u, "SRA has nonzero rs"); invalid) {
                return *invalid;
            }
            return result(Op::Sra);
        case 0x04:
            if (const auto invalid = require(shamt == 0u, "SLLV has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Sllv);
        case 0x06:
            if (const auto invalid = require(shamt == 0u, "SRLV has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Srlv);
        case 0x07:
            if (const auto invalid = require(shamt == 0u, "SRAV has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Srav);
        case 0x08:
            if (const auto invalid = require(rt == 0u && rd == 0u && shamt == 0u,
                                             "JR has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Jr);
        case 0x09:
            if (const auto invalid = require(rt == 0u && shamt == 0u,
                                             "JALR has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Jalr);
        case 0x10:
            if (const auto invalid = require(rs == 0u && rt == 0u && shamt == 0u,
                                             "MFHI has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Mfhi);
        case 0x11:
            if (const auto invalid = require(rt == 0u && rd == 0u && shamt == 0u,
                                             "MTHI has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Mthi);
        case 0x12:
            if (const auto invalid = require(rs == 0u && rt == 0u && shamt == 0u,
                                             "MFLO has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Mflo);
        case 0x13:
            if (const auto invalid = require(rt == 0u && rd == 0u && shamt == 0u,
                                             "MTLO has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Mtlo);
        case 0x18:
            if (const auto invalid = require(rd == 0u && shamt == 0u,
                                             "MULT has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Mult);
        case 0x19:
            if (const auto invalid = require(rd == 0u && shamt == 0u,
                                             "MULTU has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Multu);
        case 0x1A:
            if (const auto invalid = require(rd == 0u && shamt == 0u,
                                             "DIV has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Div);
        case 0x1B:
            if (const auto invalid = require(rd == 0u && shamt == 0u,
                                             "DIVU has nonzero unused fields"); invalid) {
                return *invalid;
            }
            return result(Op::Divu);
        case 0x20:
            if (const auto invalid = require(shamt == 0u, "ADD has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Add);
        case 0x21:
            if (const auto invalid = require(shamt == 0u, "ADDU has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Addu);
        case 0x22:
            if (const auto invalid = require(shamt == 0u, "SUB has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Sub);
        case 0x23:
            if (const auto invalid = require(shamt == 0u, "SUBU has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Subu);
        case 0x24:
            if (const auto invalid = require(shamt == 0u, "AND has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::And);
        case 0x25:
            if (const auto invalid = require(shamt == 0u, "OR has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Or);
        case 0x26:
            if (const auto invalid = require(shamt == 0u, "XOR has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Xor);
        case 0x27:
            if (const auto invalid = require(shamt == 0u, "NOR has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Nor);
        case 0x2A:
            if (const auto invalid = require(shamt == 0u, "SLT has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Slt);
        case 0x2B:
            if (const auto invalid = require(shamt == 0u, "SLTU has nonzero shamt"); invalid) {
                return *invalid;
            }
            return result(Op::Sltu);
        case 0x0C:
            return result(Op::Syscall);
        case 0x0D:
            return result(Op::Break);
        default:
            return invalid_decode(raw, "unknown SPECIAL function");
        }
    }
    case 0x01:
        switch (rt) {
        case 0x00:
            return result(Op::Bltz);
        case 0x01:
            return result(Op::Bgez);
        case 0x10:
            return result(Op::Bltzal);
        case 0x11:
            return result(Op::Bgezal);
        default:
            return invalid_decode(raw, "unknown REGIMM function");
        }
    case 0x02:
        return result(Op::J);
    case 0x03:
        return result(Op::Jal);
    case 0x04:
        return result(Op::Beq);
    case 0x05:
        return result(Op::Bne);
    case 0x06:
        if (const auto invalid = require(rt == 0u, "BLEZ has nonzero rt"); invalid) {
            return *invalid;
        }
        return result(Op::Blez);
    case 0x07:
        if (const auto invalid = require(rt == 0u, "BGTZ has nonzero rt"); invalid) {
            return *invalid;
        }
        return result(Op::Bgtz);
    case 0x08:
        return result(Op::Addi);
    case 0x09:
        return result(Op::Addiu);
    case 0x0A:
        return result(Op::Slti);
    case 0x0B:
        return result(Op::Sltiu);
    case 0x0C:
        return result(Op::Andi);
    case 0x0D:
        return result(Op::Ori);
    case 0x0E:
        return result(Op::Xori);
    case 0x0F:
        if (const auto invalid = require(rs == 0u, "LUI has nonzero rs"); invalid) {
            return *invalid;
        }
        return result(Op::Lui);
    case 0x10:
        if (rs == 0x00u && shamt == 0u && funct == 0u) {
            return result(Op::Mfc0);
        }
        if (rs == 0x04u && shamt == 0u && funct == 0u) {
            return result(Op::Mtc0);
        }
        if (rs == 0x08u) {
            if (rt == 0u) {
                return result(Op::Bc0f);
            }
            if (rt == 1u) {
                return result(Op::Bc0t);
            }
            return invalid_decode(raw, "unknown COP0 branch condition");
        }
        if (rs == 0x10u && rt == 0u && rd == 0u && shamt == 0u && funct == 0x10u) {
            return result(Op::Rfe);
        }
        return invalid_decode(raw, "unknown COP0 function");
    case 0x11:
        return unavailable_coprocessor(1u);
    case 0x12:
        if (rs == 0x00u && shamt == 0u && funct == 0u) {
            return result(Op::Mfc2);
        }
        if (rs == 0x02u && shamt == 0u && funct == 0u) {
            return result(Op::Cfc2);
        }
        if (rs == 0x04u && shamt == 0u && funct == 0u) {
            return result(Op::Mtc2);
        }
        if (rs == 0x06u && shamt == 0u && funct == 0u) {
            return result(Op::Ctc2);
        }
        if (rs == 0x08u) {
            if (rt == 0u) {
                return result(Op::Bc2f);
            }
            if (rt == 1u) {
                return result(Op::Bc2t);
            }
            return invalid_decode(raw, "unknown COP2 branch condition");
        }
        if ((rs & 0x10u) != 0u) {
            return result(Op::Cop2Command);
        }
        return invalid_decode(raw, "unknown COP2 function");
    case 0x13:
        if (rs == 0x00u && shamt == 0u && funct == 0u) {
            return invalid_decode(raw, "MFC3 is reserved on R3000A");
        }
        return unavailable_coprocessor(3u);
    case 0x20:
        return result(Op::Lb);
    case 0x21:
        return result(Op::Lh);
    case 0x22:
        return result(Op::Lwl);
    case 0x23:
        return result(Op::Lw);
    case 0x24:
        return result(Op::Lbu);
    case 0x25:
        return result(Op::Lhu);
    case 0x26:
        return result(Op::Lwr);
    case 0x28:
        return result(Op::Sb);
    case 0x29:
        return result(Op::Sh);
    case 0x2A:
        return result(Op::Swl);
    case 0x2B:
        return result(Op::Sw);
    case 0x2E:
        return result(Op::Swr);
    case 0x31:
        return unavailable_coprocessor(1u);
    case 0x32:
        return result(Op::Lwc2);
    case 0x33:
        return unavailable_coprocessor(3u);
    case 0x39:
        return unavailable_coprocessor(1u);
    case 0x3A:
        return result(Op::Swc2);
    case 0x3B:
        return invalid_decode(raw, "SWC3 is reserved on R3000A");
    default:
        return invalid_decode(raw, "unknown primary opcode");
    }
}

Machine::Machine(Memory& memory, ResetState reset_state, Coprocessor2* coprocessor2)
    : memory_(memory), coprocessor2_(coprocessor2) {
    reset(reset_state);
}

void Machine::reset(ResetState reset_state) {
    cpu_ = CpuState{};
    cop0_ = Cop0State{};
    cpu_.pc = reset_state.pc;
    cpu_.next_pc = reset_state.pc + 4u;
    cop0_.status = reset_state.status;
    cop0_.prid = reset_state.processor_id;
    status_effect_delay_ = DeferredControlEffect{};
    cause_effect_delay_ = DeferredControlEffect{};
    last_exception_.reset();
}

void Machine::set_hardware_interrupts(std::uint8_t pending) noexcept {
    const std::uint32_t hardware =
        (static_cast<std::uint32_t>(pending) << 10u) & kCauseHardwareInterruptMask;
    cop0_.cause = (cop0_.cause & ~kCauseHardwareInterruptMask) | hardware;
}

bool Machine::interrupt_pending() const noexcept {
    const std::uint32_t status = effective_status();
    return (status & kStatusCurrentInterruptEnable) != 0u &&
           (status & effective_cause() & kStatusInterruptMask) != 0u;
}

bool Machine::coprocessor0_usable() const noexcept {
    const std::uint32_t status = effective_status();
    return (status & kStatusCurrentUser) == 0u ||
           (status & kStatusCoprocessorUsable0) != 0u;
}

bool Machine::coprocessor2_usable() const noexcept {
    return coprocessor2_ != nullptr &&
           (effective_status() & kStatusCoprocessorUsable2) != 0u;
}

std::uint32_t Machine::effective_status() const noexcept {
    if (!status_effect_delay_.valid) {
        return cop0_.status;
    }
    return (cop0_.status & ~kStatusDeferredEffectMask) |
           (status_effect_delay_.previous_bits & kStatusDeferredEffectMask);
}

std::uint32_t Machine::effective_cause() const noexcept {
    if (!cause_effect_delay_.valid) {
        return cop0_.cause;
    }
    return (cop0_.cause & ~kCauseSoftwareInterruptMask) |
           (cause_effect_delay_.previous_bits & kCauseSoftwareInterruptMask);
}

std::uint32_t Machine::read_gpr(std::uint8_t reg) const noexcept {
    return reg == 0u || reg >= cpu_.gpr.size() ? 0u : cpu_.gpr[reg];
}

std::uint32_t Machine::read_merge_source(std::uint8_t reg) const noexcept {
    if (cpu_.load_delay.valid && cpu_.load_delay.merge_load && cpu_.load_delay.reg == reg) {
        return cpu_.load_delay.value;
    }
    return read_gpr(reg);
}

void Machine::set_register_write(RegisterWrite& write, std::uint8_t reg,
                                 std::uint32_t value) noexcept {
    if (reg == 0u || reg >= 32u) {
        write = RegisterWrite{};
        return;
    }
    write.valid = true;
    write.reg = reg;
    write.value = value;
}

void Machine::set_load_write(LoadDelayState& write, std::uint8_t reg,
                             std::uint32_t value, std::uint32_t issue_pc,
                             bool merge_load) noexcept {
    if (reg == 0u || reg >= 32u) {
        write = LoadDelayState{};
        return;
    }
    write.valid = true;
    write.reg = reg;
    write.value = value;
    write.issue_pc = issue_pc;
    write.merge_load = merge_load;
}

void Machine::set_coprocessor_write(CoprocessorWriteState& write,
                                    CoprocessorWriteTarget target,
                                    std::uint8_t reg, std::uint32_t value,
                                    std::uint32_t issue_pc) noexcept {
    if (reg >= 32u) {
        write = CoprocessorWriteState{};
        return;
    }
    write.target = target;
    write.reg = reg;
    write.value = value;
    write.issue_pc = issue_pc;
}

void Machine::commit_register_write(const RegisterWrite& write) noexcept {
    if (write.valid && write.reg != 0u && write.reg < cpu_.gpr.size()) {
        cpu_.gpr[write.reg] = write.value;
    }
    cpu_.gpr[0] = 0u;
}

void Machine::commit_load(const LoadDelayState& load) noexcept {
    if (load.valid && load.reg != 0u && load.reg < cpu_.gpr.size()) {
        cpu_.gpr[load.reg] = load.value;
    }
    cpu_.gpr[0] = 0u;
}

void Machine::commit_coprocessor_write(const CoprocessorWriteState& write) noexcept {
    if (write.reg >= 32u) {
        return;
    }
    switch (write.target) {
    case CoprocessorWriteTarget::None:
        break;
    case CoprocessorWriteTarget::Cop0:
        (void)cop0_.write(write.reg, write.value);
        break;
    case CoprocessorWriteTarget::Cop2Data:
        if (coprocessor2_ != nullptr) {
            coprocessor2_->write_data(write.reg, write.value);
        }
        break;
    case CoprocessorWriteTarget::Cop2Control:
        if (coprocessor2_ != nullptr) {
            coprocessor2_->write_control(write.reg, write.value);
        }
        break;
    }
}

void Machine::advance_deferred_control_effects() noexcept {
    const auto advance = [](DeferredControlEffect& effect) {
        if (!effect.valid) {
            return;
        }
        if (effect.instructions_remaining > 0u) {
            --effect.instructions_remaining;
        }
        if (effect.instructions_remaining == 0u) {
            effect = DeferredControlEffect{};
        }
    };
    advance(status_effect_delay_);
    advance(cause_effect_delay_);
}

StepResult Machine::step_word(std::uint32_t raw) {
    const DecodeResult decoded = decode(raw);
    if (!decoded.instruction.has_value() || !decoded.error.empty()) {
        Instruction invalid;
        invalid.op = Op::Reserved;
        invalid.raw = raw;
        return step(invalid);
    }
    return step(decoded.instruction.value());
}

StepResult Machine::step(const Instruction& instruction) {
    const std::uint32_t instr_pc = cpu_.pc;
    const bool was_delay_slot = cpu_.branch_pending;
    const std::uint32_t pending_branch_target = cpu_.branch_target;
    const std::uint32_t pending_branch_pc = cpu_.branch_pc;
    const LoadDelayState previous_load = cpu_.load_delay;
    const CoprocessorWriteState previous_coprocessor_write = cpu_.coprocessor_write;
    CycleMetadata timing = cycle_metadata(instruction.op);
    cpu_.in_delay_slot = was_delay_slot;
    last_exception_.reset();

    RegisterWrite register_write;
    LoadDelayState load_write;
    CoprocessorWriteState coprocessor_write;
    std::optional<std::uint32_t> status_effect_previous;
    std::optional<std::uint32_t> cause_effect_previous;
    ControlTransfer transfer;
    std::optional<ExceptionCode> fault;
    std::uint32_t fault_address = 0;
    bool has_fault_address = false;
    std::optional<std::uint8_t> fault_coprocessor;
    std::string fault_detail;

    const auto fail = [&](ExceptionCode code, std::string detail,
                          std::uint32_t address = 0u, bool has_address = false,
                          std::optional<std::uint8_t> coprocessor = std::nullopt) {
        fault = code;
        fault_detail = std::move(detail);
        fault_address = address;
        has_fault_address = has_address;
        fault_coprocessor = coprocessor;
    };
    const auto effective_address = [&]() {
        return add_offset(read_gpr(instruction.rs), sign_extend16(instruction.immediate));
    };
    const auto schedule_load = [&](std::uint8_t reg, std::uint32_t value, bool merge) {
        set_load_write(load_write, reg, value, instr_pc, merge);
    };
    const auto branch_if = [&](bool condition) {
        transfer.valid = true;
        transfer.target = condition ? branch_target(instr_pc, instruction.immediate)
                                    : instr_pc + 8u;
    };
    const auto address_allowed = [&](std::uint32_t address, AccessType access) {
        if ((effective_status() & kStatusCurrentUser) == 0u ||
            !is_kernel_address(address)) {
            return true;
        }
        const ExceptionCode code = access == AccessType::Load
                                       ? ExceptionCode::AddressErrorLoad
                                       : ExceptionCode::AddressErrorStore;
        fail(code, "user-mode access to a kernel segment", address, true);
        return false;
    };

    if ((instr_pc & 3u) != 0u) {
        fail(ExceptionCode::AddressErrorLoad, "instruction PC is not word aligned", instr_pc, true);
        timing = CycleMetadata{0u, 0u, 0u, false};
    } else if ((effective_status() & kStatusCurrentUser) != 0u &&
               is_kernel_address(instr_pc)) {
        fail(ExceptionCode::AddressErrorLoad, "user-mode instruction fetch from a kernel segment",
             instr_pc, true);
        timing = CycleMetadata{0u, 0u, 0u, false};
    } else if (interrupt_pending()) {
        fail(ExceptionCode::Interrupt, "enabled interrupt pending");
        timing = CycleMetadata{0u, 0u, 0u, false};
    } else if (!instruction_fields_valid(instruction)) {
        fail(ExceptionCode::ReservedInstruction,
             "decoded instruction contains an out-of-range field");
    } else if (was_delay_slot && is_control_transfer(instruction.op)) {
        fail(ExceptionCode::ReservedInstruction, "control transfer in a branch delay slot");
    } else if (is_coprocessor0_operation(instruction.op) && !coprocessor0_usable()) {
        fail(ExceptionCode::CoprocessorUnusable, "COP0 is unusable in the current mode",
             0u, false, 0u);
    } else if (is_coprocessor2_operation(instruction.op) && !coprocessor2_usable()) {
        fail(ExceptionCode::CoprocessorUnusable, "COP2 is disabled or not attached",
             0u, false, 2u);
    } else {
        switch (instruction.op) {
        case Op::Reserved:
            fail(ExceptionCode::ReservedInstruction, "reserved instruction");
            break;
        case Op::Nop:
            break;
        case Op::Sll:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rt) << instruction.shamt);
            break;
        case Op::Srl:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rt) >> instruction.shamt);
            break;
        case Op::Sra:
            set_register_write(register_write, instruction.rd,
                               arithmetic_right_shift(read_gpr(instruction.rt), instruction.shamt));
            break;
        case Op::Sllv:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rt) << (read_gpr(instruction.rs) & 0x1Fu));
            break;
        case Op::Srlv:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rt) >> (read_gpr(instruction.rs) & 0x1Fu));
            break;
        case Op::Srav:
            set_register_write(register_write, instruction.rd,
                               arithmetic_right_shift(read_gpr(instruction.rt),
                                                      read_gpr(instruction.rs) & 0x1Fu));
            break;
        case Op::Jr:
            transfer.valid = true;
            transfer.target = read_gpr(instruction.rs);
            break;
        case Op::Jalr:
            transfer.target = read_gpr(instruction.rs);
            if ((transfer.target & 3u) != 0u) {
                fail(ExceptionCode::AddressErrorLoad, "JALR target is not word aligned",
                     transfer.target, true);
            } else if ((effective_status() & kStatusCurrentUser) != 0u &&
                       is_kernel_address(transfer.target)) {
                fail(ExceptionCode::AddressErrorLoad,
                     "JALR target is outside the current accessible address space",
                     transfer.target, true);
            } else {
                transfer.valid = true;
                set_register_write(register_write, instruction.rd, instr_pc + 8u);
            }
            break;
        case Op::Mfhi:
            set_register_write(register_write, instruction.rd, cpu_.hi);
            break;
        case Op::Mthi:
            cpu_.hi = read_gpr(instruction.rs);
            break;
        case Op::Mflo:
            set_register_write(register_write, instruction.rd, cpu_.lo);
            break;
        case Op::Mtlo:
            cpu_.lo = read_gpr(instruction.rs);
            break;
        case Op::Mult: {
            const std::int64_t product = static_cast<std::int64_t>(signed_bits(read_gpr(instruction.rs))) *
                                         static_cast<std::int64_t>(signed_bits(read_gpr(instruction.rt)));
            const std::uint64_t bits = static_cast<std::uint64_t>(product);
            cpu_.hi = static_cast<std::uint32_t>(bits >> 32u);
            cpu_.lo = static_cast<std::uint32_t>(bits);
            break;
        }
        case Op::Multu: {
            const std::uint64_t product = static_cast<std::uint64_t>(read_gpr(instruction.rs)) *
                                          static_cast<std::uint64_t>(read_gpr(instruction.rt));
            cpu_.hi = static_cast<std::uint32_t>(product >> 32u);
            cpu_.lo = static_cast<std::uint32_t>(product);
            break;
        }
        case Op::Div: {
            const std::int32_t dividend = signed_bits(read_gpr(instruction.rs));
            const std::int32_t divisor = signed_bits(read_gpr(instruction.rt));
            if (divisor == 0) {
                cpu_.lo = dividend < 0 ? 1u : std::numeric_limits<std::uint32_t>::max();
                cpu_.hi = unsigned_bits(dividend);
            } else if (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
                cpu_.lo = unsigned_bits(std::numeric_limits<std::int32_t>::min());
                cpu_.hi = 0u;
            } else {
                cpu_.lo = unsigned_bits(dividend / divisor);
                cpu_.hi = unsigned_bits(dividend % divisor);
            }
            break;
        }
        case Op::Divu: {
            const std::uint32_t dividend = read_gpr(instruction.rs);
            const std::uint32_t divisor = read_gpr(instruction.rt);
            if (divisor == 0u) {
                cpu_.lo = std::numeric_limits<std::uint32_t>::max();
                cpu_.hi = dividend;
            } else {
                cpu_.lo = dividend / divisor;
                cpu_.hi = dividend % divisor;
            }
            break;
        }
        case Op::Add: {
            const std::int32_t lhs = signed_bits(read_gpr(instruction.rs));
            const std::int32_t rhs = signed_bits(read_gpr(instruction.rt));
            const std::int64_t wide = static_cast<std::int64_t>(lhs) + static_cast<std::int64_t>(rhs);
            if (wide > std::numeric_limits<std::int32_t>::max() ||
                wide < std::numeric_limits<std::int32_t>::min()) {
                fail(ExceptionCode::Overflow, "signed ADD overflow");
            } else {
                const std::int32_t result = static_cast<std::int32_t>(wide);
                set_register_write(register_write, instruction.rd, unsigned_bits(result));
            }
            break;
        }
        case Op::Addu:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) + read_gpr(instruction.rt));
            break;
        case Op::Sub: {
            const std::int32_t lhs = signed_bits(read_gpr(instruction.rs));
            const std::int32_t rhs = signed_bits(read_gpr(instruction.rt));
            const std::int64_t wide = static_cast<std::int64_t>(lhs) - static_cast<std::int64_t>(rhs);
            if (wide > std::numeric_limits<std::int32_t>::max() ||
                wide < std::numeric_limits<std::int32_t>::min()) {
                fail(ExceptionCode::Overflow, "signed SUB overflow");
            } else {
                const std::int32_t result = static_cast<std::int32_t>(wide);
                set_register_write(register_write, instruction.rd, unsigned_bits(result));
            }
            break;
        }
        case Op::Subu:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) - read_gpr(instruction.rt));
            break;
        case Op::And:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) & read_gpr(instruction.rt));
            break;
        case Op::Or:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) | read_gpr(instruction.rt));
            break;
        case Op::Xor:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) ^ read_gpr(instruction.rt));
            break;
        case Op::Nor:
            set_register_write(register_write, instruction.rd,
                               ~(read_gpr(instruction.rs) | read_gpr(instruction.rt)));
            break;
        case Op::Slt:
            set_register_write(register_write, instruction.rd,
                               signed_bits(read_gpr(instruction.rs)) <
                                       signed_bits(read_gpr(instruction.rt))
                                   ? 1u
                                   : 0u);
            break;
        case Op::Sltu:
            set_register_write(register_write, instruction.rd,
                               read_gpr(instruction.rs) < read_gpr(instruction.rt) ? 1u : 0u);
            break;
        case Op::Syscall:
            fail(ExceptionCode::Syscall, "SYSCALL");
            break;
        case Op::Break:
            fail(ExceptionCode::Breakpoint, "BREAK");
            break;
        case Op::J:
            transfer.valid = true;
            transfer.target = ((instr_pc + 4u) & 0xF0000000u) | (instruction.target << 2u);
            break;
        case Op::Jal:
            transfer.valid = true;
            transfer.target = ((instr_pc + 4u) & 0xF0000000u) | (instruction.target << 2u);
            set_register_write(register_write, 31u, instr_pc + 8u);
            break;
        case Op::Beq:
            branch_if(read_gpr(instruction.rs) == read_gpr(instruction.rt));
            break;
        case Op::Bne:
            branch_if(read_gpr(instruction.rs) != read_gpr(instruction.rt));
            break;
        case Op::Blez:
            branch_if(signed_bits(read_gpr(instruction.rs)) <= 0);
            break;
        case Op::Bgtz:
            branch_if(signed_bits(read_gpr(instruction.rs)) > 0);
            break;
        case Op::Bltz:
            branch_if(signed_bits(read_gpr(instruction.rs)) < 0);
            break;
        case Op::Bgez:
            branch_if(signed_bits(read_gpr(instruction.rs)) >= 0);
            break;
        case Op::Bltzal: {
            const bool taken = signed_bits(read_gpr(instruction.rs)) < 0;
            branch_if(taken);
            set_register_write(register_write, 31u, instr_pc + 8u);
            break;
        }
        case Op::Bgezal: {
            const bool taken = signed_bits(read_gpr(instruction.rs)) >= 0;
            branch_if(taken);
            set_register_write(register_write, 31u, instr_pc + 8u);
            break;
        }
        case Op::Addi: {
            const std::int32_t lhs = signed_bits(read_gpr(instruction.rs));
            const std::int32_t rhs = sign_extend16(instruction.immediate);
            const std::int64_t wide = static_cast<std::int64_t>(lhs) + static_cast<std::int64_t>(rhs);
            if (wide > std::numeric_limits<std::int32_t>::max() ||
                wide < std::numeric_limits<std::int32_t>::min()) {
                fail(ExceptionCode::Overflow, "signed ADDI overflow");
            } else {
                const std::int32_t result = static_cast<std::int32_t>(wide);
                set_register_write(register_write, instruction.rt, unsigned_bits(result));
            }
            break;
        }
        case Op::Addiu:
            set_register_write(register_write, instruction.rt,
                               add_offset(read_gpr(instruction.rs),
                                          sign_extend16(instruction.immediate)));
            break;
        case Op::Slti:
            set_register_write(register_write, instruction.rt,
                               signed_bits(read_gpr(instruction.rs)) <
                                       sign_extend16(instruction.immediate)
                                   ? 1u
                                   : 0u);
            break;
        case Op::Sltiu:
            set_register_write(register_write, instruction.rt,
                               read_gpr(instruction.rs) <
                                       unsigned_bits(sign_extend16(instruction.immediate))
                                   ? 1u
                                   : 0u);
            break;
        case Op::Andi:
            set_register_write(register_write, instruction.rt,
                               read_gpr(instruction.rs) & instruction.immediate);
            break;
        case Op::Ori:
            set_register_write(register_write, instruction.rt,
                               read_gpr(instruction.rs) | instruction.immediate);
            break;
        case Op::Xori:
            set_register_write(register_write, instruction.rt,
                               read_gpr(instruction.rs) ^ instruction.immediate);
            break;
        case Op::Lui:
            set_register_write(register_write, instruction.rt,
                               static_cast<std::uint32_t>(instruction.immediate) << 16u);
            break;
        case Op::Mfc0: {
            const std::optional<std::uint32_t> value = cop0_.read(instruction.rd);
            if (!value.has_value()) {
                fail(ExceptionCode::ReservedInstruction, "MFC0 references an unavailable register");
            } else {
                schedule_load(instruction.rt, *value, false);
            }
            break;
        }
        case Op::Mtc0:
        {
            const std::uint32_t value = read_gpr(instruction.rt);
            Cop0State probe = cop0_;
            if (!probe.write(instruction.rd, value)) {
                fail(ExceptionCode::ReservedInstruction, "MTC0 references a read-only register");
            } else {
                set_coprocessor_write(coprocessor_write, CoprocessorWriteTarget::Cop0,
                                      instruction.rd, value, instr_pc);
                if (instruction.rd == 12u) {
                    status_effect_previous = effective_status() & kStatusDeferredEffectMask;
                } else if (instruction.rd == 13u) {
                    cause_effect_previous = effective_cause() & kCauseSoftwareInterruptMask;
                }
            }
            break;
        }
        case Op::Rfe: {
            const std::uint32_t mode = cop0_.status & kStatusModeMask;
            const std::uint32_t restored = (mode & 0x3Cu) >> 2u;
            cop0_.status = (cop0_.status & ~0x0Fu) | restored;
            break;
        }
        case Op::Bc0f:
            branch_if(!coprocessor0_branch_condition_);
            break;
        case Op::Bc0t:
            branch_if(coprocessor0_branch_condition_);
            break;
        case Op::Mfc2:
            schedule_load(instruction.rt, coprocessor2_->read_data(instruction.rd), false);
            break;
        case Op::Cfc2:
            schedule_load(instruction.rt, coprocessor2_->read_control(instruction.rd), false);
            break;
        case Op::Mtc2:
            set_coprocessor_write(coprocessor_write, CoprocessorWriteTarget::Cop2Data,
                                  instruction.rd, read_gpr(instruction.rt), instr_pc);
            break;
        case Op::Ctc2:
            set_coprocessor_write(coprocessor_write, CoprocessorWriteTarget::Cop2Control,
                                  instruction.rd, read_gpr(instruction.rt), instr_pc);
            break;
        case Op::Bc2f:
            branch_if(!coprocessor2_->branch_condition());
            break;
        case Op::Bc2t:
            branch_if(coprocessor2_->branch_condition());
            break;
        case Op::Cop2Command:
            timing.result_latency_cycles =
                std::max(1u, coprocessor2_->execute_command(instruction.raw));
            timing.side_effect_latency_cycles = timing.result_latency_cycles;
            break;
        case Op::UnavailableCoprocessor:
            fail(ExceptionCode::CoprocessorUnusable, "coprocessor is not attached",
                 0u, false, instruction.coprocessor);
            break;
        case Op::Lb: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            const std::optional<std::uint8_t> value = memory_.load8(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LB backing memory read failed");
            } else {
                const std::int8_t signed_value = std::bit_cast<std::int8_t>(*value);
                schedule_load(instruction.rt, unsigned_bits(static_cast<std::int32_t>(signed_value)), false);
            }
            break;
        }
        case Op::Lh: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            if ((address & 1u) != 0u) {
                fail(ExceptionCode::AddressErrorLoad, "LH address is not halfword aligned", address, true);
                break;
            }
            const std::optional<std::uint16_t> value = memory_.load16le(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LH backing memory read failed");
            } else {
                const std::int16_t signed_value = std::bit_cast<std::int16_t>(*value);
                schedule_load(instruction.rt, unsigned_bits(static_cast<std::int32_t>(signed_value)), false);
            }
            break;
        }
        case Op::Lwl: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            const std::uint32_t aligned = address & ~3u;
            const std::optional<std::uint32_t> word = memory_.load32le(aligned);
            if (!word.has_value()) {
                fail(ExceptionCode::BusErrorData, "LWL backing memory read failed");
                break;
            }
            const std::uint32_t old_value = read_merge_source(instruction.rt);
            const std::uint32_t merged = [&] {
                switch (address & 3u) {
                case 0u:
                    return (old_value & 0x00FFFFFFu) | (*word << 24u);
                case 1u:
                    return (old_value & 0x0000FFFFu) | (*word << 16u);
                case 2u:
                    return (old_value & 0x000000FFu) | (*word << 8u);
                default:
                    return *word;
                }
            }();
            schedule_load(instruction.rt, merged, true);
            break;
        }
        case Op::Lw: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            if ((address & 3u) != 0u) {
                fail(ExceptionCode::AddressErrorLoad, "LW address is not word aligned", address, true);
                break;
            }
            const std::optional<std::uint32_t> value = memory_.load32le(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LW backing memory read failed");
            } else {
                schedule_load(instruction.rt, *value, false);
            }
            break;
        }
        case Op::Lbu: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            const std::optional<std::uint8_t> value = memory_.load8(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LBU backing memory read failed");
            } else {
                schedule_load(instruction.rt, *value, false);
            }
            break;
        }
        case Op::Lhu: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            if ((address & 1u) != 0u) {
                fail(ExceptionCode::AddressErrorLoad, "LHU address is not halfword aligned", address, true);
                break;
            }
            const std::optional<std::uint16_t> value = memory_.load16le(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LHU backing memory read failed");
            } else {
                schedule_load(instruction.rt, *value, false);
            }
            break;
        }
        case Op::Lwr: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            const std::uint32_t aligned = address & ~3u;
            const std::optional<std::uint32_t> word = memory_.load32le(aligned);
            if (!word.has_value()) {
                fail(ExceptionCode::BusErrorData, "LWR backing memory read failed");
                break;
            }
            const std::uint32_t old_value = read_merge_source(instruction.rt);
            const std::uint32_t merged = [&] {
                switch (address & 3u) {
                case 0u:
                    return *word;
                case 1u:
                    return (old_value & 0xFF000000u) | (*word >> 8u);
                case 2u:
                    return (old_value & 0xFFFF0000u) | (*word >> 16u);
                default:
                    return (old_value & 0xFFFFFF00u) | (*word >> 24u);
                }
            }();
            schedule_load(instruction.rt, merged, true);
            break;
        }
        case Op::Sb: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            if (!memory_.store8(address, static_cast<std::uint8_t>(read_gpr(instruction.rt)))) {
                fail(ExceptionCode::BusErrorData, "SB backing memory write failed");
            }
            break;
        }
        case Op::Sh: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            if ((address & 1u) != 0u) {
                fail(ExceptionCode::AddressErrorStore, "SH address is not halfword aligned", address, true);
                break;
            }
            if (!memory_.store16le(address, static_cast<std::uint16_t>(read_gpr(instruction.rt)))) {
                fail(ExceptionCode::BusErrorData, "SH backing memory write failed");
            }
            break;
        }
        case Op::Swl: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            const std::uint32_t aligned = address & ~3u;
            const std::optional<std::uint32_t> old_word = memory_.load32le(aligned);
            if (!old_word.has_value()) {
                fail(ExceptionCode::BusErrorData, "SWL backing memory read failed");
                break;
            }
            const std::uint32_t value = read_gpr(instruction.rt);
            const std::uint32_t merged = [&] {
                switch (address & 3u) {
                case 0u:
                    return (*old_word & 0xFFFFFF00u) | (value >> 24u);
                case 1u:
                    return (*old_word & 0xFFFF0000u) | (value >> 16u);
                case 2u:
                    return (*old_word & 0xFF000000u) | (value >> 8u);
                default:
                    return value;
                }
            }();
            if (!memory_.store32le(aligned, merged)) {
                fail(ExceptionCode::BusErrorData, "SWL backing memory write failed");
            }
            break;
        }
        case Op::Sw: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            if ((address & 3u) != 0u) {
                fail(ExceptionCode::AddressErrorStore, "SW address is not word aligned", address, true);
                break;
            }
            if (!memory_.store32le(address, read_gpr(instruction.rt))) {
                fail(ExceptionCode::BusErrorData, "SW backing memory write failed");
            }
            break;
        }
        case Op::Swr: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            const std::uint32_t aligned = address & ~3u;
            const std::optional<std::uint32_t> old_word = memory_.load32le(aligned);
            if (!old_word.has_value()) {
                fail(ExceptionCode::BusErrorData, "SWR backing memory read failed");
                break;
            }
            const std::uint32_t value = read_gpr(instruction.rt);
            const std::uint32_t merged = [&] {
                switch (address & 3u) {
                case 0u:
                    return value;
                case 1u:
                    return (*old_word & 0x000000FFu) | (value << 8u);
                case 2u:
                    return (*old_word & 0x0000FFFFu) | (value << 16u);
                default:
                    return (*old_word & 0x00FFFFFFu) | (value << 24u);
                }
            }();
            if (!memory_.store32le(aligned, merged)) {
                fail(ExceptionCode::BusErrorData, "SWR backing memory write failed");
            }
            break;
        }
        case Op::Lwc2: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Load)) {
                break;
            }
            if ((address & 3u) != 0u) {
                fail(ExceptionCode::AddressErrorLoad, "LWC2 address is not word aligned",
                     address, true);
                break;
            }
            const std::optional<std::uint32_t> value = memory_.load32le(address);
            if (!value.has_value()) {
                fail(ExceptionCode::BusErrorData, "LWC2 backing memory read failed");
            } else {
                set_coprocessor_write(coprocessor_write,
                                      CoprocessorWriteTarget::Cop2Data,
                                      instruction.rt, *value, instr_pc);
            }
            break;
        }
        case Op::Swc2: {
            const std::uint32_t address = effective_address();
            if (!address_allowed(address, AccessType::Store)) {
                break;
            }
            if ((address & 3u) != 0u) {
                fail(ExceptionCode::AddressErrorStore, "SWC2 address is not word aligned",
                     address, true);
                break;
            }
            if (!memory_.store32le(address, coprocessor2_->read_data(instruction.rt))) {
                fail(ExceptionCode::BusErrorData, "SWC2 backing memory write failed");
            }
            break;
        }
        }
    }

    commit_load(previous_load);
    commit_coprocessor_write(previous_coprocessor_write);
    if (fault.has_value()) {
        StepResult result = raise_exception(*fault, instr_pc, was_delay_slot, pending_branch_pc,
                                            fault_address, has_fault_address,
                                            fault_coprocessor, std::move(fault_detail));
        result.cycles = timing;
        return result;
    }

    commit_register_write(register_write);
    cpu_.load_delay = load_write;
    cpu_.coprocessor_write = coprocessor_write;
    advance_deferred_control_effects();
    if (status_effect_previous.has_value()) {
        status_effect_delay_ =
            DeferredControlEffect{true, *status_effect_previous, 2u};
    }
    if (cause_effect_previous.has_value()) {
        cause_effect_delay_ =
            DeferredControlEffect{true, *cause_effect_previous, 2u};
    }

    if (was_delay_slot) {
        cpu_.pc = pending_branch_target;
        cpu_.next_pc = pending_branch_target + 4u;
        cpu_.branch_pending = false;
        cpu_.branch_target = 0u;
        cpu_.branch_pc = 0u;
    } else if (transfer.valid) {
        cpu_.pc = instr_pc + 4u;
        cpu_.next_pc = transfer.target;
        cpu_.branch_pending = true;
        cpu_.branch_target = transfer.target;
        cpu_.branch_pc = instr_pc;
    } else {
        cpu_.pc = instr_pc + 4u;
        cpu_.next_pc = instr_pc + 8u;
        cpu_.branch_pending = false;
        cpu_.branch_target = 0u;
        cpu_.branch_pc = 0u;
    }
    cpu_.in_delay_slot = cpu_.branch_pending;
    cpu_.gpr[0] = 0u;
    return StepResult{true, std::nullopt, instr_pc, cpu_.pc, was_delay_slot, timing};
}

StepResult Machine::raise_exception(ExceptionCode code, std::uint32_t fault_pc,
                                    bool in_delay_slot, std::uint32_t branch_pc,
                                    std::uint32_t bad_vaddr, bool has_bad_vaddr,
                                    std::optional<std::uint8_t> coprocessor,
                                    std::string detail) {
    const std::uint32_t epc = in_delay_slot ? branch_pc : fault_pc;
    cop0_.epc = epc;
    if (has_bad_vaddr) {
        cop0_.bad_vaddr = bad_vaddr;
    }
    cop0_.cause &= ~(kCauseBd | kCauseCoprocessorMask | kCauseExceptionMask);
    cop0_.cause |= (static_cast<std::uint32_t>(code) << 2u);
    if (coprocessor.has_value()) {
        cop0_.cause |= (static_cast<std::uint32_t>(*coprocessor) & 3u)
                       << kCauseCoprocessorShift;
    }
    if (in_delay_slot) {
        cop0_.cause |= kCauseBd;
    }
    const std::uint32_t mode = effective_status() & kStatusModeMask;
    // Exception entry pushes current into previous and previous into old. The
    // new current pair is zero: kernel mode with interrupts disabled.
    const std::uint32_t pushed = (mode << 2u) & 0x3Cu;
    cop0_.status = (cop0_.status & ~kStatusModeMask) | pushed;

    ExceptionMetadata metadata;
    metadata.code = code;
    metadata.fault_pc = fault_pc;
    metadata.epc = epc;
    metadata.bad_vaddr = bad_vaddr;
    metadata.has_bad_vaddr = has_bad_vaddr;
    metadata.coprocessor = coprocessor.value_or(0u);
    metadata.has_coprocessor = coprocessor.has_value();
    metadata.in_delay_slot = in_delay_slot;
    metadata.branch_pc = branch_pc;
    metadata.cause = cop0_.cause;
    metadata.status = cop0_.status;
    metadata.detail = std::move(detail);
    last_exception_ = metadata;

    const std::uint32_t vector = exception_vector();
    cpu_.pc = vector;
    cpu_.next_pc = vector + 4u;
    cpu_.branch_pending = false;
    cpu_.branch_target = 0u;
    cpu_.branch_pc = 0u;
    cpu_.in_delay_slot = false;
    cpu_.load_delay = LoadDelayState{};
    cpu_.coprocessor_write = CoprocessorWriteState{};
    status_effect_delay_ = DeferredControlEffect{};
    cause_effect_delay_ = DeferredControlEffect{};
    cpu_.gpr[0] = 0u;

    return StepResult{false, metadata, fault_pc, cpu_.pc, in_delay_slot};
}

std::uint32_t Machine::exception_vector() const noexcept {
    return (cop0_.status & kStatusBev) != 0u ? kBootExceptionVector
                                             : kGeneralExceptionVector;
}

} // namespace r3000a::reference
