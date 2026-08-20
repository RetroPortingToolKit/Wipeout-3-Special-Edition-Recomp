#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace r3000a::reference {

inline constexpr std::uint32_t kStatusModeMask = 0x0000003Fu;
inline constexpr std::uint32_t kStatusBev = 1u << 22;
inline constexpr std::uint32_t kCauseBd = 1u << 31;
inline constexpr std::uint32_t kCauseExceptionMask = 0x0000007Cu;

enum class AccessType : std::uint8_t {
    Load,
    Store,
};

enum class ExceptionCode : std::uint8_t {
    Interrupt = 0,
    TlbLoad = 2,
    TlbStore = 3,
    AddressErrorLoad = 4,
    AddressErrorStore = 5,
    BusErrorInstruction = 6,
    BusErrorData = 7,
    Syscall = 8,
    Breakpoint = 9,
    ReservedInstruction = 10,
    CoprocessorUnusable = 11,
    Overflow = 12,
};

struct ExceptionMetadata {
    ExceptionCode code = ExceptionCode::ReservedInstruction;
    std::uint32_t fault_pc = 0;
    std::uint32_t epc = 0;
    std::uint32_t bad_vaddr = 0;
    bool has_bad_vaddr = false;
    bool in_delay_slot = false;
    std::uint32_t branch_pc = 0;
    std::uint32_t cause = 0;
    std::uint32_t status = 0;
    std::string detail;
};

struct Cop0State {
    std::uint32_t index = 0;
    std::uint32_t random = 0;
    std::uint32_t entry_lo = 0;
    std::uint32_t bad_vaddr = 0;
    std::uint32_t context = 0;
    std::uint32_t entry_hi = 0;
    std::uint32_t status = 0;
    std::uint32_t cause = 0;
    std::uint32_t epc = 0;
    std::uint32_t prid = 0x00000002u;

    [[nodiscard]] std::optional<std::uint32_t> read(std::uint8_t reg) const noexcept;
    [[nodiscard]] bool write(std::uint8_t reg, std::uint32_t value) noexcept;
};

struct LoadDelayState {
    bool valid = false;
    std::uint8_t reg = 0;
    std::uint32_t value = 0;
    std::uint32_t issue_pc = 0;
    bool merge_load = false;
};

struct CpuState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi = 0;
    std::uint32_t lo = 0;
    std::uint32_t pc = 0;
    std::uint32_t next_pc = 4;

    // These fields are the explicit control-flow pipeline state. After a
    // branch is retired, pc names its delay slot and branch_pending remains
    // true until that slot retires.
    bool branch_pending = false;
    std::uint32_t branch_target = 0;
    std::uint32_t branch_pc = 0;
    bool in_delay_slot = false;

    LoadDelayState load_delay{};
};

class Memory final {
public:
    Memory(std::uint32_t base, std::size_t size);

    [[nodiscard]] std::uint32_t base() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

    void fill(std::uint8_t value) noexcept;
    [[nodiscard]] std::optional<std::uint8_t> load8(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint16_t> load16le(std::uint32_t address) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> load32le(std::uint32_t address) const noexcept;
    [[nodiscard]] bool store8(std::uint32_t address, std::uint8_t value) noexcept;
    [[nodiscard]] bool store16le(std::uint32_t address, std::uint16_t value) noexcept;
    [[nodiscard]] bool store32le(std::uint32_t address, std::uint32_t value) noexcept;

    [[nodiscard]] std::vector<std::uint8_t> snapshot() const { return bytes_; }

private:
    [[nodiscard]] std::optional<std::size_t> offset(std::uint32_t address,
                                                    std::size_t width) const noexcept;

    std::uint32_t base_ = 0;
    std::vector<std::uint8_t> bytes_;
};

enum class Op : std::uint8_t {
    Reserved,
    Nop,
    Sll,
    Srl,
    Sra,
    Sllv,
    Srlv,
    Srav,
    Jr,
    Jalr,
    Mfhi,
    Mthi,
    Mflo,
    Mtlo,
    Mult,
    Multu,
    Div,
    Divu,
    Add,
    Addu,
    Sub,
    Subu,
    And,
    Or,
    Xor,
    Nor,
    Slt,
    Sltu,
    Syscall,
    Break,
    J,
    Jal,
    Beq,
    Bne,
    Blez,
    Bgtz,
    Bltz,
    Bgez,
    Bltzal,
    Bgezal,
    Addi,
    Addiu,
    Slti,
    Sltiu,
    Andi,
    Ori,
    Xori,
    Lui,
    Mfc0,
    Mtc0,
    Rfe,
    Lb,
    Lh,
    Lwl,
    Lw,
    Lbu,
    Lhu,
    Lwr,
    Sb,
    Sh,
    Swl,
    Sw,
    Swr,
};

struct Instruction {
    Op op = Op::Reserved;
    std::uint8_t rs = 0;
    std::uint8_t rt = 0;
    std::uint8_t rd = 0;
    std::uint8_t shamt = 0;
    std::uint16_t immediate = 0;
    std::uint32_t target = 0;
    std::uint32_t raw = 0;
};

struct DecodeResult {
    std::optional<Instruction> instruction;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return instruction.has_value();
    }
};

[[nodiscard]] DecodeResult decode(std::uint32_t raw);

struct StepResult {
    bool retired = false;
    std::optional<ExceptionMetadata> exception;
    std::uint32_t pc_before = 0;
    std::uint32_t pc_after = 0;
    bool was_delay_slot = false;
};

class Machine final {
public:
    explicit Machine(Memory& memory, std::uint32_t reset_pc = 0x80000000u);

    void reset(std::uint32_t reset_pc);

    [[nodiscard]] CpuState& cpu() noexcept { return cpu_; }
    [[nodiscard]] const CpuState& cpu() const noexcept { return cpu_; }
    [[nodiscard]] Cop0State& cop0() noexcept { return cop0_; }
    [[nodiscard]] const Cop0State& cop0() const noexcept { return cop0_; }
    [[nodiscard]] Memory& memory() noexcept { return memory_; }
    [[nodiscard]] const Memory& memory() const noexcept { return memory_; }
    [[nodiscard]] const std::optional<ExceptionMetadata>& last_exception() const noexcept {
        return last_exception_;
    }

    [[nodiscard]] StepResult step_word(std::uint32_t raw);
    [[nodiscard]] StepResult step(const Instruction& instruction);

    [[nodiscard]] std::uint32_t exception_vector() const noexcept;

private:
    struct RegisterWrite {
        bool valid = false;
        std::uint8_t reg = 0;
        std::uint32_t value = 0;
    };

    struct ControlTransfer {
        bool valid = false;
        std::uint32_t target = 0;
    };

    [[nodiscard]] std::uint32_t read_gpr(std::uint8_t reg) const noexcept;
    [[nodiscard]] std::uint32_t read_merge_source(std::uint8_t reg) const noexcept;
    static void set_register_write(RegisterWrite& write, std::uint8_t reg,
                                   std::uint32_t value) noexcept;
    static void set_load_write(LoadDelayState& write, std::uint8_t reg,
                               std::uint32_t value, std::uint32_t issue_pc,
                               bool merge_load) noexcept;
    void commit_register_write(const RegisterWrite& write) noexcept;
    void commit_load(const LoadDelayState& load) noexcept;

    [[nodiscard]] StepResult raise_exception(ExceptionCode code, std::uint32_t fault_pc,
                                             bool in_delay_slot, std::uint32_t branch_pc,
                                             std::uint32_t bad_vaddr, bool has_bad_vaddr,
                                             std::string detail);

    Memory& memory_;
    CpuState cpu_{};
    Cop0State cop0_{};
    std::optional<ExceptionMetadata> last_exception_;
};

} // namespace r3000a::reference
