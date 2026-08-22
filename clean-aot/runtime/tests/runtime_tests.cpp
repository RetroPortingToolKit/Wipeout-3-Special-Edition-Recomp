#include "clean_aot/runtime/runtime.hpp"
#include "clean_aot/runtime/native_dispatch.hpp"
#include "clean_aot/runtime/generated_cpu.hpp"
#include "r3000a/reference.hpp"
#include "gte.hpp"

#include "clean_aot_soc.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

class Runner final {
public:
    void expect(bool condition, std::string_view expression, int line) {
        if (!condition) {
            ++failures_;
            std::cerr << "line " << line << ": expectation failed: "
                      << expression << '\n';
        }
    }

    [[nodiscard]] int result() const noexcept { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

#define EXPECT(runner, expression) \
    (runner).expect(static_cast<bool>(expression), #expression, __LINE__)

std::unique_ptr<clean_aot::runtime::Runtime> make_runtime(
    std::uint64_t numerator = 9U,
    std::uint64_t denominator = 1U) {
    static const std::vector<std::uint8_t> bios(
        clean_aot::memory::kBiosBytes, 0U);
    clean_aot::runtime::Configuration configuration{};
    configuration.bios = bios;
    configuration.cpu_multiplier_numerator = numerator;
    configuration.cpu_multiplier_denominator = denominator;
    return std::make_unique<clean_aot::runtime::Runtime>(configuration);
}

bool generated_step_a(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.pc = state.branch_taken ? 0x00001020U : 0x00001010U;
    return true;
}

bool generated_step_b(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.pc = 0x00001030U;
    return true;
}

bool unresolved_generated_step(clean_aot::runtime::GeneratedCpuState&) noexcept {
    return false;
}

bool generated_self_loop(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.gpr[8] += 1U;
    return true;
}

bool generated_bios_reset_step(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.gpr[29] = 0x801f'fff0U;
    state.pc = 0xbfc0'0010U;
    state.next_pc = 0xbfc0'0014U;
    return true;
}
bool generated_cache_isolation_step(
    clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.cop0_status = 1U << 16U;
    if (!state.write8(state.memory_context, 0x0000'0100U, 0x11U) ||
        !state.write16(state.memory_context, 0x8000'0200U, 0x2233U) ||
        !state.write32(state.memory_context, 0x0000'0300U, 0x4455'6677U) ||
        !state.write32(state.memory_context, 0xa000'0400U, 0x89ab'cdefU)) {
        return false;
    }
    state.pc = 0xbfc0'0010U;
    return true;
}
bool generated_to_copied_epoch(clean_aot::runtime::GeneratedCpuState&state)noexcept{state.pc=0xA0000080U;return true;}bool generated_copied_step(clean_aot::runtime::GeneratedCpuState&state)noexcept{state.gpr[4]=0xC0DEF00DU;state.pc=0xA0000084U;return true;}bool generated_copied_alias_jump(clean_aot::runtime::GeneratedCpuState&state)noexcept{state.pc=0x000000C0U;return true;}bool generated_copied_alias_step(clean_aot::runtime::GeneratedCpuState&state)noexcept{state.gpr[5]=0xA11A5EDU;state.pc=0x000000C4U;return true;}

struct GeneratedMemory final { std::array<std::uint8_t,16> bytes{}; };
bool generated_read32(void* context,std::uint32_t address,std::uint32_t& value) noexcept {
    auto& memory=*static_cast<GeneratedMemory*>(context); if(address<0x1000U||address+4U>0x1010U)return false;
    const auto i=address-0x1000U;value=std::uint32_t(memory.bytes[i])|(std::uint32_t(memory.bytes[i+1U])<<8U)|(std::uint32_t(memory.bytes[i+2U])<<16U)|(std::uint32_t(memory.bytes[i+3U])<<24U);return true;
}
bool generated_write32(void* context,std::uint32_t address,std::uint32_t value) noexcept {
    auto& memory=*static_cast<GeneratedMemory*>(context);if(address<0x1000U||address+4U>0x1010U)return false;
    const auto i=address-0x1000U;for(unsigned lane=0;lane<4U;++lane)memory.bytes[i+lane]=static_cast<std::uint8_t>(value>>(lane*8U));return true;
}
bool rejected_read32(void*,std::uint32_t,std::uint32_t&) noexcept{return false;}
std::uint32_t cop2_read_data(void*c,std::uint8_t r)noexcept{return static_cast<clean_aot::gte::Gte*>(c)->read_data(r);}std::uint32_t cop2_read_control(void*c,std::uint8_t r)noexcept{return static_cast<clean_aot::gte::Gte*>(c)->read_control(r);}void cop2_write_data(void*c,std::uint8_t r,std::uint32_t v)noexcept{static_cast<clean_aot::gte::Gte*>(c)->write_data(r,v);}void cop2_write_control(void*c,std::uint8_t r,std::uint32_t v)noexcept{static_cast<clean_aot::gte::Gte*>(c)->write_control(r,v);}std::uint8_t cop2_execute(void*c,std::uint32_t raw,bool&recognized)noexcept{auto receipt=static_cast<clean_aot::gte::Gte*>(c)->execute(raw);recognized=receipt.recognized;return receipt.cycles;}

void test_authenticated_bios_reset_contract(Runner& runner) {
    using namespace clean_aot::runtime;
    constexpr std::string_view identity =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr std::string_view bios_sha =
        "07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541";
    const std::array<std::uint32_t, 1U> roots{0xbfc0'0000U};
    const std::array entries{
        GeneratedRegionEntry{0xbfc0'0000U, generated_bios_reset_step, 1U},
    };
    const GeneratedExecutableBinding binding{
        identity, bios_sha, 0xbfc0'0000U, 0xbfc8'0000ULL, roots, entries};
    auto runtime = make_runtime();
    EXPECT(runner, runtime->bios_pc() == 0U);
    EXPECT(runner, runtime->bind_authenticated_bios(identity, binding) ==
                       DispatchStatus::Bound);
    EXPECT(runner, runtime->bios_pc() == 0xbfc0'0000U);
    EXPECT(runner, runtime->dispatch_bios() == DispatchStatus::Executed);
    EXPECT(runner, runtime->last_bios_instruction_count() == 1U);
    EXPECT(runner, runtime->bios_pc() == 0xbfc0'0010U);
    EXPECT(runner, runtime->dispatch_bios() == DispatchStatus::UnknownCode);
    EXPECT(runner, runtime->reset() == Status::Ok);
    EXPECT(runner, runtime->bios_pc() == 0xbfc0'0000U);
    EXPECT(runner, runtime->dispatch_bios() == DispatchStatus::LoadRejected);
}

void test_generated_cache_isolation(Runner& runner) {
    using namespace clean_aot::runtime;
    constexpr std::string_view identity =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    constexpr std::string_view bios_sha =
        "07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541";
    const std::array<std::uint32_t, 1U> roots{0xbfc0'0000U};
    const std::array entries{GeneratedRegionEntry{
        0xbfc0'0000U, generated_cache_isolation_step, 4U}};
    const GeneratedExecutableBinding binding{
        identity, bios_sha, 0xbfc0'0000U, 0xbfc8'0000ULL, roots, entries};
    auto runtime = make_runtime();
    EXPECT(runner, runtime->write8({}, 0x100U, 0xaaU).ok());
    EXPECT(runner, runtime->write16({}, 0x200U, 0xbbccU).ok());
    EXPECT(runner, runtime->write32({}, 0x300U, 0xddee'ff00U).ok());
    EXPECT(runner, runtime->write32({}, 0x400U, 0x1020'3040U).ok());
    EXPECT(runner, runtime->bind_authenticated_bios(identity, binding) ==
                       DispatchStatus::Bound);
    EXPECT(runner, runtime->dispatch_bios() == DispatchStatus::Executed);
    EXPECT(runner,
           runtime->bios_execution_snapshot().cache_isolated_writes == 3U);
    EXPECT(runner, runtime->read8({}, 0x100U).value == 0xaaU);
    EXPECT(runner, runtime->read16({}, 0x200U).value == 0xbbccU);
    EXPECT(runner, runtime->read32({}, 0x300U).value == 0xddee'ff00U);
    EXPECT(runner, runtime->read32({}, 0x400U).value == 0x89ab'cdefU);
}

void test_authenticated_copied_epoch(Runner&runner){using namespace clean_aot::runtime;constexpr std::string_view identity="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";constexpr std::string_view bios_sha="07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541";constexpr std::string_view payload_sha="66840dda154e8a113c31dd0ad32f7f3a366a80e8136979d8f5a101d3d29d6f72";const std::array<std::uint32_t,1>bios_roots{0xBFC00000U};const std::array bios_entries{GeneratedRegionEntry{0xBFC00000U,generated_to_copied_epoch,1U}};const GeneratedExecutableBinding bios_binding{identity,bios_sha,0xBFC00000U,0xBFC80000ULL,bios_roots,bios_entries};auto runtime=make_runtime();EXPECT(runner,runtime->bind_authenticated_bios(identity,bios_binding)==DispatchStatus::Bound);for(std::uint32_t i=0;i<8U;++i){EXPECT(runner,runtime->write8({},0x1000U+i,static_cast<std::uint8_t>(i+1U)).ok());EXPECT(runner,runtime->write8({},0x2000U+i,static_cast<std::uint8_t>(i+1U)).ok());EXPECT(runner,runtime->write8({},0xA0000080U+i,static_cast<std::uint8_t>(i+1U)).ok());EXPECT(runner,runtime->write8({},0x000000C0U+i,static_cast<std::uint8_t>(i+1U)).ok());}EXPECT(runner,runtime->dispatch_bios()==DispatchStatus::Executed);const std::array<std::uint32_t,2>roots{0xA0000080U,0xA0000084U};const std::array entries{GeneratedRegionEntry{0xA0000080U,generated_copied_step,1U},GeneratedRegionEntry{0xA0000084U,generated_copied_alias_jump,1U}};const GeneratedExecutableBinding generated{identity,payload_sha,0xA0000080U,0xA0000088ULL,roots,entries};const Runtime::AuthenticatedCopiedEpochBinding epoch{identity,0x1000U,8U,payload_sha,payload_sha,generated};EXPECT(runner,runtime->bind_authenticated_copied_epoch(epoch)==DispatchStatus::Bound);const std::array<std::uint32_t,1>alias_roots{0x000000C0U};const std::array alias_entries{GeneratedRegionEntry{0x000000C0U,generated_copied_alias_step,1U}};const GeneratedExecutableBinding alias_generated{identity,payload_sha,0x000000C0U,0x000000C8ULL,alias_roots,alias_entries};const Runtime::AuthenticatedCopiedEpochBinding alias_epoch{identity,0x2000U,8U,payload_sha,payload_sha,alias_generated};EXPECT(runner,runtime->bind_authenticated_copied_epoch(alias_epoch)==DispatchStatus::Bound);EXPECT(runner,runtime->bind_authenticated_copied_epoch(alias_epoch)==DispatchStatus::InvalidBinding);EXPECT(runner,runtime->dispatch_bios()==DispatchStatus::Executed);EXPECT(runner,runtime->bios_pc()==0xA0000084U);EXPECT(runner,runtime->dispatch_bios()==DispatchStatus::Executed);EXPECT(runner,runtime->bios_pc()==0x000000C0U);EXPECT(runner,runtime->dispatch_bios()==DispatchStatus::Executed);EXPECT(runner,runtime->bios_pc()==0x000000C4U);EXPECT(runner,runtime->write8({},0xC0U,0xFFU).ok());EXPECT(runner,runtime->dispatch_bios()==DispatchStatus::EpochMismatch);auto wrong=epoch;wrong.source_sha256="ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";EXPECT(runner,runtime->bind_authenticated_copied_epoch(wrong)==DispatchStatus::ExecutableHashMismatch);}

void test_overlap_binding_refreshes_unchanged_exact_epoch(Runner& runner) {
    using namespace clean_aot::runtime;
    constexpr std::string_view identity =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    constexpr std::string_view bios_sha =
        "07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541";
    constexpr std::string_view payload_sha =
        "66840dda154e8a113c31dd0ad32f7f3a366a80e8136979d8f5a101d3d29d6f72";
    const std::array<std::uint32_t, 1U> bios_roots{0xBFC00000U};
    const std::array bios_entries{GeneratedRegionEntry{
        0xBFC00000U, generated_to_copied_epoch, 1U}};
    const GeneratedExecutableBinding bios_binding{
        identity, bios_sha, 0xBFC00000U, 0xBFC80000ULL, bios_roots,
        bios_entries};
    auto runtime = make_runtime();
    EXPECT(runner, runtime->bind_authenticated_bios(identity, bios_binding) ==
                       DispatchStatus::Bound);
    for (std::uint32_t index = 0U; index < 8U; ++index) {
        EXPECT(runner, runtime->write8({}, 0x2000U + index,
                                      static_cast<std::uint8_t>(index + 1U)).ok());
        EXPECT(runner, runtime->write8({}, 0xC0U + index,
                                      static_cast<std::uint8_t>(index + 1U)).ok());
    }
    const std::array<std::uint32_t, 1U> roots{0xC0U};
    const std::array entries{
        GeneratedRegionEntry{0xC0U, generated_copied_alias_step, 1U}};
    const GeneratedExecutableBinding generated{
        identity, payload_sha, 0xC0U, 0xC8ULL, roots, entries};
    const Runtime::AuthenticatedCopiedEpochBinding epoch{
        identity, 0x2000U, 8U, payload_sha, payload_sha, generated};
    EXPECT(runner, runtime->bind_authenticated_copied_epoch(epoch) ==
                       DispatchStatus::Bound);
    EXPECT(runner, runtime->write8({}, 0xC8U, 0x5aU).ok());
    EXPECT(runner, runtime->bind_authenticated_copied_epoch(epoch) ==
                       DispatchStatus::InvalidBinding);
}

void test_authenticated_generated_dispatch(Runner& runner) {
    using clean_aot::runtime::DispatchStatus;
    using clean_aot::runtime::GeneratedDispatcher;
    using clean_aot::runtime::GeneratedExecutableBinding;
    using clean_aot::runtime::GeneratedRegionEntry;

    constexpr std::string_view identity =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::array entries{
        GeneratedRegionEntry{0x00001000U, generated_step_a},
        GeneratedRegionEntry{0x00001010U, generated_step_b},
        GeneratedRegionEntry{0x00001020U, unresolved_generated_step},
        GeneratedRegionEntry{0x00001040U, generated_self_loop, 1U},
    };
    const std::array<std::uint32_t, 3U> roots{
        0x00001000U, 0x00001010U, 0x00001040U};
    const GeneratedExecutableBinding binding{identity, identity, 0x00001000U,
                                              0x00001100U, roots, entries};
    GeneratedDispatcher dispatcher;
    EXPECT(runner, dispatcher.bind(identity, binding) == DispatchStatus::Bound);

    clean_aot::runtime::GeneratedCpuState cpu{}; cpu.pc=0x00001000U;
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::Executed);
    EXPECT(runner, cpu.pc == 0x00001010U);
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::Executed);
    EXPECT(runner, cpu.pc == 0x00001030U);
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::UnknownCode);

    cpu.pc = 0x00001004U;
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::UnknownCode);
    cpu.pc = 0x00001020U;
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::UnknownCode);
    EXPECT(runner, cpu.pc == 0x00001020U);

    cpu.pc = 0x00001040U;
    EXPECT(runner, dispatcher.dispatch(cpu) == DispatchStatus::Executed);
    EXPECT(runner, cpu.pc == 0x00001040U);
    EXPECT(runner, cpu.gpr[8] == 1U);
    EXPECT(runner, dispatcher.last_instruction_count() == 1U);

    GeneratedDispatcher mismatch;
    EXPECT(runner, mismatch.bind(
                       "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
                       binding) == DispatchStatus::IdentityMismatch);
    EXPECT(runner, !mismatch.bound());

    const std::array unsorted{
        GeneratedRegionEntry{0x00001010U, generated_step_b},
        GeneratedRegionEntry{0x00001000U, generated_step_a},
    };
    const GeneratedExecutableBinding bad{identity, identity, 0x00001000U,
                                          0x00001100U, roots, unsorted};
    EXPECT(runner, mismatch.bind(identity, bad) == DispatchStatus::InvalidBinding);
}

void test_generated_integer_differential(Runner& runner) {
    using clean_aot::runtime::GeneratedCpuState; namespace ops=clean_aot::runtime::cpu_ops;
    r3000a::reference::Memory memory(0x1000U,0x100U);
    std::uint32_t random=0x6d2b79f5U; auto next=[&]{random^=random<<13U;random^=random>>17U;random^=random<<5U;return random;};
    for(unsigned i=0;i<2000U;++i){
        const auto a=next(),b=next(); GeneratedCpuState generated{}; generated.gpr[1]=a;generated.gpr[2]=b;
        r3000a::reference::Machine oracle(memory,{0x1000U,0U,r3000a::reference::kGenericR3000AProcessorId}); oracle.cpu().gpr[1]=a;oracle.cpu().gpr[2]=b;
        ops::addu(generated,3,1,2); (void)oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x21U);
        EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::slt(generated,3,1,2,false); (void)oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x2aU);
        EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::slt(generated,3,1,2,true); (void)oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x2bU);
        EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::bit_and(generated,3,1,0xffU); (void)oracle.step_word((0x0cU<<26U)|(1U<<21U)|(3U<<16U)|0xffU);
        EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::logic(generated,3,a,b,2U);(void)oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x26U);EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::shift(generated,3,b,a&31U,2U);(void)oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x07U);EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        generated.fault=clean_aot::runtime::CpuFault::None;generated.exception_raised=false;ops::sub(generated,3,a,b);const auto subtraction=oracle.step_word((1U<<21U)|(2U<<16U)|(3U<<11U)|0x22U);EXPECT(runner,generated.exception_raised==subtraction.exception.has_value());if(!generated.exception_raised)EXPECT(runner,generated.gpr[3]==oracle.cpu().gpr[3]);
        ops::mult(generated,a,b,false); (void)oracle.step_word((1U<<21U)|(2U<<16U)|0x18U);
        EXPECT(runner,generated.hi==oracle.cpu().hi); EXPECT(runner,generated.lo==oracle.cpu().lo);
        ops::div(generated,a,b,false); (void)oracle.step_word((1U<<21U)|(2U<<16U)|0x1aU);
        EXPECT(runner,generated.hi==oracle.cpu().hi); EXPECT(runner,generated.lo==oracle.cpu().lo);
    }
}

void test_generated_unaligned_memory_differential(Runner& runner) {
    using clean_aot::runtime::GeneratedCpuState; namespace ops=clean_aot::runtime::cpu_ops;
    constexpr std::uint32_t initial_word=0x44332211U,old_register=0xA1B2C3D4U,store_value=0x89ABCDEFU;
    for(std::uint32_t lane=0;lane<4U;++lane){
        for(const bool left:{false,true}){
            GeneratedMemory generated_memory{};GeneratedCpuState generated{};generated.memory_context=&generated_memory;generated.read32=generated_read32;generated.write32=generated_write32;generated.gpr[1]=0x1000U;generated.gpr[2]=old_register;(void)generated_write32(&generated_memory,0x1000U,initial_word);
            r3000a::reference::Memory oracle_memory(0x1000U,0x10U);EXPECT(runner,oracle_memory.store32le(0x1000U,initial_word));r3000a::reference::Machine oracle(oracle_memory,{0x2000U,0U,r3000a::reference::kGenericR3000AProcessorId});oracle.cpu().gpr[1]=0x1000U;oracle.cpu().gpr[2]=old_register;
            ops::begin(generated,0x2000U);if(left)ops::lwl(generated,1,2,static_cast<std::int16_t>(lane));else ops::lwr(generated,1,2,static_cast<std::int16_t>(lane));ops::end(generated);ops::begin(generated,0x2004U);ops::end(generated);
            const auto opcode=left?0x22U:0x26U;(void)oracle.step_word((opcode<<26U)|(1U<<21U)|(2U<<16U)|lane);(void)oracle.step_word(0U);
            EXPECT(runner,generated.gpr[2]==oracle.cpu().gpr[2]);
        }
        for(const bool left:{false,true}){
            GeneratedMemory generated_memory{};GeneratedCpuState generated{};generated.memory_context=&generated_memory;generated.read32=generated_read32;generated.write32=generated_write32;generated.gpr[1]=0x1000U;generated.gpr[2]=store_value;(void)generated_write32(&generated_memory,0x1000U,initial_word);
            r3000a::reference::Memory oracle_memory(0x1000U,0x10U);EXPECT(runner,oracle_memory.store32le(0x1000U,initial_word));r3000a::reference::Machine oracle(oracle_memory,{0x2000U,0U,r3000a::reference::kGenericR3000AProcessorId});oracle.cpu().gpr[1]=0x1000U;oracle.cpu().gpr[2]=store_value;
            ops::begin(generated,0x2000U);if(left)ops::swl(generated,1,2,static_cast<std::int16_t>(lane));else ops::swr(generated,1,2,static_cast<std::int16_t>(lane));ops::end(generated);
            const auto opcode=left?0x2AU:0x2EU;(void)oracle.step_word((opcode<<26U)|(1U<<21U)|(2U<<16U)|lane);std::uint32_t actual=0;EXPECT(runner,generated_read32(&generated_memory,0x1000U,actual));EXPECT(runner,oracle_memory.load32le(0x1000U).value_or(0U)==actual);
        }
    }
    GeneratedCpuState exception{};exception.cop0_status=clean_aot::runtime::kStatusBev|3U;exception.gpr[1]=0x1001U;ops::begin(exception,0x3004U,true,0x3000U);ops::lw(exception,1,2,0);ops::end(exception);
    EXPECT(runner,exception.exception_raised);EXPECT(runner,exception.exception_code==clean_aot::runtime::CpuException::AddressErrorLoad);EXPECT(runner,exception.cop0_epc==0x3000U);EXPECT(runner,(exception.cop0_cause&clean_aot::runtime::kCauseBd)!=0U);EXPECT(runner,exception.cop0_bad_vaddr==0x1001U);EXPECT(runner,exception.pc==clean_aot::runtime::kBootExceptionVector);EXPECT(runner,(exception.cop0_status&0x3FU)==12U);
    GeneratedCpuState bus_error{};bus_error.gpr[1]=0x1000U;bus_error.read32=rejected_read32;ops::begin(bus_error,0x4000U);ops::lw(bus_error,1,2,0);ops::end(bus_error);EXPECT(runner,bus_error.fault==clean_aot::runtime::CpuFault::None);EXPECT(runner,bus_error.exception_raised);EXPECT(runner,bus_error.exception_code==clean_aot::runtime::CpuException::BusErrorData);EXPECT(runner,bus_error.cop0_epc==0x4000U);
    GeneratedCpuState debug{};debug.gpr[1]=0x12345678U;for(const std::uint8_t reg:std::array<std::uint8_t,6U>{3U,5U,6U,7U,9U,11U}){ops::mtc0(debug,1,reg);EXPECT(runner,ops::read_cop0(debug,reg)==0U);ops::begin(debug,0x4800U);ops::end(debug);EXPECT(runner,ops::read_cop0(debug,reg)==0x12345678U);}EXPECT(runner,!debug.exception_raised);
    GeneratedCpuState deferred{};deferred.cop0_status=0x401U;deferred.cop0_cause=0x400U;deferred.gpr[1]=0U;ops::mtc0(deferred,1,12U);ops::begin(deferred,0x4900U);ops::sample_interrupt(deferred);EXPECT(runner,deferred.exception_raised);EXPECT(runner,deferred.cop0_epc==0x4900U);
    GeneratedCpuState missing_abi{};missing_abi.gpr[1]=0x1000U;ops::begin(missing_abi,0x5000U);ops::lw(missing_abi,1,2,0);ops::end(missing_abi);EXPECT(runner,missing_abi.fault==clean_aot::runtime::CpuFault::MemoryAccess);EXPECT(runner,!missing_abi.exception_raised);EXPECT(runner,missing_abi.pc==0U);
}

void test_generated_cop2_binding_and_latency(Runner&runner){using clean_aot::runtime::GeneratedCpuState;namespace ops=clean_aot::runtime::cpu_ops;clean_aot::gte::Gte generated_gte,oracle;GeneratedCpuState cpu{};cpu.cop0_status=1U<<30U;cpu.cop2_context=&generated_gte;cpu.cop2_read_data=cop2_read_data;cpu.cop2_read_control=cop2_read_control;cpu.cop2_write_data=cop2_write_data;cpu.cop2_write_control=cop2_write_control;cpu.cop2_execute=cop2_execute;auto transfer=[&](std::uint8_t reg,std::uint32_t value){cpu.gpr[1]=value;ops::mtc2(cpu,1,reg,false);EXPECT(runner,generated_gte.read_data(reg)!=value);ops::begin(cpu,0U);ops::end(cpu);EXPECT(runner,generated_gte.read_data(reg)==value);};transfer(9U,192U);oracle.write_data(9U,192U);transfer(10U,256U);oracle.write_data(10U,256U);transfer(11U,320U);oracle.write_data(11U,320U);constexpr std::uint32_t command=0x4a000028U;const auto receipt=oracle.execute(command);const auto command_cycle=cpu.issue_cycle;ops::cop2_command(cpu,command);EXPECT(runner,cpu.cop2_ready_cycle==command_cycle+receipt.cycles);ops::mfc2(cpu,2,9,false);EXPECT(runner,cpu.issue_cycle==cpu.cop2_ready_cycle);ops::end(cpu);ops::begin(cpu,4U);ops::end(cpu);EXPECT(runner,cpu.gpr[2]==oracle.read_data(9U));GeneratedCpuState disabled{};ops::begin(disabled,0x100U);ops::cop2_command(disabled,command);EXPECT(runner,disabled.exception_raised);EXPECT(runner,disabled.exception_code==clean_aot::runtime::CpuException::CoprocessorUnusable);EXPECT(runner,(disabled.cop0_cause&(3U<<28U))==(2U<<28U));}
void test_generated_cop2_memory_transfers(Runner&runner){using clean_aot::runtime::GeneratedCpuState;namespace ops=clean_aot::runtime::cpu_ops;clean_aot::gte::Gte gte;GeneratedMemory memory{};(void)generated_write32(&memory,0x1000U,0xDEADBEEFU);GeneratedCpuState cpu{};cpu.cop0_status=1U<<30U;cpu.gpr[1]=0x1000U;cpu.memory_context=&memory;cpu.read32=generated_read32;cpu.write32=generated_write32;cpu.cop2_context=&gte;cpu.cop2_read_data=cop2_read_data;cpu.cop2_write_data=cop2_write_data;ops::lwc2(cpu,1,4,0);EXPECT(runner,gte.read_data(4U)==0U);ops::begin(cpu,4U);ops::end(cpu);EXPECT(runner,gte.read_data(4U)==0xDEADBEEFU);ops::swc2(cpu,1,4,4);std::uint32_t stored=0;EXPECT(runner,generated_read32(&memory,0x1004U,stored));EXPECT(runner,stored==0xDEADBEEFU);}

void test_authenticated_load_and_first_dispatch(Runner& runner) {
    using clean_aot::runtime::DispatchStatus;
    using clean_aot::runtime::GeneratedExecutableBinding;
    using clean_aot::runtime::GeneratedRegionEntry;
    constexpr std::string_view identity =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    constexpr std::string_view executable_sha =
        "66840dda154e8a113c31dd0ad32f7f3a366a80e8136979d8f5a101d3d29d6f72";
    const std::array<std::uint8_t, 8U> executable{1U,2U,3U,4U,5U,6U,7U,8U};
    const std::array<std::uint32_t, 1U> roots{0x00001000U};
    const std::array entries{GeneratedRegionEntry{0x00001000U, generated_step_a}};
    const GeneratedExecutableBinding binding{identity, executable_sha, 0x00001000U,
                                              0x00001008U, roots, entries};
    auto runtime = make_runtime();
    auto mutated = executable; mutated[0] ^= 1U;
    EXPECT(runner, runtime->load_authenticated_executable(identity, mutated, 0x00001000U,
                                                           binding) ==
                       DispatchStatus::ExecutableHashMismatch);
    EXPECT(runner, runtime->load_authenticated_executable(identity, executable,
                                                           0x00001004U, binding) ==
                       DispatchStatus::UnknownCode);
    EXPECT(runner, runtime->load_authenticated_executable(identity, executable,
                                                           0x00001000U, binding) ==
                       DispatchStatus::Bound);
    EXPECT(runner, runtime->read32({}, 0x00001000U).value == 0x04030201U);
    EXPECT(runner, runtime->selected_pc() == 0x00001000U);
    EXPECT(runner, runtime->dispatch_selected(false) == DispatchStatus::Executed);
    EXPECT(runner, runtime->selected_pc() == 0x00001010U);
    EXPECT(runner, runtime->dispatch_selected(false) == DispatchStatus::UnknownCode);
}

void test_retirement_clock(Runner& runner) {
    using clean_aot::runtime::RetirementClock;
    using clean_aot::runtime::Status;

    RetirementClock nine_x(9U, 1U);
    EXPECT(runner, nine_x.configured());
    auto first = nine_x.advance_to(3'048'192U);
    EXPECT(runner, first.ok());
    EXPECT(runner, first.native_delta == 338'688U);
    EXPECT(runner, first.native_total == 338'688U);
    EXPECT(runner, first.phase == 0U);
    const auto nine_x_deadline =
        nine_x.deadline_for_native_cycle(338'689U);
    EXPECT(runner, nine_x_deadline.ok());
    EXPECT(runner, nine_x_deadline.retirement_cycle == 3'048'201U);
    EXPECT(runner, nine_x.retirement_total() == 3'048'192U);

    RetirementClock fractional(3U, 2U);
    std::array<std::uint64_t, 7U> expected{0U, 0U, 1U, 2U, 2U, 3U, 4U};
    for (std::uint64_t raw = 0U; raw < expected.size(); ++raw) {
        const auto advance = fractional.advance_to(raw);
        EXPECT(runner, advance.ok());
        EXPECT(runner, advance.native_total == expected[raw]);
    }
    const auto backwards = fractional.advance_to(5U);
    EXPECT(runner, backwards.status == Status::TimeReversal);
    EXPECT(runner, fractional.retirement_total() == 6U);
    EXPECT(runner, fractional.native_total() == 4U);
    const auto fractional_deadline =
        fractional.deadline_for_native_cycle(5U);
    EXPECT(runner, fractional_deadline.ok());
    EXPECT(runner, fractional_deadline.retirement_cycle == 8U);

    RetirementClock exact(1U, 1U);
    const auto maximum =
        exact.advance_to(std::numeric_limits<std::uint64_t>::max());
    EXPECT(runner, maximum.ok());
    EXPECT(runner,
           maximum.native_total == std::numeric_limits<std::uint64_t>::max());

    RetirementClock overflow(1U, std::numeric_limits<std::uint64_t>::max());
    EXPECT(runner, overflow.configured());
    EXPECT(runner, overflow.advance_to(1U).ok());
    const auto failed = overflow.advance_to(2U);
    EXPECT(runner, failed.status == Status::ArithmeticOverflow);
    EXPECT(runner, overflow.retirement_total() == 1U);
    EXPECT(runner,
           overflow.native_total() == std::numeric_limits<std::uint64_t>::max());

    RetirementClock invalid(11U, 1U);
    EXPECT(runner, !invalid.configured());
}

void test_configuration_and_plain_memory(Runner& runner) {
    auto runtime = make_runtime();
    EXPECT(runner, runtime->configured());

    const clean_aot::runtime::AccessContext late{900U, 0x8001'0000U, false};
    EXPECT(runner, runtime->write32(late, 0x8001'0000U, 0x1122'3344U).ok());
    EXPECT(runner, runtime->current_retirement_cycle() == 0U);
    EXPECT(runner, runtime->current_native_cycle() == 0U);
    const auto read = runtime->read32(late, 0x0001'0000U);
    EXPECT(runner, read.ok());
    EXPECT(runner, read.value == 0x1122'3344U);

    EXPECT(runner, runtime->write_masked32(
                       late, 0x8001'0000U, 0xaabb'ccddU, 0x5U)
                       .ok());
    const auto merged = runtime->read32(late, 0x8001'0000U);
    EXPECT(runner, merged.ok());
    EXPECT(runner, merged.value == 0x11bb'33ddU);
    EXPECT(runner, !runtime->write_masked32(
                        late, 0x8001'0002U, 0U, 1U)
                        .ok());

    static const std::array<std::uint8_t, 1U> short_bios{};
    clean_aot::runtime::Configuration bad{};
    bad.bios = short_bios;
    clean_aot::runtime::Runtime invalid(bad);
    EXPECT(runner, !invalid.configured());
}

void test_issue_cycle_and_mmio(Runner& runner) {
    using clean_aot::runtime::AccessContext;
    using clean_aot::runtime::AccessFault;

    auto runtime = make_runtime();
    EXPECT(runner, runtime->configured());

    const AccessContext at_zero{0U, 0xbfc0'0000U, false};
    EXPECT(runner, runtime->write16(at_zero, 0x1f80'1100U, 0U).ok());
    EXPECT(runner, runtime->write16(at_zero, 0x1f80'1104U, 0U).ok());

    const AccessContext at_nine_hundred{900U, 0x8001'1000U, false};
    const auto timer = runtime->read16(at_nine_hundred, 0x1f80'1100U);
    EXPECT(runner, timer.ok());
    EXPECT(runner, timer.value == 100U);
    EXPECT(runner, runtime->current_retirement_cycle() == 900U);
    EXPECT(runner, runtime->current_native_cycle() == 100U);

    const AccessContext backwards{899U, 0x8001'1004U, false};
    const auto rejected = runtime->read16(backwards, 0x1f80'1100U);
    EXPECT(runner, rejected.fault == AccessFault::TimeReversal);
    EXPECT(runner, runtime->current_retirement_cycle() == 900U);
    EXPECT(runner, runtime->current_native_cycle() == 100U);

    EXPECT(runner,
           runtime->write32(at_nine_hundred, 0xfffe'0130U, 0x0000'0804U)
               .ok());
    const auto cache = runtime->read32(at_nine_hundred, 0xfffe'0130U);
    EXPECT(runner, cache.ok());
    EXPECT(runner, cache.value == 0x0000'0804U);

    EXPECT(runner,
           runtime->write16(at_nine_hundred, 0x1f80'1060U, 0x0b88U).ok());
    const auto ram_size = runtime->read32(at_nine_hundred, 0x1f80'1060U);
    EXPECT(runner, ram_size.ok());
    EXPECT(runner, (ram_size.value & 0xffffU) == 0x0b88U);
    EXPECT(runner, runtime->write32(at_nine_hundred, 0x0020'0000U,
                                    0x5566'7788U).ok());
    EXPECT(runner, runtime->read32(at_nine_hundred, 0U).value ==
                       0x5566'7788U);
    EXPECT(runner, runtime->write16(at_nine_hundred, 0x1f80'1060U,
                                    0x0888U).ok());
    EXPECT(runner, !runtime->read32(at_nine_hundred, 0x0020'0000U).ok());
    EXPECT(runner, runtime->read32(at_nine_hundred, 0U).value ==
                       0x5566'7788U);

    const auto bad_gpu = runtime->write16(
        at_nine_hundred, 0x1f80'1810U, 0U);
    EXPECT(runner, bad_gpu.fault == AccessFault::BusError);
    const auto unknown = runtime->read32(
        at_nine_hundred, 0x1f80'1900U);
    EXPECT(runner, unknown.fault == AccessFault::BusError);
}

void test_interrupt_latch(Runner& runner) {
    using clean_aot::runtime::AccessContext;
    auto runtime = make_runtime(1U, 1U);
    EXPECT(runner, runtime->configured());
    const AccessContext context{10U, 0x8001'0000U, false};
    const std::uint32_t vblank = clean_aot::soc::interrupt_bit(
        clean_aot::soc::InterruptSource::vblank);
    EXPECT(runner,
           runtime->write32(context, 0x1f80'1074U, vblank).ok());
    runtime->soc().interrupts().raise(clean_aot::soc::InterruptSource::vblank);
    const auto status = runtime->read32(context, 0x1f80'1070U);
    EXPECT(runner, status.ok());
    EXPECT(runner, (status.value & vblank) != 0U);
    EXPECT(runner, runtime->cpu_interrupt_line());
    EXPECT(runner, runtime->hardware_interrupt_lines() == 1U);

    EXPECT(runner, runtime->write32(
                       context, 0x1f80'1070U, ~vblank)
                       .ok());
    EXPECT(runner, !runtime->cpu_interrupt_line());
    EXPECT(runner, runtime->hardware_interrupt_lines() == 0U);

    auto timer_runtime = make_runtime(1U, 1U);
    const AccessContext at_zero{0U, 0x8001'0100U, false};
    const std::uint32_t counter0 = clean_aot::soc::interrupt_bit(
        clean_aot::soc::InterruptSource::counter0);
    const std::uint32_t graphics = clean_aot::soc::interrupt_bit(
        clean_aot::soc::InterruptSource::graphics);
    const std::uint32_t dma = clean_aot::soc::interrupt_bit(
        clean_aot::soc::InterruptSource::dma);
    const std::uint32_t simultaneous = counter0 | graphics | dma;
    EXPECT(runner,
           timer_runtime->write32(at_zero, 0x1f80'1074U, simultaneous).ok());
    EXPECT(runner,
           timer_runtime->write32(at_zero, 0x1f80'1810U, 0x1f00'0000U).ok());
    timer_runtime->soc().dma().write_interrupt_control(1U << 15U);
    EXPECT(runner,
           timer_runtime->write16(at_zero, 0x1f80'1108U, 1U).ok());
    EXPECT(runner,
           timer_runtime->write16(at_zero, 0x1f80'1104U, 0x00d8U).ok());

    const AccessContext first_target{1U, 0x8001'0104U, false};
    const auto simultaneous_status =
        timer_runtime->read32(first_target, 0x1f80'1070U);
    EXPECT(runner, simultaneous_status.ok());
    EXPECT(runner, (simultaneous_status.value & simultaneous) == simultaneous);
    EXPECT(runner, timer_runtime->cpu_interrupt_line());
    EXPECT(runner, timer_runtime->write32(
                       first_target, 0x1f80'1070U, ~simultaneous)
                       .ok());
    EXPECT(runner, timer_runtime->cpu_interrupt_line());
    EXPECT(runner,
           (timer_runtime->soc().interrupts().status() & simultaneous) ==
               simultaneous);

    EXPECT(runner,
           timer_runtime->write32(first_target, 0x1f80'1814U, 0x0200'0000U)
               .ok());
    timer_runtime->soc().dma().write_interrupt_control(0U);

    const AccessContext second_target{2U, 0x8001'0108U, false};
    EXPECT(runner,
           timer_runtime->read32(second_target, 0x1f80'1070U).ok());
    EXPECT(runner, timer_runtime->write32(
                       second_target, 0x1f80'1070U, ~simultaneous)
                       .ok());
    EXPECT(runner, !timer_runtime->cpu_interrupt_line());
}

void test_device_deadlines(Runner& runner) {
    using clean_aot::runtime::AccessContext;
    using clean_aot::runtime::DeadlineSource;
    using clean_aot::runtime::Status;

    auto runtime = make_runtime();
    const AccessContext at_zero{0U, 0x8001'0000U, false};
    EXPECT(runner, runtime->write16(at_zero, 0x1f80'1108U, 10U).ok());
    EXPECT(runner, runtime->write16(at_zero, 0x1f80'1104U, 0x0010U).ok());
    const auto timer_deadline = runtime->next_device_deadline();
    EXPECT(runner, timer_deadline.ok());
    EXPECT(runner, timer_deadline.pending);
    EXPECT(runner, timer_deadline.source == DeadlineSource::RootCounter);
    EXPECT(runner, timer_deadline.native_cycle == 10U);
    EXPECT(runner, timer_deadline.raw_cpu_cycle == 90U);

    const auto missed = runtime->advance_to_retirement_cycle(91U);
    EXPECT(runner, missed.status == Status::MissedDeadline);
    EXPECT(runner, runtime->current_retirement_cycle() == 0U);
    const auto exact = runtime->advance_to_retirement_cycle(90U);
    EXPECT(runner, exact.ok());
    EXPECT(runner, runtime->soc().interrupts().pending(
                       clean_aot::soc::InterruptSource::counter0));
    EXPECT(runner, !runtime->next_device_deadline().pending);

    auto cd_runtime = make_runtime();
    EXPECT(runner, cd_runtime->write8(at_zero, 0x1f80'1801U, 0x01U).ok());
    const auto cd_deadline = cd_runtime->next_device_deadline();
    EXPECT(runner, cd_deadline.ok());
    EXPECT(runner, cd_deadline.pending);
    EXPECT(runner, cd_deadline.source == DeadlineSource::Cdrom);
    EXPECT(runner, cd_deadline.native_cycle == 33'868U);
    EXPECT(runner, cd_deadline.raw_cpu_cycle == 304'812U);
    EXPECT(runner,
           cd_runtime->advance_to_retirement_cycle(cd_deadline.raw_cpu_cycle)
               .ok());
}

void test_reset(Runner& runner) {
    auto runtime = make_runtime();
    const clean_aot::runtime::AccessContext context{90U, 0U, false};
    EXPECT(runner, runtime->write32(context, 0x8000'0100U, 0xfeed'faceU).ok());
    EXPECT(runner, runtime->read32(context, 0x1f80'1070U).ok());
    EXPECT(runner, runtime->current_native_cycle() == 10U);
    EXPECT(runner, runtime->reset() == clean_aot::runtime::Status::Ok);
    EXPECT(runner, runtime->current_native_cycle() == 0U);
    const auto cleared = runtime->read32(
        clean_aot::runtime::AccessContext{}, 0x8000'0100U);
    EXPECT(runner, cleared.ok());
    EXPECT(runner, cleared.value == 0U);
}

void test_exact_audio_clock(Runner& runner) {
    auto runtime = make_runtime();
    const auto frame = runtime->advance_to_retirement_cycle(3'048'192U);
    EXPECT(runner, frame.ok());
    EXPECT(runner, frame.native_total == 338'688U);
    EXPECT(runner, runtime->produced_audio_samples() == 441U);
    EXPECT(runner, runtime->queued_audio_samples() == 441U);
    std::array<clean_aot::runtime::StereoSample, 441U> samples{};
    EXPECT(runner, runtime->drain_audio(samples) == samples.size());
    EXPECT(runner, runtime->queued_audio_samples() == 0U);
    for (const auto sample : samples) {
        EXPECT(runner, sample == clean_aot::runtime::StereoSample{});
    }
}

void test_spu_irq_sample_deadline(Runner& runner) {
    auto runtime = make_runtime();
    const clean_aot::runtime::AccessContext context{};
    EXPECT(runner,
           runtime->write16(context, 0x1f80'1daaU, 0x0040U).ok());
    const auto deadline = runtime->next_device_deadline();
    EXPECT(runner, deadline.ok());
    EXPECT(runner, deadline.pending);
    EXPECT(runner,
           deadline.source == clean_aot::runtime::DeadlineSource::Spu);
    EXPECT(runner, deadline.native_cycle == 768U);
    EXPECT(runner, deadline.raw_cpu_cycle == 6'912U);
    EXPECT(runner,
           runtime->advance_to_retirement_cycle(deadline.raw_cpu_cycle).ok());
    EXPECT(runner, runtime->produced_audio_samples() == 1U);
}

} // namespace

int main() {
    Runner runner;
    test_authenticated_generated_dispatch(runner);
    test_authenticated_bios_reset_contract(runner);
    test_generated_cache_isolation(runner);
    test_authenticated_copied_epoch(runner);
    test_overlap_binding_refreshes_unchanged_exact_epoch(runner);
    test_generated_integer_differential(runner);
    test_generated_unaligned_memory_differential(runner);
    test_generated_cop2_binding_and_latency(runner);
    test_generated_cop2_memory_transfers(runner);
    test_authenticated_load_and_first_dispatch(runner);
    test_retirement_clock(runner);
    test_configuration_and_plain_memory(runner);
    test_issue_cycle_and_mmio(runner);
    test_interrupt_latch(runner);
    test_device_deadlines(runner);
    test_reset(runner);
    test_exact_audio_clock(runner);
    test_spu_irq_sample_deadline(runner);
    return runner.result();
}
