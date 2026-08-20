#ifndef R3000A_REFERENCE_HPP
#define R3000A_REFERENCE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace r3000a::reference {

inline constexpr std::uint32_t kStatusModeMask = 0x0000003Fu;
inline constexpr std::uint32_t kStatusCurrentInterruptEnable = 1u << 0u;
inline constexpr std::uint32_t kStatusCurrentUser = 1u << 1u;
inline constexpr std::uint32_t kStatusInterruptMask = 0x0000FF00u;
inline constexpr std::uint32_t kStatusBev = 1u << 22;
inline constexpr std::uint32_t kStatusCoprocessorUsable0 = 1u << 28u;
inline constexpr std::uint32_t kStatusCoprocessorUsable2 = 1u << 30u;
inline constexpr std::uint32_t kStatusCoprocessorUsableMask = 0xFu << 28u;
inline constexpr std::uint32_t kStatusDeferredEffectMask =
    kStatusModeMask | kStatusInterruptMask | kStatusCoprocessorUsableMask;
inline constexpr std::uint32_t kCauseBd = 1u << 31;
inline constexpr std::uint32_t kCauseCoprocessorShift = 28u;
inline constexpr std::uint32_t kCauseCoprocessorMask = 3u << kCauseCoprocessorShift;
inline constexpr std::uint32_t kCauseInterruptPendingMask = 0x0000FF00u;
inline constexpr std::uint32_t kCauseSoftwareInterruptMask = 0x00000300u;
inline constexpr std::uint32_t kCauseHardwareInterruptMask = 0x0000FC00u;
inline constexpr std::uint32_t kCauseExceptionMask = 0x0000007Cu;

// The address map is stated as architectural fields instead of opaque guest
// addresses. KSEG0 and KSEG1 are selectors 4 and 5 in bits 31..29 and both
// alias the low physical half-gigabyte. The boot ROM starts at physical 1FC
// megabytes; exception offsets are defined by the R3000A architecture.
inline constexpr std::uint32_t kAddressSegmentShift = 29u;
inline constexpr std::uint32_t kAddressSegmentMask = 7u << kAddressSegmentShift;
inline constexpr std::uint32_t kPhysicalAddressMask =
    (std::uint32_t{1} << kAddressSegmentShift) - 1u;
inline constexpr std::uint32_t kKseg0Base = 4u << kAddressSegmentShift;
inline constexpr std::uint32_t kKseg1Base = 5u << kAddressSegmentShift;
inline constexpr std::uint32_t kBootRomPhysicalBase = 0x1FCu << 20u;
inline constexpr std::uint32_t kGeneralExceptionOffset = 0x80u;
inline constexpr std::uint32_t kBootExceptionOffset = 0x180u;
inline constexpr std::uint32_t kResetVector = kKseg1Base | kBootRomPhysicalBase;
inline constexpr std::uint32_t kGeneralExceptionVector =
    kKseg0Base | kGeneralExceptionOffset;
inline constexpr std::uint32_t kBootExceptionVector =
    kKseg1Base | kBootRomPhysicalBase | kBootExceptionOffset;
inline constexpr std::uint32_t kWordSignBit = std::uint32_t{1} << 31u;
// The standalone oracle defaults to the generic IDT R3000A CP0 profile from
// the family architecture manual (Imp=3, Rev=0). A machine integration with a
// different observed PRId supplies it explicitly through ResetState.
inline constexpr std::uint32_t kGenericR3000AImplementationId = 3u;
inline constexpr std::uint32_t kGenericR3000AProcessorId =
    kGenericR3000AImplementationId << 8u;

[[nodiscard]] constexpr std::uint32_t normalize_address(std::uint32_t address) noexcept {
    const std::uint32_t segment = address & kAddressSegmentMask;
    return (segment == kKseg0Base || segment == kKseg1Base)
               ? (address & kPhysicalAddressMask)
               : address;
}

[[nodiscard]] constexpr bool is_kernel_address(std::uint32_t address) noexcept {
    return (address & kWordSignBit) != 0u;
}

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
    std::uint8_t coprocessor = 0;
    bool has_coprocessor = false;
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
    std::uint32_t status = kStatusBev;
    std::uint32_t cause = 0;
    std::uint32_t epc = 0;
    std::uint32_t prid = kGenericR3000AProcessorId;

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

enum class CoprocessorWriteTarget : std::uint8_t {
    None,
    Cop0,
    Cop2Data,
    Cop2Control,
};

struct CoprocessorWriteState {
    CoprocessorWriteTarget target = CoprocessorWriteTarget::None;
    std::uint8_t reg = 0;
    std::uint32_t value = 0;
    std::uint32_t issue_pc = 0;

    [[nodiscard]] bool valid() const noexcept {
        return target != CoprocessorWriteTarget::None;
    }
};

struct CpuState {
    std::array<std::uint32_t, 32> gpr{};
    std::uint32_t hi = 0;
    std::uint32_t lo = 0;
    std::uint32_t pc = kResetVector;
    std::uint32_t next_pc = kResetVector + 4u;

    // These fields are the explicit control-flow pipeline state. After a
    // branch is retired, pc names its delay slot and branch_pending remains
    // true until that slot retires.
    bool branch_pending = false;
    std::uint32_t branch_target = 0;
    std::uint32_t branch_pc = 0;
    bool in_delay_slot = false;

    LoadDelayState load_delay{};
    CoprocessorWriteState coprocessor_write{};
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
    Bc0f,
    Bc0t,
    Mfc2,
    Cfc2,
    Mtc2,
    Ctc2,
    Bc2f,
    Bc2t,
    Cop2Command,
    UnavailableCoprocessor,
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
    Lwc2,
    Swc2,
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
    std::uint8_t coprocessor = 0;
};

struct DecodeResult {
    std::optional<Instruction> instruction;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return instruction.has_value() && error.empty();
    }
};

[[nodiscard]] DecodeResult decode(std::uint32_t raw);

struct CycleMetadata {
    // Cached integer instructions issue at one per cycle. Result latency is
    // separately exposed for load delays and HI/LO or COP2 consumers. Memory
    // and device wait states belong to the machine scheduler, not this oracle.
    std::uint32_t issue_cycles = 1;
    std::uint32_t result_latency_cycles = 1;
    std::uint32_t side_effect_latency_cycles = 1;
    bool external_wait_states = false;
};

[[nodiscard]] constexpr CycleMetadata cycle_metadata(Op op) noexcept {
    switch (op) {
    case Op::Mult:
    case Op::Multu:
        return CycleMetadata{1u, 12u, 12u, false};
    case Op::Div:
    case Op::Divu:
        return CycleMetadata{1u, 35u, 35u, false};
    case Op::Mfc0:
    case Op::Mfc2:
    case Op::Cfc2:
        return CycleMetadata{1u, 2u, 2u, false};
    case Op::Mtc0:
        return CycleMetadata{1u, 2u, 3u, false};
    case Op::Mtc2:
    case Op::Ctc2:
        return CycleMetadata{1u, 2u, 2u, false};
    case Op::Lb:
    case Op::Lh:
    case Op::Lwl:
    case Op::Lw:
    case Op::Lbu:
    case Op::Lhu:
    case Op::Lwr:
        return CycleMetadata{1u, 2u, 2u, true};
    case Op::Sb:
    case Op::Sh:
    case Op::Swl:
    case Op::Sw:
    case Op::Swr:
    case Op::Swc2:
        return CycleMetadata{1u, 1u, 1u, true};
    case Op::Lwc2:
        return CycleMetadata{1u, 2u, 2u, true};
    default:
        return CycleMetadata{};
    }
}

class Coprocessor2 {
public:
    virtual ~Coprocessor2() = default;

    [[nodiscard]] virtual std::uint32_t read_data(std::uint8_t reg) const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t read_control(std::uint8_t reg) const noexcept = 0;
    virtual void write_data(std::uint8_t reg, std::uint32_t value) noexcept = 0;
    virtual void write_control(std::uint8_t reg, std::uint32_t value) noexcept = 0;

    // Return the CPU-visible COP2 condition signal sampled by BC2F/BC2T.
    [[nodiscard]] virtual bool branch_condition() const noexcept = 0;

    // Return the deterministic result latency for this command. The CPU still
    // issues the command in one cached instruction cycle.
    [[nodiscard]] virtual std::uint32_t execute_command(std::uint32_t raw) noexcept = 0;
};

struct ResetState {
    std::uint32_t pc = kResetVector;
    std::uint32_t status = kStatusBev;
    std::uint32_t processor_id = kGenericR3000AProcessorId;
};

struct StepResult {
    bool retired = false;
    std::optional<ExceptionMetadata> exception;
    std::uint32_t pc_before = 0;
    std::uint32_t pc_after = 0;
    bool was_delay_slot = false;
    CycleMetadata cycles{};
};

class Machine final {
public:
    explicit Machine(Memory& memory, ResetState reset_state = {},
                     Coprocessor2* coprocessor2 = nullptr);

    void reset(ResetState reset_state = {});

    [[nodiscard]] CpuState& cpu() noexcept { return cpu_; }
    [[nodiscard]] const CpuState& cpu() const noexcept { return cpu_; }
    [[nodiscard]] Cop0State& cop0() noexcept { return cop0_; }
    [[nodiscard]] const Cop0State& cop0() const noexcept { return cop0_; }
    [[nodiscard]] Memory& memory() noexcept { return memory_; }
    [[nodiscard]] const Memory& memory() const noexcept { return memory_; }
    [[nodiscard]] const std::optional<ExceptionMetadata>& last_exception() const noexcept {
        return last_exception_;
    }

    void attach_coprocessor2(Coprocessor2* coprocessor2) noexcept {
        coprocessor2_ = coprocessor2;
    }
    [[nodiscard]] Coprocessor2* coprocessor2() noexcept { return coprocessor2_; }
    [[nodiscard]] const Coprocessor2* coprocessor2() const noexcept { return coprocessor2_; }

    // Bit zero maps to hardware interrupt input IP2; bit five maps to IP7.
    void set_hardware_interrupts(std::uint8_t pending) noexcept;

    // Settled external BrCond0 input sampled by BC0F/BC0T. Board-level input
    // synchronization belongs to the machine scheduler.
    void set_coprocessor0_branch_condition(bool active) noexcept {
        coprocessor0_branch_condition_ = active;
    }
    [[nodiscard]] bool coprocessor0_branch_condition() const noexcept {
        return coprocessor0_branch_condition_;
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

    struct DeferredControlEffect {
        bool valid = false;
        std::uint32_t previous_bits = 0;
        std::uint8_t instructions_remaining = 0;
    };

    [[nodiscard]] std::uint32_t read_gpr(std::uint8_t reg) const noexcept;
    [[nodiscard]] std::uint32_t read_merge_source(std::uint8_t reg) const noexcept;
    static void set_register_write(RegisterWrite& write, std::uint8_t reg,
                                   std::uint32_t value) noexcept;
    static void set_load_write(LoadDelayState& write, std::uint8_t reg,
                               std::uint32_t value, std::uint32_t issue_pc,
                               bool merge_load) noexcept;
    static void set_coprocessor_write(CoprocessorWriteState& write,
                                      CoprocessorWriteTarget target,
                                      std::uint8_t reg, std::uint32_t value,
                                      std::uint32_t issue_pc) noexcept;
    void commit_register_write(const RegisterWrite& write) noexcept;
    void commit_load(const LoadDelayState& load) noexcept;
    void commit_coprocessor_write(const CoprocessorWriteState& write) noexcept;
    void advance_deferred_control_effects() noexcept;

    [[nodiscard]] std::uint32_t effective_status() const noexcept;
    [[nodiscard]] std::uint32_t effective_cause() const noexcept;

    [[nodiscard]] StepResult raise_exception(ExceptionCode code, std::uint32_t fault_pc,
                                             bool in_delay_slot, std::uint32_t branch_pc,
                                             std::uint32_t bad_vaddr, bool has_bad_vaddr,
                                             std::optional<std::uint8_t> coprocessor,
                                             std::string detail);

    [[nodiscard]] bool interrupt_pending() const noexcept;
    [[nodiscard]] bool coprocessor0_usable() const noexcept;
    [[nodiscard]] bool coprocessor2_usable() const noexcept;

    Memory& memory_;
    Coprocessor2* coprocessor2_ = nullptr;
    CpuState cpu_{};
    Cop0State cop0_{};
    bool coprocessor0_branch_condition_ = false;
    DeferredControlEffect status_effect_delay_{};
    DeferredControlEffect cause_effect_delay_{};
    std::optional<ExceptionMetadata> last_exception_;
};

} // namespace r3000a::reference

#endif // R3000A_REFERENCE_HPP
