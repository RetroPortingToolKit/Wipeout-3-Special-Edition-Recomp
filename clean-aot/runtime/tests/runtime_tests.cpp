#include "clean_aot/runtime/runtime.hpp"
#include "clean_aot/runtime/native_dispatch.hpp"
#include "clean_aot/runtime/generated_cpu.hpp"
#include "r3000a/reference.hpp"

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

bool generated_step_a(std::uint32_t& pc, bool branch_taken) noexcept {
    pc = branch_taken ? 0x00001020U : 0x00001010U;
    return true;
}

bool generated_step_b(std::uint32_t& pc, bool) noexcept {
    pc = 0x00001030U;
    return true;
}

bool unresolved_generated_step(std::uint32_t&, bool) noexcept {
    return false;
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
    };
    const std::array<std::uint32_t, 2U> roots{0x00001000U, 0x00001010U};
    const GeneratedExecutableBinding binding{identity, identity, 0x00001000U,
                                              0x00001100U, roots, entries};
    GeneratedDispatcher dispatcher;
    EXPECT(runner, dispatcher.bind(identity, binding) == DispatchStatus::Bound);

    std::uint32_t pc = 0x00001000U;
    EXPECT(runner, dispatcher.dispatch(pc, false) == DispatchStatus::Executed);
    EXPECT(runner, pc == 0x00001010U);
    EXPECT(runner, dispatcher.dispatch(pc, false) == DispatchStatus::Executed);
    EXPECT(runner, pc == 0x00001030U);
    EXPECT(runner, dispatcher.dispatch(pc, false) == DispatchStatus::UnknownCode);

    pc = 0x00001004U;
    EXPECT(runner, dispatcher.dispatch(pc, false) == DispatchStatus::UnknownCode);
    pc = 0x00001020U;
    EXPECT(runner, dispatcher.dispatch(pc, false) == DispatchStatus::UnknownCode);
    EXPECT(runner, pc == 0x00001020U);

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
        ops::mult(generated,a,b,false); (void)oracle.step_word((1U<<21U)|(2U<<16U)|0x18U);
        EXPECT(runner,generated.hi==oracle.cpu().hi); EXPECT(runner,generated.lo==oracle.cpu().lo);
        ops::div(generated,a,b,false); (void)oracle.step_word((1U<<21U)|(2U<<16U)|0x1aU);
        EXPECT(runner,generated.hi==oracle.cpu().hi); EXPECT(runner,generated.lo==oracle.cpu().lo);
    }
}

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
    test_generated_integer_differential(runner);
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
