#include "r3000a/reference.hpp"

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
using r3000a::reference::Machine;
using r3000a::reference::Memory;

constexpr std::uint32_t kBase = 0x00001000u;
constexpr std::uint32_t kPc = kBase;

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

void test_decode_and_integer_semantics() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, kPc);
    machine.cpu().gpr[1] = 0x80000000u;
    machine.cpu().gpr[2] = 0x00000003u;
    step_ok(machine, r_type(1, 2, 3, 0, 0x21));
    expect_eq(machine.cpu().gpr[3], 0x80000003u, "ADDU");

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

    machine.reset(kPc);
    machine.cpu().gpr[1] = 31u;
    machine.cpu().gpr[2] = 0xFFFFFFFFu;
    step_ok(machine, r_type(0, 2, 3, 4, 0x00));
    expect_eq(machine.cpu().gpr[3], 0xFFFFFFF0u, "SLL");
    step_ok(machine, r_type(1, 3, 4, 0, 0x07));
    expect_eq(machine.cpu().gpr[4], 0xFFFFFFFFu, "SRAV");
    expect_eq(machine.cpu().gpr[0], 0u, "r0 invariant");

    machine.reset(kPc);
    machine.cpu().gpr[1] = 0xFFFFFFFEu;
    machine.cpu().gpr[2] = 3u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x18));
    expect_eq(machine.cpu().hi, 0xFFFFFFFFu, "MULT HI");
    expect_eq(machine.cpu().lo, 0xFFFFFFFAu, "MULT LO");
    step_ok(machine, r_type(0, 0, 3, 0, 0x12));
    expect_eq(machine.cpu().gpr[3], 0xFFFFFFFAu, "MFLO");
    machine.cpu().gpr[1] = 0xFFFFFFF9u;
    machine.cpu().gpr[2] = 3u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x1Au));
    expect_eq(machine.cpu().lo, 0xFFFFFFFEu, "DIV quotient");
    expect_eq(machine.cpu().hi, 0xFFFFFFFFu, "DIV remainder");
}

void test_control_flow_and_delay_slots() {
    Memory memory(kBase, 0x400u);
    Machine machine(memory, kPc);

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

    machine.reset(kPc);
    machine.cpu().gpr[1] = 9u;
    machine.cpu().gpr[2] = 9u;
    step_ok(machine, i_type(0x05u, 1, 2, 3u));
    expect_eq(machine.cpu().pc, kPc + 4u, "not-taken branch enters delay slot");
    step_ok(machine, i_type(0x09u, 0, 3, 0x55u));
    expect_eq(machine.cpu().gpr[3], 0x55u, "not-taken branch delay slot");
    expect_eq(machine.cpu().pc, kPc + 8u, "not-taken branch fall-through");

    machine.reset(kPc + 0x80u);
    machine.cpu().gpr[4] = kPc + 0x180u;
    step_ok(machine, r_type(4, 0, 0, 0, 0x08));
    step_ok(machine, i_type(0x09u, 0, 5, 0x33u));
    expect_eq(machine.cpu().gpr[5], 0x33u, "JR delay slot");
    expect_eq(machine.cpu().pc, kPc + 0x180u, "JR target");

    machine.reset(kPc + 0xA0u);
    machine.cpu().gpr[4] = kPc + 0x1C0u;
    step_ok(machine, r_type(4, 0, 6, 0, 0x09));
    expect_eq(machine.cpu().gpr[6], kPc + 0xA8u, "JALR link rd");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 0x1C0u, "JALR target");

    machine.reset(kPc + 0x200u);
    machine.cpu().gpr[4] = kPc + 0x201u;
    machine.cpu().gpr[6] = 0x77777777u;
    const auto bad_jalr = machine.step_word(r_type(4, 0, 6, 0, 0x09));
    expect(!bad_jalr.retired, "misaligned JALR must fault immediately");
    expect(bad_jalr.exception->code == ExceptionCode::AddressErrorLoad,
           "misaligned JALR exception code");
    expect_eq(machine.cpu().gpr[6], 0x77777777u, "misaligned JALR must not link");
    expect_eq(machine.cpu().pc, 0x80000080u, "misaligned JALR exception vector");

    machine.reset(kPc + 0x240u);
    machine.cpu().gpr[4] = kPc + 0x241u;
    step_ok(machine, r_type(4, 0, 0, 0, 0x08));
    step_ok(machine, nop());
    const auto bad_jr_fetch = machine.step_word(nop());
    expect(!bad_jr_fetch.retired, "misaligned JR target must fault on fetch");
    expect(bad_jr_fetch.exception->code == ExceptionCode::AddressErrorLoad,
           "misaligned JR fetch exception code");
    expect_eq(machine.cop0().bad_vaddr, kPc + 0x241u, "misaligned JR BadVAddr");

    machine.reset(kPc);
    machine.cpu().gpr[1] = std::numeric_limits<std::uint32_t>::max();
    machine.cpu().gpr[31] = 0x11111111u;
    step_ok(machine, i_type(0x01u, 1, 0x10u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BLTZAL taken link");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BLTZAL target");

    machine.reset(kPc);
    machine.cpu().gpr[1] = 1u;
    machine.cpu().gpr[31] = 0x22222222u;
    step_ok(machine, i_type(0x01u, 1, 0x10u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BLTZAL not-taken still links");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 8u, "BLTZAL not-taken fall-through");

    machine.reset(kPc);
    machine.cpu().gpr[1] = 0u;
    step_ok(machine, i_type(0x01u, 1, 0x11u, 2u));
    expect_eq(machine.cpu().gpr[31], kPc + 8u, "BGEZAL taken link");
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BGEZAL target");

    machine.reset(kPc);
    machine.cpu().gpr[1] = 0u;
    step_ok(machine, i_type(0x06u, 1, 0, 2u));
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BLEZ target");

    machine.reset(kPc);
    machine.cpu().gpr[1] = 1u;
    step_ok(machine, i_type(0x07u, 1, 0, 2u));
    step_ok(machine, nop());
    expect_eq(machine.cpu().pc, kPc + 12u, "BGTZ target");

    machine.reset(kPc);
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
    Machine machine(memory, kPc);
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

    machine.reset(kPc);
    machine.cop0().status = 0x12340015u;
    step_ok(machine, cop0_type(0x00u, 2, 12u));
    expect_eq(machine.cpu().gpr[2], 0u, "MFC0 has a transfer delay");
    step_ok(machine, i_type(0x09u, 0, 3, 1u));
    expect_eq(machine.cpu().gpr[3], 1u, "MFC0 consumer sees old value");
    expect_eq(machine.cpu().gpr[2], 0x12340015u, "MFC0 commits after delay slot");

    machine.reset(kPc);
    machine.cop0().status = 0x0000002Du;
    step_ok(machine, cop0_type(0x10u, 0, 0, 0x10u));
    const std::uint32_t mode = 0x2Du;
    const std::uint32_t expected = (mode & ~0x0Fu) |
                                   ((mode & 0x3Cu) >> 2u);
    expect_eq(machine.cop0().status, expected, "RFE pops mode stack");

    machine.reset(kPc);
    machine.cop0().status = r3000a::reference::kStatusBev;
    const auto syscall = machine.step_word(r_type(0, 0, 0, 0, 0x0Cu));
    expect(!syscall.retired, "SYSCALL must raise");
    expect_eq(machine.cpu().pc, 0xBFC00180u, "BEV exception vector");
    expect(syscall.exception->code == ExceptionCode::Syscall, "SYSCALL code");

    machine.reset(kPc);
    machine.cop0().status = 0xABCD0035u;
    const auto status_exception = machine.step_word(r_type(0, 0, 0, 0, 0x0Cu));
    expect(!status_exception.retired, "second SYSCALL must raise");
    const std::uint32_t status_before = 0xABCD0035u;
    const std::uint32_t pushed_mode = ((status_before & 0x3Fu) << 2u & 0x3Cu) | 0x2u;
    const std::uint32_t expected_status = (status_before & ~0x3Fu) | pushed_mode;
    expect_eq(machine.cop0().status, expected_status, "exception status stack push");
}

void test_unaligned_memory_operations() {
    Memory memory(kBase, 0x600u);
    Machine machine(memory, kPc);
    for (std::uint32_t i = 0; i < 0x100u; ++i) {
        store_ok(memory.store8(kBase + 0x80u + i,
                               static_cast<std::uint8_t>(i * 3u + 1u)),
                 "initial unaligned byte");
    }

    for (std::uint32_t offset = 0; offset < 4u; ++offset) {
        machine.reset(kPc);
        const std::uint32_t address = kBase + 0x84u + offset;
        machine.cpu().gpr[1] = address;
        machine.cpu().gpr[2] = 0xA5A5A5A5u;
        step_ok(machine, i_type(0x22u, 1, 2, 3u));
        step_ok(machine, i_type(0x26u, 1, 2, 0u));
        run_nop(machine);
        const std::uint32_t expected = memory.load32le(address).value();
        expect_eq(machine.cpu().gpr[2], expected, "LWL/LWR pair");
    }

    for (std::uint32_t offset = 0; offset < 4u; ++offset) {
        store_ok(memory.store32le(kBase + 0x180u, 0xAAAAAAAAu), "initial SWL/SWR word 0");
        store_ok(memory.store32le(kBase + 0x184u, 0xAAAAAAAAu), "initial SWL/SWR word 1");
        machine.reset(kPc);
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

    machine.reset(kPc);
    machine.cpu().gpr[1] = kBase + 0x01u;
    const auto bad_load = machine.step_word(i_type(0x23u, 1, 2, 0u));
    expect(!bad_load.retired, "unaligned LW must fault");
    expect(bad_load.exception->code == ExceptionCode::AddressErrorLoad,
           "unaligned LW exception code");
    expect_eq(machine.cop0().bad_vaddr, kBase + 1u, "unaligned LW BadVAddr");

    machine.reset(kPc);
    machine.cpu().gpr[1] = kBase + 0x01u;
    machine.cpu().gpr[2] = 0x12345678u;
    const auto bad_store = machine.step_word(i_type(0x2Bu, 1, 2, 0u));
    expect(!bad_store.retired, "unaligned SW must fault");
    expect(bad_store.exception->code == ExceptionCode::AddressErrorStore,
           "unaligned SW exception code");
}

void test_reserved_and_r0_invariants() {
    Memory memory(kBase, 0x200u);
    Machine machine(memory, kPc);
    machine.cpu().gpr[0] = 0xFFFFFFFFu;
    machine.cpu().gpr[1] = 3u;
    machine.cpu().gpr[2] = 4u;
    step_ok(machine, r_type(1, 2, 0, 0, 0x21));
    expect_eq(machine.cpu().gpr[0], 0u, "r0 remains zero after ALU write");

    machine.reset(kPc);
    const auto reserved = machine.step_word(0xFFFFFFFFu);
    expect(!reserved.retired, "unknown opcode must fault closed");
    expect(reserved.exception->code == ExceptionCode::ReservedInstruction,
           "reserved instruction code");
    expect_eq(machine.cop0().epc, kPc, "reserved instruction EPC");

    machine.reset(kPc);
    const auto malformed_branch = machine.step_word(i_type(0x06u, 1, 2, 0u));
    expect(!malformed_branch.retired, "malformed BLEZ encoding must fault closed");
    expect(malformed_branch.exception->code == ExceptionCode::ReservedInstruction,
           "malformed BLEZ reserved code");
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
    for (unsigned iteration = 0; iteration < 512u; ++iteration) {
        for (std::uint32_t i = 0; i < 0x1000u; ++i) {
            store_ok(memory.store8(kBase + i,
                                   static_cast<std::uint8_t>(next_random(random) & 0xFFu)),
                     "random memory fill");
        }
        const std::uint32_t address = kBase + 0x100u +
                                      static_cast<std::uint32_t>(next_random(random) % 0x700u);
        const std::uint32_t value = static_cast<std::uint32_t>(next_random(random));
        const std::uint32_t expected_word = memory.load32le(address).value();

        Machine loads(memory, kPc);
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
        Machine stores(memory, kPc);
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

        Machine alu(memory, kPc);
        const std::uint32_t lhs = static_cast<std::uint32_t>(next_random(random));
        const std::uint32_t rhs = static_cast<std::uint32_t>(next_random(random));
        alu.cpu().gpr[1] = lhs;
        alu.cpu().gpr[2] = rhs;
        step_ok(alu, r_type(1, 2, 3, 0, 0x21));
        expect_eq(alu.cpu().gpr[3], lhs + rhs, "random ADDU invariant");
        step_ok(alu, r_type(1, 2, 4, 0, 0x23));
        expect_eq(alu.cpu().gpr[4], lhs - rhs, "random SUBU invariant");
        step_ok(alu, r_type(1, 2, 0, 0, 0x25));
        expect_eq(alu.cpu().gpr[0], 0u, "random r0 invariant");

        Machine branch(memory, kPc);
        const std::uint32_t branch_lhs = static_cast<std::uint32_t>(next_random(random));
        const std::uint32_t branch_rhs = static_cast<std::uint32_t>(next_random(random));
        branch.cpu().gpr[1] = branch_lhs;
        branch.cpu().gpr[2] = branch_rhs;
        const std::uint8_t branch_opcode = static_cast<std::uint8_t>(iteration % 4u);
        bool taken = false;
        switch (branch_opcode) {
        case 0u:
            taken = branch_lhs == branch_rhs;
            step_ok(branch, i_type(0x04u, 1, 2, 2u));
            break;
        case 1u:
            taken = branch_lhs != branch_rhs;
            step_ok(branch, i_type(0x05u, 1, 2, 2u));
            break;
        case 2u:
            taken = (branch_lhs & 0x80000000u) != 0u || branch_lhs == 0u;
            step_ok(branch, i_type(0x06u, 1, 0, 2u));
            break;
        default:
            taken = (branch_lhs & 0x80000000u) == 0u && branch_lhs != 0u;
            step_ok(branch, i_type(0x07u, 1, 0, 2u));
            break;
        }
        expect_eq(branch.cpu().pc, kPc + 4u, "random branch delay PC");
        step_ok(branch, i_type(0x09u, 0, 3, static_cast<std::uint16_t>(iteration)));
        expect_eq(branch.cpu().gpr[3], iteration, "random branch delay execution");
        expect_eq(branch.cpu().pc, taken ? kPc + 12u : kPc + 8u,
                  "random branch continuation");
    }
}

using Test = std::pair<const char*, void (*)()>;

} // namespace

int main() {
    const std::vector<Test> tests{
        {"decode_and_integer_semantics", test_decode_and_integer_semantics},
        {"control_flow_and_delay_slots", test_control_flow_and_delay_slots},
        {"delayed_loads_and_cop0", test_delayed_loads_and_cop0},
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
