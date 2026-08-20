#include "r3000a/reference.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using r3000a::reference::ExceptionCode;
using r3000a::reference::Coprocessor2;
using r3000a::reference::Machine;
using r3000a::reference::Memory;
using r3000a::reference::Op;
using r3000a::reference::ResetState;

constexpr std::uint32_t kBase = 0x00001000u;
constexpr std::uint32_t kPc = kBase;

constexpr ResetState test_reset(std::uint32_t pc = kPc) {
    return ResetState{pc, 0u, r3000a::reference::kGenericR3000AProcessorId};
}

class FakeCoprocessor2 final : public Coprocessor2 {
public:
    [[nodiscard]] std::uint32_t read_data(std::uint8_t reg) const noexcept override {
        return data[reg];
    }

    [[nodiscard]] std::uint32_t read_control(std::uint8_t reg) const noexcept override {
        return control[reg];
    }

    void write_data(std::uint8_t reg, std::uint32_t value) noexcept override {
        data[reg] = value;
    }

    void write_control(std::uint8_t reg, std::uint32_t value) noexcept override {
        control[reg] = value;
    }

    [[nodiscard]] bool branch_condition() const noexcept override {
        return condition;
    }

    [[nodiscard]] std::uint32_t execute_command(std::uint32_t raw) noexcept override {
        last_command = raw;
        ++command_count;
        return command_latency;
    }

    std::array<std::uint32_t, 32> data{};
    std::array<std::uint32_t, 32> control{};
    std::uint32_t last_command = 0;
    std::uint32_t command_count = 0;
    std::uint32_t command_latency = 7;
    bool condition = false;
};

class Failure final : public std::runtime_error {
public:
    explicit Failure(const std::string& message) : std::runtime_error(message) {}
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw Failure(message);
    }
}

void expect_eq(std::uint32_t actual, std::uint32_t expected, const std::string& label) {
    if (actual != expected) {
        std::ostringstream message;
        message << label << ": expected 0x" << std::hex << expected << ", got 0x" << actual;
        throw Failure(message.str());
    }
}

void expect_eq(std::uint8_t actual, std::uint8_t expected, const std::string& label) {
    if (actual != expected) {
        std::ostringstream message;
        message << label << ": expected " << static_cast<unsigned>(expected)
                << ", got " << static_cast<unsigned>(actual);
        throw Failure(message.str());
    }
}

std::uint32_t r_type(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                    std::uint8_t shamt, std::uint8_t funct) {
    return (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) |
           (static_cast<std::uint32_t>(shamt) << 6u) | funct;
}

std::uint32_t i_type(std::uint8_t opcode, std::uint8_t rs, std::uint8_t rt,
                    std::uint16_t immediate) {
    return (static_cast<std::uint32_t>(opcode) << 26u) |
           (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) | immediate;
}

std::uint32_t j_type(std::uint8_t opcode, std::uint32_t target) {
    return (static_cast<std::uint32_t>(opcode) << 26u) | ((target >> 2u) & 0x03FFFFFFu);
}

std::uint32_t cop0_type(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                       std::uint8_t funct = 0) {
    return (0x10u << 26u) | (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u) | funct;
}

std::uint32_t cop2_type(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd) {
    return (0x12u << 26u) | (static_cast<std::uint32_t>(rs) << 21u) |
           (static_cast<std::uint32_t>(rt) << 16u) |
           (static_cast<std::uint32_t>(rd) << 11u);
}

std::uint32_t cop2_command(std::uint32_t payload) {
    return (0x12u << 26u) | (std::uint32_t{1} << 25u) |
           (payload & ((std::uint32_t{1} << 25u) - 1u));
}

std::uint32_t nop() {
    return 0u;
}

void run_nop(Machine& machine) {
    const auto result = machine.step_word(nop());
    expect(result.retired, "NOP must retire");
}

void step_ok(Machine& machine, std::uint32_t raw) {
    const auto result = machine.step_word(raw);
    expect(result.retired, "instruction must retire");
}

void store_ok(bool stored, const std::string& label) {
    expect(stored, label + " must fit in test memory");
}

std::uint32_t word_of(const std::array<std::uint8_t, 4>& bytes) {
    std::uint32_t value = 0;
    for (unsigned index = 0; index < bytes.size(); ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8u);
    }
    return value;
}

std::array<std::uint8_t, 4> model_lwl(const std::array<std::uint8_t, 4>& memory,
                                      std::array<std::uint8_t, 4> reg,
                                      unsigned offset) {
    for (unsigned memory_lane = 0; memory_lane <= offset; ++memory_lane) {
        reg[3u - offset + memory_lane] = memory[memory_lane];
    }
    return reg;
}

std::array<std::uint8_t, 4> model_lwr(const std::array<std::uint8_t, 4>& memory,
                                      std::array<std::uint8_t, 4> reg,
                                      unsigned offset) {
    for (unsigned memory_lane = offset; memory_lane < memory.size(); ++memory_lane) {
        reg[memory_lane - offset] = memory[memory_lane];
    }
    return reg;
}

std::array<std::uint8_t, 4> model_swl(std::array<std::uint8_t, 4> memory,
                                      const std::array<std::uint8_t, 4>& reg,
                                      unsigned offset) {
    for (unsigned memory_lane = 0; memory_lane <= offset; ++memory_lane) {
        memory[memory_lane] = reg[3u - offset + memory_lane];
    }
    return memory;
}

std::array<std::uint8_t, 4> model_swr(std::array<std::uint8_t, 4> memory,
                                      const std::array<std::uint8_t, 4>& reg,
                                      unsigned offset) {
    for (unsigned memory_lane = offset; memory_lane < memory.size(); ++memory_lane) {
        memory[memory_lane] = reg[memory_lane - offset];
    }
    return memory;
}

void test_reset_and_segment_mapping() {
    Memory memory(0u, 0x4000u);
    Machine machine(memory);

    expect_eq(machine.cpu().pc, r3000a::reference::kResetVector,
              "architectural reset PC");
    expect_eq(machine.cpu().next_pc, r3000a::reference::kResetVector + 4u,
              "architectural reset next PC");
    expect_eq(machine.cop0().status, r3000a::reference::kStatusBev,
              "architectural reset BEV");
    expect_eq(r3000a::reference::kGenericR3000AImplementationId, 3u,
              "generic IDT R3000A implementation profile");
    expect_eq(machine.cop0().prid >> 8u, 3u,
              "generic reference PRId implementation field");
    expect_eq(machine.cop0().prid & 0xFFu, 0u,
              "generic reference PRId revision field");

    constexpr std::uint32_t selected_machine_prid = 2u;
    machine.reset(ResetState{r3000a::reference::kResetVector,
                             r3000a::reference::kStatusBev,
                             selected_machine_prid});
    expect_eq(machine.cop0().prid, selected_machine_prid,
              "machine integration can select its observed PRId");

    constexpr std::uint32_t physical = 0x120u;
    expect_eq(r3000a::reference::normalize_address(
                  r3000a::reference::kKseg0Base + physical),
              physical, "KSEG0 physical alias");
    expect_eq(r3000a::reference::normalize_address(
                  r3000a::reference::kKseg1Base + physical),
              physical, "KSEG1 physical alias");
    store_ok(memory.store32le(physical, 0x10203040u), "physical alias seed");
    expect_eq(memory.load32le(r3000a::reference::kKseg0Base + physical).value(),
              0x10203040u, "KSEG0 memory alias");
    expect_eq(memory.load32le(r3000a::reference::kKseg1Base + physical).value(),
              0x10203040u, "KSEG1 memory alias");

    Memory aliased_base(r3000a::reference::kKseg1Base + kBase, 0x40u);
    expect_eq(aliased_base.base(), kBase, "Memory stores a physical base");

    Machine user(memory, test_reset());
    user.cop0().status = r3000a::reference::kStatusCurrentUser;
    user.cpu().gpr[1] = r3000a::reference::kKseg0Base + physical;
    const auto forbidden_load = user.step_word(i_type(0x23u, 1, 2, 0u));
    expect(!forbidden_load.retired, "user KSEG0 load must fault");
    expect(forbidden_load.exception->code == ExceptionCode::AddressErrorLoad,
           "user KSEG0 load exception code");
    expect_eq(user.cop0().bad_vaddr, r3000a::reference::kKseg0Base + physical,
              "user KSEG0 load BadVAddr");

    user.reset(test_reset());
    user.cop0().status = r3000a::reference::kStatusCurrentUser;
    user.cpu().gpr[1] = r3000a::reference::kKseg1Base + physical;
    user.cpu().gpr[2] = 0x55667788u;
    const auto forbidden_store = user.step_word(i_type(0x2Bu, 1, 2, 0u));
    expect(!forbidden_store.retired, "user KSEG1 store must fault");
    expect(forbidden_store.exception->code == ExceptionCode::AddressErrorStore,
           "user KSEG1 store exception code");
    expect_eq(memory.load32le(physical).value(), 0x10203040u,
              "faulting user store is transactional");

    user.reset(ResetState{r3000a::reference::kKseg0Base + physical,
                          r3000a::reference::kStatusCurrentUser,
                          r3000a::reference::kGenericR3000AProcessorId});
    const auto forbidden_fetch = user.step_word(nop());
    expect(!forbidden_fetch.retired, "user KSEG0 fetch must fault");
    expect(forbidden_fetch.exception->code == ExceptionCode::AddressErrorLoad,
           "user KSEG0 fetch exception code");
    expect_eq(user.cop0().bad_vaddr, r3000a::reference::kKseg0Base + physical,
              "user KSEG0 fetch BadVAddr");
    expect_eq(forbidden_fetch.cycles.issue_cycles, 0u,
              "forbidden instruction fetch does not issue");
    expect_eq(forbidden_fetch.cycles.result_latency_cycles, 0u,
              "forbidden instruction fetch has no result latency");
    expect(!forbidden_fetch.cycles.external_wait_states,
           "forbidden instruction fetch has no data wait-state classification");
}

void test_decode_and_integer_semantics() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, test_reset());

    const auto valid_decode = r3000a::reference::decode(r_type(1, 2, 3, 0, 0x21));
    expect(static_cast<bool>(valid_decode), "valid ADDU decode");
    const auto invalid_decode = r3000a::reference::decode(i_type(0x06u, 1, 2, 0u));
    expect(!static_cast<bool>(invalid_decode), "malformed BLEZ decode must be false");
    expect(!invalid_decode.instruction.has_value(), "invalid decode has no instruction");
    expect(!invalid_decode.error.empty(), "invalid decode retains a diagnostic");

    machine.cpu().gpr[1] = r3000a::reference::kWordSignBit;
    machine.cpu().gpr[2] = 0x00000003u;
    step_ok(machine, r_type(1, 2, 3, 0, 0x21));
    expect_eq(machine.cpu().gpr[3], r3000a::reference::kWordSignBit + 3u, "ADDU");

    step_ok(machine, i_type(0x0Cu, 3, 4, 0x00FFu));
    expect_eq(machine.cpu().gpr[4], 0x00000003u, "ANDI zero extension");
    step_ok(machine, i_type(0x0Fu, 0, 5, 0xA55Au));
    expect_eq(machine.cpu().gpr[5], 0xA55A0000u, "LUI");
    step_ok(machine, r_type(5, 2, 6, 0, 0x2Au));
    expect_eq(machine.cpu().gpr[6], 1u, "SLT signed");
    step_ok(machine, r_type(1, 2, 7, 0, 0x2Bu));
    expect_eq(machine.cpu().gpr[7], 0u, "SLTU unsigned");

    machine.cpu().pc = kPc + 0x40u;
    machine.cpu().gpr[8] = 0x7FFFFFFFu;
    machine.cpu().gpr[9] = 1u;
    const auto overflow = machine.step_word(r_type(8, 9, 10, 0, 0x20));
    expect(!overflow.retired, "ADD overflow must not retire");
    expect(overflow.exception.has_value(), "ADD overflow must report an exception");
    expect(overflow.exception->code == ExceptionCode::Overflow, "ADD overflow code");
    expect_eq(machine.cpu().gpr[10], 0u, "ADD overflow must not write rd");
    expect_eq(machine.cop0().epc, kPc + 0x40u, "ADD overflow EPC");
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseExceptionMask,
              static_cast<std::uint32_t>(ExceptionCode::Overflow) << 2u,
              "ADD overflow Cause");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 31u;
    machine.cpu().gpr[2] = 0xFFFFFFFFu;
    step_ok(machine, r_type(0, 2, 3, 4, 0x00));
    expect_eq(machine.cpu().gpr[3], 0xFFFFFFF0u, "SLL");
    machine.cpu().gpr[2] = r3000a::reference::kWordSignBit;
    step_ok(machine, r_type(0, 2, 5, 4, 0x02));
    expect_eq(machine.cpu().gpr[5], 0x08000000u, "fixed SRL zero fill");
    step_ok(machine, r_type(0, 2, 6, 4, 0x03));
    expect_eq(machine.cpu().gpr[6], 0xF8000000u, "fixed SRA sign fill");
    machine.cpu().gpr[1] = 36u;
    step_ok(machine, r_type(1, 2, 7, 0, 0x06));
    expect_eq(machine.cpu().gpr[7], 0x08000000u, "SRLV masks shift count");
    machine.cpu().gpr[2] = 0xFFFFFFFFu;
    step_ok(machine, r_type(1, 3, 4, 0, 0x07));
    expect_eq(machine.cpu().gpr[4], 0xFFFFFFFFu, "SRAV");
    expect_eq(machine.cpu().gpr[0], 0u, "r0 invariant");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 0xFFFFFFFEu;
    machine.cpu().gpr[2] = 3u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x18));
    expect_eq(machine.cpu().hi, 0xFFFFFFFFu, "MULT HI");
    expect_eq(machine.cpu().lo, 0xFFFFFFFAu, "MULT LO");
    step_ok(machine, r_type(0, 0, 3, 0, 0x12));
    expect_eq(machine.cpu().gpr[3], 0xFFFFFFFAu, "MFLO");
    step_ok(machine, r_type(0, 0, 4, 0, 0x10));
    expect_eq(machine.cpu().gpr[4], 0xFFFFFFFFu, "MFHI");
    machine.cpu().gpr[5] = 0x13579BDFu;
    step_ok(machine, r_type(5, 0, 0, 0, 0x11));
    step_ok(machine, r_type(0, 0, 6, 0, 0x10));
    expect_eq(machine.cpu().gpr[6], 0x13579BDFu, "MTHI/MFHI round-trip");
    machine.cpu().gpr[5] = 0x2468ACE0u;
    step_ok(machine, r_type(5, 0, 0, 0, 0x13));
    step_ok(machine, r_type(0, 0, 6, 0, 0x12));
    expect_eq(machine.cpu().gpr[6], 0x2468ACE0u, "MTLO/MFLO round-trip");
    machine.cpu().gpr[1] = 0xFFFFFFF9u;
    machine.cpu().gpr[2] = 3u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Au));
    expect_eq(machine.cpu().lo, 0xFFFFFFFEu, "DIV quotient");
    expect_eq(machine.cpu().hi, 0xFFFFFFFFu, "DIV remainder");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = std::numeric_limits<std::uint32_t>::max();
    machine.cpu().gpr[2] = std::numeric_limits<std::uint32_t>::max();
    step_ok(machine, r_type(1, 2, 0, 0, 0x19));
    expect_eq(machine.cpu().hi, std::numeric_limits<std::uint32_t>::max() - 1u,
              "MULTU maximum HI");
    expect_eq(machine.cpu().lo, 1u, "MULTU maximum LO");

    machine.cpu().gpr[1] = r3000a::reference::kWordSignBit;
    machine.cpu().gpr[2] = std::numeric_limits<std::uint32_t>::max();
    step_ok(machine, r_type(1, 2, 0, 0, 0x18));
    expect_eq(machine.cpu().hi, 0u, "MULT minimum times minus one HI");
    expect_eq(machine.cpu().lo, r3000a::reference::kWordSignBit,
              "MULT minimum times minus one LO");

    machine.cpu().gpr[1] = r3000a::reference::kWordSignBit;
    machine.cpu().gpr[2] = std::numeric_limits<std::uint32_t>::max();
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Au));
    expect_eq(machine.cpu().lo, r3000a::reference::kWordSignBit,
              "DIV minimum by minus one quotient");
    expect_eq(machine.cpu().hi, 0u, "DIV minimum by minus one remainder");

    machine.cpu().gpr[1] = 7u;
    machine.cpu().gpr[2] = 0u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Au));
    expect_eq(machine.cpu().lo, std::numeric_limits<std::uint32_t>::max(),
              "DIV positive by zero quotient");
    expect_eq(machine.cpu().hi, 7u, "DIV positive by zero remainder");
    machine.cpu().gpr[1] = std::numeric_limits<std::uint32_t>::max() - 6u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Au));
    expect_eq(machine.cpu().lo, 1u, "DIV negative by zero quotient");
    expect_eq(machine.cpu().hi, std::numeric_limits<std::uint32_t>::max() - 6u,
              "DIV negative by zero remainder");

    machine.cpu().gpr[1] = 0x12345678u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Bu));
    expect_eq(machine.cpu().lo, std::numeric_limits<std::uint32_t>::max(),
              "DIVU by zero quotient");
    expect_eq(machine.cpu().hi, 0x12345678u, "DIVU by zero remainder");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = r3000a::reference::kWordSignBit;
    machine.cpu().gpr[2] = 1u;
    const auto sub_overflow = machine.step_word(r_type(1, 2, 3, 0, 0x22));
    expect(!sub_overflow.retired, "SUB minimum minus one must overflow");
    expect(sub_overflow.exception->code == ExceptionCode::Overflow,
           "SUB overflow exception code");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = std::numeric_limits<std::int32_t>::max();
    machine.cpu().gpr[2] = 0x22222222u;
    const auto addi_overflow = machine.step_word(i_type(0x08u, 1, 2, 1u));
    expect(!addi_overflow.retired &&
               addi_overflow.exception->code == ExceptionCode::Overflow,
           "ADDI maximum plus one must overflow");
    expect_eq(machine.cpu().gpr[2], 0x22222222u,
              "ADDI overflow must not write rt");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    machine.cpu().gpr[2] = 40u;
    step_ok(machine, r_type(2, 1, 3, 0, 0x04));
    expect_eq(machine.cpu().gpr[3], 1u << 8u, "SLLV masks shift count");
    machine.cpu().gpr[1] = r3000a::reference::kWordSignBit;
    machine.cpu().gpr[2] = 31u;
    step_ok(machine, r_type(2, 1, 4, 0, 0x07));
    expect_eq(machine.cpu().gpr[4], std::numeric_limits<std::uint32_t>::max(),
              "SRAV sign fill at 31");

    machine.cpu().gpr[1] = std::numeric_limits<std::uint32_t>::max() - 1u;
    step_ok(machine, i_type(0x0Bu, 1, 5, std::numeric_limits<std::uint16_t>::max()));
    expect_eq(machine.cpu().gpr[5], 1u, "SLTIU sign-extends its immediate");
}

void test_control_flow_and_delay_slots() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, test_reset());

    const std::uint32_t jal_target = 0x00123400u;
    step_ok(machine, j_type(0x03u, jal_target));
    expect_eq(machine.cpu().pc, kPc + 4u, "JAL enters delay slot");
    expect(machine.cpu().branch_pending, "JAL records a pending redirect");
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "JAL link");
    machine.cpu().gpr[2] = 0u;
    step_ok(machine, i_type(0x09u, 0, 2, 7u));
    expect_eq(machine.cpu().gpr[2], 7u, "JAL delay slot executes");
    expect_eq(machine.cpu().pc, jal_target, "JAL target");
    expect(!machine.cpu().branch_pending, "JAL delay slot drains redirect");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 9u;
    machine.cpu().gpr[2] = 9u;
    step_ok(machine, i_type(0x05u, 1, 2, 3u));
    expect_eq(machine.cpu().pc, kPc + 4u, "not-taken branch enters delay slot");
    step_ok(machine, i_type(0x09u, 0, 3, 0x55u));
    expect_eq(machine.cpu().gpr[3], 0x55u, "not-taken branch delay slot");
    expect_eq(machine.cpu().pc, kPc + 8u, "not-taken branch fall-through");

    machine.reset(test_reset(kPc + 0x80u));
    machine.cpu().gpr[4] = kPc + 0x180u;
    step_ok(machine, r_type(4, 0, 0, 0, 0x08));
    step_ok(machine, i_type(0x09u, 0, 5, 0x33u));
    expect_eq(machine.cpu().gpr[5], 0x33u, "JR delay slot");
    expect_eq(machine.cpu().pc, kPc + 0x180u, "JR target");

    machine.reset(test_reset(kPc + 0xA0u));
    machine.cpu().gpr[4] = kPc + 0x1C0u;
    step_ok(machine, r_type(4, 0, 6, 0, 0x09));
    expect_eq(machine.cpu().gpr[6], kPc + 0xA8u, "JALR link rd");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 0x1C0u, "JALR target");

    machine.reset(test_reset(kPc + 0x200u));
    machine.cpu().gpr[4] = kPc + 0x201u;
    machine.cpu().gpr[6] = 0x77777777u;
    const auto bad_jalr = machine.step_word(r_type(4, 0, 6, 0, 0x09));
    expect(!bad_jalr.retired, "misaligned JALR must fault immediately");
    expect(bad_jalr.exception->code == ExceptionCode::AddressErrorLoad,
           "misaligned JALR exception code");
    expect_eq(machine.cpu().gpr[6], 0x77777777u, "misaligned JALR must not link");
    expect_eq(machine.cpu().pc, r3000a::reference::kGeneralExceptionVector,
              "misaligned JALR exception vector");

    machine.reset(test_reset(kPc + 0x220u));
    machine.cop0().status = r3000a::reference::kStatusCurrentUser;
    machine.cpu().gpr[4] = r3000a::reference::kKseg0Base + 0x100u;
    machine.cpu().gpr[6] = 0x66666666u;
    const auto inaccessible_jalr = machine.step_word(r_type(4, 0, 6, 0, 0x09));
    expect(!inaccessible_jalr.retired,
           "user JALR to an inaccessible segment must fault immediately");
    expect(inaccessible_jalr.exception->code == ExceptionCode::AddressErrorLoad,
           "inaccessible JALR exception code");
    expect_eq(machine.cop0().epc, kPc + 0x220u, "inaccessible JALR EPC");
    expect_eq(machine.cop0().bad_vaddr,
              r3000a::reference::kKseg0Base + 0x100u,
              "inaccessible JALR BadVAddr");
    expect_eq(machine.cpu().gpr[6], 0x66666666u,
              "inaccessible JALR must not link");
    expect(!machine.cpu().branch_pending,
           "inaccessible JALR must not enter its delay slot");

    machine.reset(test_reset(kPc + 0x240u));
    machine.cpu().gpr[4] = kPc + 0x241u;
    step_ok(machine, r_type(4, 0, 0, 0, 0x08));
    step_ok(machine, nop());
    const auto bad_jr_fetch = machine.step_word(nop());
    expect(!bad_jr_fetch.retired, "misaligned JR target must fault on fetch");
    expect(bad_jr_fetch.exception->code == ExceptionCode::AddressErrorLoad,
           "misaligned JR fetch exception code");
    expect_eq(machine.cop0().bad_vaddr, kPc + 0x241u, "misaligned JR BadVAddr");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = std::numeric_limits<std::uint32_t>::max();
    machine.cpu().gpr[31] = 0x11111111u;
    step_ok(machine, i_type(0x01u, 1, 0x10u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BLTZAL taken link");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BLTZAL target");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    machine.cpu().gpr[31] = 0x22222222u;
    step_ok(machine, i_type(0x01u, 1, 0x10u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BLTZAL not-taken still links");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 8u, "BLTZAL not-taken fall-through");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 0u;
    step_ok(machine, i_type(0x01u, 1, 0x11u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BGEZAL taken link");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BGEZAL target");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 0u;
    step_ok(machine, i_type(0x06u, 1, 0, 2u));
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BLEZ target");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    step_ok(machine, i_type(0x07u, 1, 0, 2u));
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BGTZ target");

    const std::uint32_t jump_boundary = (std::uint32_t{1} << 28u) - 4u;
    machine.reset(test_reset(jump_boundary));
    step_ok(machine, j_type(0x02u, 0x100u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, (std::uint32_t{1} << 28u) | 0x100u,
              "J takes its high nibble from the delay-slot address");

    machine.reset(test_reset(kPc + 0x20u));
    machine.cpu().gpr[1] = 4u;
    step_ok(machine, i_type(0x04u, 1, 1,
                            std::numeric_limits<std::uint16_t>::max() - 1u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, kPc + 0x1Cu, "negative branch displacement");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    step_ok(machine, i_type(0x04u, 1, 1, 2u));
    const auto nested_branch = machine.step_word(j_type(0x02u, 0x100u));
    expect(!nested_branch.retired, "control transfer in delay slot fails deterministically");
    expect(nested_branch.exception->code == ExceptionCode::ReservedInstruction,
           "delay-slot control-transfer policy code");
    expect(nested_branch.exception->in_delay_slot, "delay-slot control-transfer metadata");
    expect_eq(machine.cop0().epc, kPc, "delay-slot control-transfer EPC");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    machine.cpu().gpr[2] = 1u;
    step_ok(machine, i_type(0x04u, 1, 2, 2u));
    const auto delay_exception = machine.step_word(i_type(0x23u, 3, 4, 2u));
    expect(!delay_exception.retired, "faulting branch delay slot must not retire");
    expect(delay_exception.exception.has_value(), "delay-slot exception metadata");
    expect(delay_exception.exception->in_delay_slot, "delay-slot flag");
    expect_eq(machine.cop0().epc, kPc, "delay-slot EPC is branch PC");
    expect((machine.cop0().cause & r3000a::reference::kCauseBd) != 0u,
           "delay-slot Cause.BD");
}

void test_delayed_loads_and_cop0() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, test_reset());
    store_ok(memory.store32le(kBase + 0x40u, 0xCAFEBABEu), "initial LW word");
    machine.cpu().gpr[1] = kBase + 0x40u;
    machine.cpu().gpr[2] = 0x11111111u;
    step_ok(machine, i_type(0x23u, 1, 2, 0u));
    expect_eq(machine.cpu().gpr[2], 0x11111111u, "LW delay before consumer");
    step_ok(machine, i_type(0x09u, 2, 3, 1u));
    expect_eq(machine.cpu().gpr[3], 0x11111112u, "consumer sees old load value");
    expect_eq(machine.cpu().gpr[2], 0xCAFEBABEu, "LW commits after delay slot");
    step_ok(machine, i_type(0x09u, 2, 4, 1u));
    expect_eq(machine.cpu().gpr[4], 0xCAFEBABFu, "later instruction sees LW");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x40u;
    machine.cpu().gpr[2] = 0x11111111u;
    step_ok(machine, i_type(0x23u, 1, 2, 0u));
    step_ok(machine, i_type(0x09u, 0, 2, 9u));
    expect_eq(machine.cpu().gpr[2], 9u,
              "younger direct write cancels the older load destination");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x40u;
    step_ok(machine, i_type(0x23u, 1, 2, 0u));
    const auto fault_after_load = machine.step_word(0xFFFFFFFFu);
    expect(!fault_after_load.retired, "fault after LW must raise");
    expect_eq(machine.cpu().gpr[2], 0xCAFEBABEu,
              "older delayed load commits before a younger exception");

    store_ok(memory.store32le(kBase + 0x44u, 0x0BADF00Du), "second LW word");
    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x40u;
    step_ok(machine, i_type(0x23u, 1, 2, 0u));
    step_ok(machine, i_type(0x23u, 1, 2, 4u));
    expect_eq(machine.cpu().gpr[2], 0xCAFEBABEu,
              "first of two loads commits while second remains pending");
    run_nop(machine);
    expect_eq(machine.cpu().gpr[2], 0x0BADF00Du,
              "second of two loads commits one instruction later");

    store_ok(memory.store8(kBase + 0x60u, 0x80u), "signed byte seed");
    store_ok(memory.store16le(kBase + 0x62u, 0x8001u), "signed halfword seed");
    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x60u;
    step_ok(machine, i_type(0x20u, 1, 2, 0u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[2], 0xFFFFFF80u, "LB sign extension");
    step_ok(machine, i_type(0x24u, 1, 3, 0u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[3], 0x80u, "LBU zero extension");
    step_ok(machine, i_type(0x21u, 1, 4, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[4], 0xFFFF8001u, "LH sign extension");
    step_ok(machine, i_type(0x25u, 1, 5, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[5], 0x8001u, "LHU zero extension");

    const auto bc0f = r3000a::reference::decode(i_type(0x10u, 0x08u, 0u, 2u));
    const auto bc0t = r3000a::reference::decode(i_type(0x10u, 0x08u, 1u, 2u));
    expect(bc0f && bc0f.instruction->op == Op::Bc0f, "BC0F typed decode");
    expect(bc0t && bc0t.instruction->op == Op::Bc0t, "BC0T typed decode");

    machine.reset(test_reset());
    machine.set_coprocessor0_branch_condition(true);
    step_ok(machine, i_type(0x10u, 0x08u, 1u, 2u));
    step_ok(machine, i_type(0x09u, 0u, 6u, 0x77u));
    expect_eq(machine.cpu().gpr[6], 0x77u, "BC0T delay slot executes");
    expect_eq(machine.cpu().pc, kPc + 12u, "BC0T samples an active BrCond0 input");

    machine.reset(test_reset());
    machine.set_coprocessor0_branch_condition(false);
    step_ok(machine, i_type(0x10u, 0x08u, 0u, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, kPc + 12u, "BC0F samples an inactive BrCond0 input");

    machine.reset(test_reset());
    machine.set_coprocessor0_branch_condition(true);
    step_ok(machine, i_type(0x10u, 0x08u, 0u, 2u));
    step_ok(machine, i_type(0x09u, 0u, 7u, 0x55u));
    expect_eq(machine.cpu().gpr[7], 0x55u, "not-taken BC0F delay slot executes");
    expect_eq(machine.cpu().pc, kPc + 8u, "BC0F falls through on an active BrCond0 input");

    machine.reset(test_reset());
    machine.set_coprocessor0_branch_condition(false);
    step_ok(machine, i_type(0x10u, 0x08u, 1u, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, kPc + 8u, "BC0T falls through on an inactive BrCond0 input");

    machine.reset(test_reset());
    machine.cop0().status = 0x12340015u;
    step_ok(machine, cop0_type(0x00u, 2, 12u));
    expect_eq(machine.cpu().gpr[2], 0u, "MFC0 has a transfer delay");
    step_ok(machine, i_type(0x09u, 0, 3, 1u));
    expect_eq(machine.cpu().gpr[3], 1u, "MFC0 consumer sees old value");
    expect_eq(machine.cpu().gpr[2], 0x12340015u, "MFC0 commits after delay slot");

    machine.reset(test_reset());
    machine.cop0().status = 0x0000002Du;
    step_ok(machine, cop0_type(0x10u, 0, 0, 0x10u));
    const std::uint32_t mode = 0x2Du;
    const std::uint32_t expected = (mode & ~0x0Fu) |
                                   ((mode & 0x3Cu) >> 2u);
    expect_eq(machine.cop0().status, expected, "RFE pops mode stack");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusBev;
    const auto syscall = machine.step_word(r_type(0, 0, 0, 0, 0x0Cu));
    expect(!syscall.retired, "SYSCALL must raise");
    expect_eq(machine.cpu().pc, r3000a::reference::kBootExceptionVector,
              "BEV exception vector");
    expect(syscall.exception->code == ExceptionCode::Syscall, "SYSCALL code");

    machine.reset(test_reset());
    machine.cop0().status = 0xABCD0035u;
    const auto status_exception = machine.step_word(r_type(0, 0, 0, 0, 0x0Cu));
    expect(!status_exception.retired, "second SYSCALL must raise");
    const std::uint32_t status_before = 0xABCD0035u;
    const std::uint32_t pushed_mode = ((status_before & 0x3Fu) << 2u) & 0x3Cu;
    const std::uint32_t expected_status = (status_before & ~0x3Fu) | pushed_mode;
    expect_eq(machine.cop0().status, expected_status, "exception status stack push");
}

void test_exception_interrupt_and_status_semantics() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, test_reset());

    constexpr std::uint32_t preserved_status = 0xA5000000u;
    constexpr std::uint32_t preserved_pending = 0x00008400u;
    for (std::uint32_t mode = 0; mode < 64u; ++mode) {
        machine.reset(test_reset());
        const std::uint32_t before = preserved_status | mode;
        machine.cop0().status = before;
        machine.cop0().cause = preserved_pending | r3000a::reference::kCauseCoprocessorMask;
        const auto raised = machine.step_word(r_type(0, 0, 0, 0, 0x0Cu));
        expect(!raised.retired, "SYSCALL must raise for every status-stack state");
        const std::uint32_t pushed = (mode << 2u) & r3000a::reference::kStatusModeMask;
        const std::uint32_t expected_status =
            (before & ~r3000a::reference::kStatusModeMask) | pushed;
        expect_eq(machine.cop0().status, expected_status,
                  "exception pushes all six status mode bits");
        expect_eq(machine.cop0().cause & r3000a::reference::kCauseCoprocessorMask,
                  0u, "non-coprocessor exception clears Cause.CE");
        expect_eq(machine.cop0().cause & r3000a::reference::kCauseInterruptPendingMask,
                  preserved_pending, "exception preserves Cause.IP");

        step_ok(machine, cop0_type(0x10u, 0, 0, 0x10u));
        const std::uint32_t popped =
            (expected_status & ~0x0Fu) |
            ((expected_status & r3000a::reference::kStatusModeMask) >> 2u);
        expect_eq(machine.cop0().status, popped, "RFE pops the current/previous stack");
    }

    machine.reset(test_reset());
    const auto breakpoint = machine.step_word(r_type(0, 0, 0, 0, 0x0Du));
    expect(!breakpoint.retired, "BREAK must raise");
    expect(breakpoint.exception->code == ExceptionCode::Breakpoint, "BREAK code");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCurrentUser;
    const auto user_cop0 = machine.step_word(cop0_type(0x00u, 2, 15u));
    expect(!user_cop0.retired, "disabled user COP0 access must raise");
    expect(user_cop0.exception->code == ExceptionCode::CoprocessorUnusable,
           "disabled user COP0 exception code");
    expect(user_cop0.exception->has_coprocessor && user_cop0.exception->coprocessor == 0u,
           "disabled user COP0 metadata");
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseCoprocessorMask,
              0u, "COP0 Cause.CE");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCurrentUser;
    machine.set_coprocessor0_branch_condition(true);
    const auto user_bc0 = machine.step_word(i_type(0x10u, 0x08u, 1u, 2u));
    expect(!user_bc0.retired &&
               user_bc0.exception->code == ExceptionCode::CoprocessorUnusable,
           "user BC0T with CU0 clear raises coprocessor unusable");
    expect(user_bc0.exception->has_coprocessor && user_bc0.exception->coprocessor == 0u,
           "disabled user BC0T records coprocessor zero");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCurrentUser |
                            r3000a::reference::kStatusCoprocessorUsable0;
    step_ok(machine, cop0_type(0x00u, 2, 15u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[2], r3000a::reference::kGenericR3000AProcessorId,
              "CU0 permits user COP0 transfer");

    machine.reset(test_reset());
    machine.cop0().cause = r3000a::reference::kCauseSoftwareInterruptMask;
    machine.set_hardware_interrupts(1u);
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseSoftwareInterruptMask,
              r3000a::reference::kCauseSoftwareInterruptMask,
              "hardware interrupt update preserves software pending bits");
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseHardwareInterruptMask,
              1u << 10u, "hardware interrupt input maps to IP2");
    machine.set_hardware_interrupts(0u);
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseSoftwareInterruptMask,
              r3000a::reference::kCauseSoftwareInterruptMask,
              "clearing hardware interrupts preserves software pending bits");

    machine.reset(test_reset());
    machine.cpu().gpr[2] = r3000a::reference::kStatusBev;
    step_ok(machine, cop0_type(0x04u, 2, 12u));
    expect_eq(machine.cop0().status, 0u, "MTC0 status write is not immediately visible");
    step_ok(machine, cop0_type(0x00u, 3, 12u));
    expect_eq(machine.cop0().status, r3000a::reference::kStatusBev,
              "MTC0 status write commits after one intervening instruction");
    run_nop(machine);
    expect_eq(machine.cpu().gpr[3], 0u,
              "MFC0 in the MTC0 transfer slot observes the old status");
    step_ok(machine, cop0_type(0x00u, 4, 12u));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[4], r3000a::reference::kStatusBev,
              "later MFC0 observes the committed status");

    machine.reset(test_reset());
    machine.set_hardware_interrupts(1u);
    machine.cpu().gpr[2] = r3000a::reference::kStatusCurrentInterruptEnable |
                           (1u << 10u);
    step_ok(machine, cop0_type(0x04u, 2, 12u));
    step_ok(machine, nop());
    step_ok(machine, nop());
    const auto status_effect_interrupt = machine.step_word(nop());
    expect(!status_effect_interrupt.retired,
           "MTC0 status interrupt effects begin after two further instructions");
    expect(status_effect_interrupt.exception->code == ExceptionCode::Interrupt,
           "delayed MTC0 status interrupt code");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCurrentInterruptEnable |
                            (1u << 8u);
    machine.cpu().gpr[2] = 1u << 8u;
    step_ok(machine, cop0_type(0x04u, 2, 13u));
    step_ok(machine, nop());
    step_ok(machine, nop());
    const auto cause_effect_interrupt = machine.step_word(nop());
    expect(!cause_effect_interrupt.retired,
           "MTC0 Cause software interrupt effects begin after two further instructions");
    expect(cause_effect_interrupt.exception->code == ExceptionCode::Interrupt,
           "delayed MTC0 Cause interrupt code");

    machine.reset(test_reset());
    machine.set_hardware_interrupts(1u);
    machine.cop0().status = 1u << 10u;
    step_ok(machine, nop());
    machine.cop0().status = r3000a::reference::kStatusCurrentInterruptEnable |
                            (1u << 11u);
    step_ok(machine, nop());

    machine.reset(test_reset());
    machine.cpu().gpr[2] = 0x11111111u;
    machine.set_hardware_interrupts(1u);
    machine.cop0().status = r3000a::reference::kStatusCurrentInterruptEnable |
                            (1u << 10u);
    const auto interrupt = machine.step_word(i_type(0x09u, 0, 2, 7u));
    expect(!interrupt.retired, "enabled pending interrupt must preempt instruction issue");
    expect(interrupt.exception->code == ExceptionCode::Interrupt, "interrupt code");
    expect_eq(machine.cpu().gpr[2], 0x11111111u, "interrupted instruction has no effect");
    expect_eq(interrupt.cycles.issue_cycles, 0u, "interrupt issues no guest instruction");
    expect_eq(interrupt.cycles.result_latency_cycles, 0u,
              "interrupt has no guest result latency");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = 1u;
    machine.cpu().gpr[2] = 1u;
    step_ok(machine, i_type(0x04u, 1, 2, 2u));
    machine.set_hardware_interrupts(1u);
    machine.cop0().status = r3000a::reference::kStatusCurrentInterruptEnable |
                            (1u << 10u);
    const auto delay_interrupt = machine.step_word(i_type(0x09u, 0, 3, 9u));
    expect(!delay_interrupt.retired, "interrupt may occur before a branch delay slot");
    expect(delay_interrupt.exception->in_delay_slot, "interrupt delay-slot metadata");
    expect_eq(machine.cop0().epc, kPc, "delay-slot interrupt EPC is branch PC");
    expect((machine.cop0().cause & r3000a::reference::kCauseBd) != 0u,
           "delay-slot interrupt sets Cause.BD");
    expect_eq(machine.cpu().gpr[3], 0u, "preempted delay-slot instruction has no effect");

    store_ok(memory.store32le(kBase + 0x40u, 0x76543210u), "interrupt load seed");
    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x40u;
    step_ok(machine, i_type(0x23u, 1, 2, 0u));
    machine.set_hardware_interrupts(1u);
    machine.cop0().status = r3000a::reference::kStatusCurrentInterruptEnable |
                            (1u << 10u);
    const auto interrupt_after_load = machine.step_word(nop());
    expect(!interrupt_after_load.retired, "interrupt after load must raise");
    expect_eq(machine.cpu().gpr[2], 0x76543210u,
              "older delayed load commits before interrupt entry");
}

void test_coprocessor2_boundary() {
    Memory memory(kBase, 0x500u);
    FakeCoprocessor2 coprocessor;
    Machine machine(memory, test_reset(), &coprocessor);
    machine.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;

    const auto mfc2 = r3000a::reference::decode(cop2_type(0x00u, 2, 3));
    const auto cfc2 = r3000a::reference::decode(cop2_type(0x02u, 2, 3));
    const auto mtc2 = r3000a::reference::decode(cop2_type(0x04u, 2, 3));
    const auto ctc2 = r3000a::reference::decode(cop2_type(0x06u, 2, 3));
    const auto bc2f = r3000a::reference::decode(i_type(0x12u, 0x08u, 0u, 2u));
    const auto bc2t = r3000a::reference::decode(i_type(0x12u, 0x08u, 1u, 2u));
    expect(mfc2 && mfc2.instruction->op == Op::Mfc2, "MFC2 typed decode");
    expect(cfc2 && cfc2.instruction->op == Op::Cfc2, "CFC2 typed decode");
    expect(mtc2 && mtc2.instruction->op == Op::Mtc2, "MTC2 typed decode");
    expect(ctc2 && ctc2.instruction->op == Op::Ctc2, "CTC2 typed decode");
    expect(bc2f && bc2f.instruction->op == Op::Bc2f, "BC2F typed decode");
    expect(bc2t && bc2t.instruction->op == Op::Bc2t, "BC2T typed decode");
    expect(r3000a::reference::decode(i_type(0x32u, 1, 3, 0u)).instruction->op == Op::Lwc2,
           "LWC2 typed decode");
    expect(r3000a::reference::decode(i_type(0x3Au, 1, 3, 0u)).instruction->op == Op::Swc2,
           "SWC2 typed decode");

    machine.cpu().gpr[2] = 0x11223344u;
    step_ok(machine, cop2_type(0x04u, 2, 3));
    expect_eq(coprocessor.data[3], 0u, "MTC2 has a transfer delay");
    run_nop(machine);
    expect_eq(coprocessor.data[3], 0x11223344u, "MTC2 data write");
    machine.cpu().gpr[2] = 0x55667788u;
    step_ok(machine, cop2_type(0x06u, 2, 4));
    expect_eq(coprocessor.control[4], 0u, "CTC2 has a transfer delay");
    run_nop(machine);
    expect_eq(coprocessor.control[4], 0x55667788u, "CTC2 control write");

    coprocessor.data[5] = 0x89ABCDEFu;
    machine.cpu().gpr[6] = 0x01020304u;
    const auto mfc2_step = machine.step_word(cop2_type(0x00u, 6, 5));
    expect(mfc2_step.retired, "MFC2 must retire");
    expect_eq(machine.cpu().gpr[6], 0x01020304u, "MFC2 transfer delay");
    run_nop(machine);
    expect_eq(machine.cpu().gpr[6], 0x89ABCDEFu, "MFC2 delayed result");
    expect_eq(mfc2_step.cycles.result_latency_cycles, 2u, "MFC2 latency metadata");

    coprocessor.control[7] = 0x0BADF00Du;
    step_ok(machine, cop2_type(0x02u, 8, 7));
    run_nop(machine);
    expect_eq(machine.cpu().gpr[8], 0x0BADF00Du, "CFC2 delayed result");

    const std::uint32_t command = cop2_command(0x12345u);
    const auto command_step = machine.step_word(command);
    expect(command_step.retired, "COP2 command must retire through attached port");
    expect_eq(coprocessor.last_command, command, "COP2 command preserves raw word");
    expect_eq(coprocessor.command_count, 1u, "COP2 command dispatch count");
    expect_eq(command_step.cycles.result_latency_cycles, coprocessor.command_latency,
              "COP2 command latency comes from typed port");
    expect_eq(command_step.cycles.side_effect_latency_cycles, coprocessor.command_latency,
              "COP2 command side-effect latency comes from typed port");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;
    coprocessor.condition = true;
    step_ok(machine, i_type(0x12u, 0x08u, 1u, 2u));
    expect(machine.cpu().branch_pending, "taken BC2T records a pending redirect");
    expect_eq(machine.cpu().pc, kPc + 4u, "BC2T enters its delay slot");
    step_ok(machine, i_type(0x09u, 0u, 11u, 0x66u));
    expect_eq(machine.cpu().gpr[11], 0x66u, "BC2T delay slot executes");
    expect_eq(machine.cpu().pc, kPc + 12u, "BC2T branches on the device condition signal");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;
    coprocessor.condition = false;
    step_ok(machine, i_type(0x12u, 0x08u, 1u, 2u));
    step_ok(machine, i_type(0x09u, 0u, 12u, 0x44u));
    expect_eq(machine.cpu().gpr[12], 0x44u, "not-taken BC2T delay slot executes");
    expect_eq(machine.cpu().pc, kPc + 8u, "BC2T falls through when the condition is false");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;
    coprocessor.condition = true;
    step_ok(machine, i_type(0x12u, 0x08u, 0u, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, kPc + 8u, "BC2F falls through when the condition is true");

    machine.reset(test_reset());
    machine.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;
    coprocessor.condition = false;
    step_ok(machine, i_type(0x12u, 0x08u, 0u, 2u));
    run_nop(machine);
    expect_eq(machine.cpu().pc, kPc + 12u, "BC2F branches when the condition is false");

    store_ok(memory.store32le(kBase + 0x80u, 0xCAFED00Du), "LWC2 seed");
    machine.cpu().gpr[1] = kBase + 0x80u;
    step_ok(machine, i_type(0x32u, 1, 9, 0u));
    expect_eq(coprocessor.data[9], 0u, "LWC2 has a coprocessor write delay");
    run_nop(machine);
    expect_eq(coprocessor.data[9], 0xCAFED00Du, "LWC2 data write");
    coprocessor.data[10] = 0x13579BDFu;
    step_ok(machine, i_type(0x3Au, 1, 10, 4u));
    expect_eq(memory.load32le(kBase + 0x84u).value(), 0x13579BDFu, "SWC2 memory write");

    machine.reset(test_reset());
    const auto disabled_branch = machine.step_word(i_type(0x12u, 0x08u, 1u, 2u));
    expect(!disabled_branch.retired &&
               disabled_branch.exception->code == ExceptionCode::CoprocessorUnusable,
           "disabled BC2 condition branch raises coprocessor unusable");
    expect(disabled_branch.exception->has_coprocessor &&
               disabled_branch.exception->coprocessor == 2u,
           "disabled BC2 condition branch records Cause.CE");

    machine.reset(test_reset());
    const auto disabled = machine.step_word(cop2_type(0x00u, 2, 3));
    expect(!disabled.retired, "disabled COP2 must raise");
    expect(disabled.exception->code == ExceptionCode::CoprocessorUnusable,
           "disabled COP2 exception code");
    expect(disabled.exception->has_coprocessor && disabled.exception->coprocessor == 2u,
           "disabled COP2 metadata");
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseCoprocessorMask,
              2u << r3000a::reference::kCauseCoprocessorShift,
              "disabled COP2 Cause.CE");

    Machine unattached(memory, test_reset());
    unattached.cop0().status = r3000a::reference::kStatusCoprocessorUsable2;
    const auto missing = unattached.step_word(cop2_command(1u));
    expect(!missing.retired && missing.exception->code == ExceptionCode::CoprocessorUnusable,
           "enabled but unattached COP2 fails explicitly");

    Machine cop1(memory, test_reset());
    const auto unavailable = cop1.step_word(0x11u << 26u);
    expect(!unavailable.retired &&
               unavailable.exception->code == ExceptionCode::CoprocessorUnusable,
           "valid COP1 space is not mislabeled Reserved");
    expect(unavailable.exception->has_coprocessor && unavailable.exception->coprocessor == 1u,
           "COP1 Cause.CE metadata");

    Machine cop3(memory, test_reset());
    const auto mfc3 = cop3.step_word(0x13u << 26u);
    expect(!mfc3.retired && mfc3.exception->code == ExceptionCode::ReservedInstruction,
           "R3000A MFC3 raises reserved instruction");
    expect(!mfc3.exception->has_coprocessor,
           "R3000A MFC3 does not report coprocessor-unusable metadata");

    cop3.reset(test_reset());
    const auto cfc3 = cop3.step_word((0x13u << 26u) | (0x02u << 21u));
    expect(!cfc3.retired && cfc3.exception->code == ExceptionCode::CoprocessorUnusable,
           "CFC3 remains an unavailable coprocessor operation");
    expect(cfc3.exception->has_coprocessor && cfc3.exception->coprocessor == 3u,
           "CFC3 records Cause.CE metadata");

    cop3.reset(test_reset());
    const auto lwc3 = cop3.step_word(i_type(0x33u, 1, 2, 0u));
    expect(!lwc3.retired && lwc3.exception->code == ExceptionCode::CoprocessorUnusable,
           "LWC3 raises coprocessor unusable");
    expect(lwc3.exception->has_coprocessor && lwc3.exception->coprocessor == 3u,
           "LWC3 Cause.CE metadata");

    const auto malformed = r3000a::reference::decode(cop2_type(0x01u, 2, 3));
    expect(!malformed && malformed.error == "unknown COP2 function",
           "malformed COP2 encoding fails decode explicitly");
    const auto malformed_branch =
        r3000a::reference::decode(i_type(0x12u, 0x08u, 2u, 0u));
    expect(!malformed_branch && malformed_branch.error == "unknown COP2 branch condition",
           "malformed COP2 condition branch fails decode explicitly");
    const auto malformed_bc0 =
        r3000a::reference::decode(i_type(0x10u, 0x08u, 2u, 0u));
    expect(!malformed_bc0 && malformed_bc0.error == "unknown COP0 branch condition",
           "malformed COP0 condition branch fails decode explicitly");
}

void test_cycle_metadata() {
    const auto alu = r3000a::reference::cycle_metadata(Op::Addu);
    const auto multiply = r3000a::reference::cycle_metadata(Op::Mult);
    const auto divide = r3000a::reference::cycle_metadata(Op::Divu);
    const auto load = r3000a::reference::cycle_metadata(Op::Lw);
    const auto store = r3000a::reference::cycle_metadata(Op::Sw);
    const auto mtc0 = r3000a::reference::cycle_metadata(Op::Mtc0);
    expect_eq(alu.issue_cycles, 1u, "ALU issue cycles");
    expect_eq(alu.result_latency_cycles, 1u, "ALU result latency");
    expect_eq(multiply.result_latency_cycles, 12u, "multiply result latency");
    expect_eq(divide.result_latency_cycles, 35u, "divide result latency");
    expect_eq(load.result_latency_cycles, 2u, "load result latency");
    expect_eq(mtc0.result_latency_cycles, 2u, "MTC0 register visibility latency");
    expect_eq(mtc0.side_effect_latency_cycles, 3u, "MTC0 control-effect latency");
    expect(load.external_wait_states, "load exposes external wait-state boundary");
    expect(store.external_wait_states, "store exposes external wait-state boundary");
}

void test_unaligned_memory_operations() {
    Memory memory(kBase, 0x600u);
    Machine machine(memory, test_reset());
    constexpr std::uint32_t aligned = kBase + 0x80u;
    const std::array<std::uint8_t, 4> memory_bytes{0x10u, 0x21u, 0x32u, 0x43u};
    const std::array<std::uint8_t, 4> register_bytes{0xA0u, 0xB1u, 0xC2u, 0xD3u};

    for (unsigned offset = 0; offset < 4u; ++offset) {
        store_ok(memory.store32le(aligned, word_of(memory_bytes)), "LWL model seed");
        machine.reset(test_reset());
        machine.cpu().gpr[1] = aligned + offset;
        machine.cpu().gpr[2] = word_of(register_bytes);
        step_ok(machine, i_type(0x22u, 1, 2, 0u));
        run_nop(machine);
        expect_eq(machine.cpu().gpr[2], word_of(model_lwl(memory_bytes, register_bytes, offset)),
                  "single LWL byte-lane model");

        machine.reset(test_reset());
        machine.cpu().gpr[1] = aligned + offset;
        machine.cpu().gpr[2] = word_of(register_bytes);
        step_ok(machine, i_type(0x26u, 1, 2, 0u));
        run_nop(machine);
        expect_eq(machine.cpu().gpr[2], word_of(model_lwr(memory_bytes, register_bytes, offset)),
                  "single LWR byte-lane model");

        store_ok(memory.store32le(aligned, word_of(memory_bytes)), "SWL model seed");
        machine.reset(test_reset());
        machine.cpu().gpr[1] = aligned + offset;
        machine.cpu().gpr[2] = word_of(register_bytes);
        step_ok(machine, i_type(0x2Au, 1, 2, 0u));
        const auto after_swl = memory.snapshot();
        const auto expected_swl = model_swl(memory_bytes, register_bytes, offset);
        for (unsigned lane = 0; lane < 4u; ++lane) {
            expect_eq(after_swl[0x80u + lane], expected_swl[lane],
                      "single SWL byte-lane model");
        }

        store_ok(memory.store32le(aligned, word_of(memory_bytes)), "SWR model seed");
        machine.reset(test_reset());
        machine.cpu().gpr[1] = aligned + offset;
        machine.cpu().gpr[2] = word_of(register_bytes);
        step_ok(machine, i_type(0x2Eu, 1, 2, 0u));
        const auto after_swr = memory.snapshot();
        const auto expected_swr = model_swr(memory_bytes, register_bytes, offset);
        for (unsigned lane = 0; lane < 4u; ++lane) {
            expect_eq(after_swr[0x80u + lane], expected_swr[lane],
                      "single SWR byte-lane model");
        }
    }

    for (std::uint32_t i = 0; i < 0x100u; ++i) {
        store_ok(memory.store8(kBase + 0x100u + i,
                               static_cast<std::uint8_t>(i * 3u + 1u)),
                 "initial unaligned byte");
    }

    for (std::uint32_t offset = 0; offset < 4u; ++offset) {
        machine.reset(test_reset());
        const std::uint32_t address = kBase + 0x104u + offset;
        machine.cpu().gpr[1] = address;
        machine.cpu().gpr[2] = 0xA5A5A5A5u;
        step_ok(machine, i_type(0x22u, 1, 2, 3u));
        step_ok(machine, i_type(0x26u, 1, 2, 0u));
        run_nop(machine);
        const auto snapshot = memory.snapshot();
        const std::size_t start = static_cast<std::size_t>(address - kBase);
        const std::array<std::uint8_t, 4> expected_bytes{
            snapshot[start], snapshot[start + 1u], snapshot[start + 2u], snapshot[start + 3u]};
        const std::uint32_t expected = word_of(expected_bytes);
        expect_eq(machine.cpu().gpr[2], expected, "LWL/LWR pair");
    }

    for (std::uint32_t offset = 0; offset < 4u; ++offset) {
        store_ok(memory.store32le(kBase + 0x180u, 0xAAAAAAAAu), "initial SWL/SWR word 0");
        store_ok(memory.store32le(kBase + 0x184u, 0xAAAAAAAAu), "initial SWL/SWR word 1");
        machine.reset(test_reset());
        const std::uint32_t address = kBase + 0x180u + offset;
        machine.cpu().gpr[1] = address;
        machine.cpu().gpr[2] = 0x11223344u;
        step_ok(machine, i_type(0x2Au, 1, 2, 3u));
        step_ok(machine, i_type(0x2Eu, 1, 2, 0u));
        const auto bytes = memory.snapshot();
        const std::size_t start = static_cast<std::size_t>(address - kBase);
        expect_eq(bytes[start], static_cast<std::uint8_t>(0x44u), "SWL/SWR byte 0");
        expect_eq(bytes[start + 1u], static_cast<std::uint8_t>(0x33u), "SWL/SWR byte 1");
        expect_eq(bytes[start + 2u], static_cast<std::uint8_t>(0x22u), "SWL/SWR byte 2");
        expect_eq(bytes[start + 3u], static_cast<std::uint8_t>(0x11u), "SWL/SWR byte 3");
    }

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x01u;
    const auto bad_load = machine.step_word(i_type(0x23u, 1, 2, 0u));
    expect(!bad_load.retired, "unaligned LW must fault");
    expect(bad_load.exception->code == ExceptionCode::AddressErrorLoad,
           "unaligned LW exception code");
    expect_eq(machine.cop0().bad_vaddr, kBase + 1u, "unaligned LW BadVAddr");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 0x01u;
    machine.cpu().gpr[2] = 0x12345678u;
    const auto bad_store = machine.step_word(i_type(0x2Bu, 1, 2, 0u));
    expect(!bad_store.retired, "unaligned SW must fault");
    expect(bad_store.exception->code == ExceptionCode::AddressErrorStore,
           "unaligned SW exception code");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 1u;
    const auto bad_half_load = machine.step_word(i_type(0x21u, 1, 2, 0u));
    expect(!bad_half_load.retired, "unaligned LH must fault");
    expect(bad_half_load.exception->code == ExceptionCode::AddressErrorLoad,
           "unaligned LH exception code");

    machine.reset(test_reset());
    machine.cpu().gpr[1] = kBase + 1u;
    const auto bad_half_store = machine.step_word(i_type(0x29u, 1, 2, 0u));
    expect(!bad_half_store.retired, "unaligned SH must fault");
    expect(bad_half_store.exception->code == ExceptionCode::AddressErrorStore,
           "unaligned SH exception code");

    machine.reset(test_reset(kPc + 2u));
    const auto bad_fetch = machine.step_word(nop());
    expect(!bad_fetch.retired, "misaligned instruction PC must fault");
    expect(bad_fetch.exception->code == ExceptionCode::AddressErrorLoad,
           "misaligned instruction exception code");
    expect_eq(machine.cop0().bad_vaddr, kPc + 2u, "misaligned instruction BadVAddr");
    expect_eq(bad_fetch.cycles.issue_cycles, 0u,
              "misaligned instruction fetch does not issue");
    expect_eq(bad_fetch.cycles.result_latency_cycles, 0u,
              "misaligned instruction fetch has no result latency");
    expect_eq(bad_fetch.cycles.side_effect_latency_cycles, 0u,
              "misaligned instruction fetch has no side-effect latency");
    expect(!bad_fetch.cycles.external_wait_states,
           "misaligned instruction fetch has no data wait-state classification");

    Memory bounded_memory(kBase, 0x40u);
    Machine bus_load(bounded_memory, test_reset());
    bus_load.cop0().bad_vaddr = 0x12345678u;
    bus_load.cpu().gpr[1] = kBase + 0x40u;
    const auto failed_read = bus_load.step_word(i_type(0x23u, 1, 2, 0u));
    expect(!failed_read.retired && failed_read.exception->code == ExceptionCode::BusErrorData,
           "aligned rejected memory read is a data bus error");
    expect(!failed_read.exception->has_bad_vaddr,
           "data bus error does not report architectural BadVAddr");
    expect_eq(bus_load.cop0().bad_vaddr, 0x12345678u,
              "data bus error preserves BadVAddr");

    Machine bus_store(bounded_memory, test_reset());
    bus_store.cop0().bad_vaddr = 0x87654321u;
    bus_store.cpu().gpr[1] = kBase + 0x40u;
    bus_store.cpu().gpr[2] = 0xCAFEBABEu;
    const auto failed_write = bus_store.step_word(i_type(0x2Bu, 1, 2, 0u));
    expect(!failed_write.retired && failed_write.exception->code == ExceptionCode::BusErrorData,
           "flat-memory rejected write uses the documented DBE harness policy");
    expect(!failed_write.exception->has_bad_vaddr,
           "rejected-write harness fault does not report architectural BadVAddr");
    expect_eq(bus_store.cop0().bad_vaddr, 0x87654321u,
              "rejected-write harness fault preserves BadVAddr");
}

void test_reserved_and_r0_invariants() {
    Memory memory(kBase, 0x200u);
    Machine machine(memory, test_reset());
    machine.cpu().gpr[0] = 0xFFFFFFFFu;
    machine.cpu().gpr[1] = 3u;
    machine.cpu().gpr[2] = 4u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x21));
    expect_eq(machine.cpu().gpr[0], 0u, "r0 remains zero after ALU write");

    machine.reset(test_reset());
    const auto reserved = machine.step_word(0xFFFFFFFFu);
    expect(!reserved.retired, "unknown opcode must fault closed");
    expect(reserved.exception->code == ExceptionCode::ReservedInstruction,
           "reserved instruction code");
    expect_eq(machine.cop0().epc, kPc, "reserved instruction EPC");

    machine.reset(test_reset());
    const auto malformed_branch = machine.step_word(i_type(0x06u, 1, 2, 0u));
    expect(!malformed_branch.retired, "malformed BLEZ encoding must fault closed");
    expect(malformed_branch.exception->code == ExceptionCode::ReservedInstruction,
           "malformed BLEZ reserved code");

    const std::uint32_t swc3_word = i_type(0x3Bu, 1, 2, 0u);
    const auto swc3_decode = r3000a::reference::decode(swc3_word);
    expect(!swc3_decode && swc3_decode.error == "SWC3 is reserved on R3000A",
           "SWC3 decodes as the architectural reserved case");
    machine.reset(test_reset());
    machine.cop0().cause = r3000a::reference::kCauseCoprocessorMask;
    const auto swc3 = machine.step_word(swc3_word);
    expect(!swc3.retired && swc3.exception->code == ExceptionCode::ReservedInstruction,
           "SWC3 raises Reserved Instruction rather than Coprocessor Unusable");
    expect(!swc3.exception->has_coprocessor,
           "SWC3 Reserved Instruction has no coprocessor metadata");
    expect_eq(machine.cop0().cause & r3000a::reference::kCauseCoprocessorMask, 0u,
              "SWC3 Reserved Instruction clears Cause.CE");

    machine.reset(test_reset());
    r3000a::reference::Instruction malformed_fields;
    malformed_fields.op = Op::Addu;
    malformed_fields.rs = 32u;
    malformed_fields.rt = 1u;
    malformed_fields.rd = 2u;
    const auto malformed_direct = machine.step(malformed_fields);
    expect(!malformed_direct.retired, "out-of-range direct instruction fields must fault");
    expect(malformed_direct.exception->code == ExceptionCode::ReservedInstruction,
           "out-of-range direct instruction field code");

    machine.reset(test_reset());
    r3000a::reference::Instruction malformed_op;
    malformed_op.op = static_cast<Op>(std::numeric_limits<std::uint8_t>::max());
    const auto invalid_op = machine.step(malformed_op);
    expect(!invalid_op.retired &&
               invalid_op.exception->code == ExceptionCode::ReservedInstruction,
           "out-of-range direct Op must fault closed");

    machine.reset(test_reset());
    r3000a::reference::Instruction oversized_target;
    oversized_target.op = Op::J;
    oversized_target.target = std::uint32_t{1} << 26u;
    const auto invalid_target = machine.step(oversized_target);
    expect(!invalid_target.retired &&
               invalid_target.exception->code == ExceptionCode::ReservedInstruction,
           "out-of-range direct jump target must fault closed");
}

std::uint64_t next_random(std::uint64_t& state) {
    state ^= state << 7u;
    state ^= state >> 9u;
    state ^= state << 8u;
    return state;
}

void test_randomized_invariants() {
    Memory memory(kBase, 0x1000u);
    std::uint64_t random = 0xD1B54A32D192ED03ull;
    for (unsigned iteration = 0; iteration < 384u; ++iteration) {
        for (std::uint32_t i = 0; i < 0x1000u; ++i) {
            store_ok(memory.store8(kBase + i,
                                   static_cast<std::uint8_t>(next_random(random) & 0xFFu)),
                     "random memory fill");
        }
        const std::uint32_t address = kBase + 0x100u +
                                      static_cast<std::uint32_t>(next_random(random) % 0x700u);
        const std::uint32_t value = static_cast<std::uint32_t>(next_random(random));
        const auto before_load = memory.snapshot();
        const std::size_t load_start = static_cast<std::size_t>(address - kBase);
        const std::array<std::uint8_t, 4> expected_load_bytes{
            before_load[load_start], before_load[load_start + 1u],
            before_load[load_start + 2u], before_load[load_start + 3u]};
        const std::uint32_t expected_word = word_of(expected_load_bytes);

        Machine loads(memory, test_reset());
        loads.cpu().gpr[1] = address;
        loads.cpu().gpr[2] = static_cast<std::uint32_t>(next_random(random));
        step_ok(loads, i_type(0x22u, 1, 2, 3u));
        step_ok(loads, i_type(0x26u, 1, 2, 0u));
        step_ok(loads, nop());
        expect_eq(loads.cpu().gpr[2], expected_word, "random LWL/LWR invariant");

        const std::uint32_t store_address = kBase + 0x300u +
                                             static_cast<std::uint32_t>(next_random(random) % 0x500u);
        store_ok(memory.store32le(store_address & ~3u, 0xDEADBEEFu),
                 "random store word 0");
        store_ok(memory.store32le((store_address & ~3u) + 4u, 0xDEADBEEFu),
                 "random store word 1");
        Machine stores(memory, test_reset());
        stores.cpu().gpr[1] = store_address;
        stores.cpu().gpr[2] = value;
        step_ok(stores, i_type(0x2Au, 1, 2, 3u));
        step_ok(stores, i_type(0x2Eu, 1, 2, 0u));
        const auto bytes = memory.snapshot();
        const std::size_t start = static_cast<std::size_t>(store_address - kBase);
        expect_eq(bytes[start], static_cast<std::uint8_t>(value & 0xFFu),
                  "random SWL/SWR byte 0");
        expect_eq(bytes[start + 1u], static_cast<std::uint8_t>((value >> 8u) & 0xFFu),
                  "random SWL/SWR byte 1");
        expect_eq(bytes[start + 2u], static_cast<std::uint8_t>((value >> 16u) & 0xFFu),
                  "random SWL/SWR byte 2");
        expect_eq(bytes[start + 3u], static_cast<std::uint8_t>((value >> 24u) & 0xFFu),
                  "random SWL/SWR byte 3");

        Machine alu(memory, test_reset());
        const std::uint32_t lhs = static_cast<std::uint32_t>(next_random(random));
        const std::uint32_t rhs = static_cast<std::uint32_t>(next_random(random));
        alu.cpu().gpr[1] = lhs;
        alu.cpu().gpr[2] = rhs;
        step_ok(alu, r_type(1, 2, 3, 0, 0x21));
        step_ok(alu, r_type(3, 2, 4, 0, 0x23));
        expect_eq(alu.cpu().gpr[4], lhs, "random ADDU/SUBU round-trip invariant");
        step_ok(alu, r_type(2, 1, 5, 0, 0x21));
        expect_eq(alu.cpu().gpr[5], alu.cpu().gpr[3],
                  "random ADDU commutativity invariant");
        step_ok(alu, r_type(1, 2, 6, 0, 0x26));
        alu.cpu().gpr[7] = alu.cpu().gpr[6];
        step_ok(alu, r_type(7, 2, 7, 0, 0x26));
        expect_eq(alu.cpu().gpr[7], lhs, "random XOR involution invariant");
        step_ok(alu, r_type(1, 2, 0, 0, 0x25));
        expect_eq(alu.cpu().gpr[0], 0u, "random r0 invariant");

        const std::uint32_t branch_lhs = static_cast<std::uint32_t>(next_random(random));
        const std::uint32_t branch_rhs = static_cast<std::uint32_t>(next_random(random));
        Machine first_branch(memory, test_reset());
        Machine complementary_branch(memory, test_reset());
        for (Machine* branch : {&first_branch, &complementary_branch}) {
            branch->cpu().gpr[1] = branch_lhs;
            branch->cpu().gpr[2] = branch_rhs;
        }
        const bool equality_pair = (iteration & 1u) == 0u;
        const std::uint32_t first_word = equality_pair
                                             ? i_type(0x04u, 1, 2, 2u)
                                             : i_type(0x06u, 1, 0, 2u);
        const std::uint32_t complementary_word = equality_pair
                                                     ? i_type(0x05u, 1, 2, 2u)
                                                     : i_type(0x07u, 1, 0, 2u);
        step_ok(first_branch, first_word);
        step_ok(complementary_branch, complementary_word);
        expect_eq(first_branch.cpu().pc, kPc + 4u, "random first branch delay PC");
        expect_eq(complementary_branch.cpu().pc, kPc + 4u,
                  "random complementary branch delay PC");
        const std::uint32_t delay_word =
            i_type(0x09u, 0, 3, static_cast<std::uint16_t>(iteration));
        step_ok(first_branch, delay_word);
        step_ok(complementary_branch, delay_word);
        expect_eq(first_branch.cpu().gpr[3], iteration,
                  "random first branch delay execution");
        expect_eq(complementary_branch.cpu().gpr[3], iteration,
                  "random complementary branch delay execution");
        const bool first_taken = first_branch.cpu().pc == kPc + 12u;
        const bool complementary_taken = complementary_branch.cpu().pc == kPc + 12u;
        expect(first_branch.cpu().pc == kPc + 8u || first_taken,
               "random first branch continuation belongs to the valid pair");
        expect(complementary_branch.cpu().pc == kPc + 8u || complementary_taken,
               "random complementary branch continuation belongs to the valid pair");
        expect(first_taken != complementary_taken,
               "random complementary branch predicates have opposite outcomes");
    }
}

using Test = std::pair<const char*, void (*)()>;

} // namespace

int main() {
    const std::vector<Test> tests{
        {"reset_and_segment_mapping", test_reset_and_segment_mapping},
        {"decode_and_integer_semantics", test_decode_and_integer_semantics},
        {"control_flow_and_delay_slots", test_control_flow_and_delay_slots},
        {"delayed_loads_and_cop0", test_delayed_loads_and_cop0},
        {"exception_interrupt_and_status_semantics",
         test_exception_interrupt_and_status_semantics},
        {"coprocessor2_boundary", test_coprocessor2_boundary},
        {"cycle_metadata", test_cycle_metadata},
        {"unaligned_memory_operations", test_unaligned_memory_operations},
        {"reserved_and_r0_invariants", test_reserved_and_r0_invariants},
        {"randomized_invariants", test_randomized_invariants},
    };

    unsigned passed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "PASS all " << passed << " tests\n";
    return 0;
}
