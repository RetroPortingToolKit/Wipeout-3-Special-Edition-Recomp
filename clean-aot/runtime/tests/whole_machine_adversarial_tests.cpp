#include "clean_aot_soc.hpp"
#include "machine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

using clean_aot::machine::Event;
using clean_aot::machine::FrameReceipt;
using clean_aot::machine::Machine;
using clean_aot::machine::VBlankRate;
using clean_aot::soc::CycleDelta;
using clean_aot::soc::DmaBusCallbacks;
using clean_aot::soc::DmaCallbackStatus;
using clean_aot::soc::DmaController;
using clean_aot::soc::DmaDeviceCallbacks;
using clean_aot::soc::DmaRunBudget;
using clean_aot::soc::InterruptSource;
using clean_aot::soc::SocSnapshot;
using clean_aot::soc::SocStatus;
using clean_aot::soc::SystemOnChip;

constexpr std::uint64_t kFrames = 3'600U;
constexpr std::uint64_t kNativeCyclesPerFrame = 338'688U;
constexpr std::uint64_t kRetirementMultiplier = 9U;
constexpr std::uint64_t kRetirementCyclesPerFrame =
    kNativeCyclesPerFrame * kRetirementMultiplier;
constexpr std::uint32_t kDmaWordsPerFrame = 4U;

class Runner final {
public:
    void expect(const bool condition, const char* const expression,
                const int line) noexcept {
        ++checks_;
        if (!condition) {
            ++failures_;
            std::cerr << "line " << line << ": " << expression << '\n';
        }
    }

    [[nodiscard]] int result() const noexcept {
        if (failures_ == 0U) {
            std::cout << "checks=" << checks_ << " failures=0\n";
            return EXIT_SUCCESS;
        }
        return EXIT_FAILURE;
    }

private:
    std::uint64_t checks_ = 0U;
    std::uint64_t failures_ = 0U;
};

#define EXPECT(runner, expression) \
    (runner).expect(static_cast<bool>(expression), #expression, __LINE__)

struct DmaEndpoints {
    std::array<std::uint32_t, kDmaWordsPerFrame> words{};
    std::uint32_t frame = 0U;
    std::uint32_t reads = 0U;
    std::uint64_t device_words = 0U;
    std::uint64_t checksum = 0U;
};

DmaCallbackStatus read_bus(void* const context, const std::uint32_t address,
                           std::uint32_t* const value) noexcept {
    auto* const endpoints = static_cast<DmaEndpoints*>(context);
    if (endpoints == nullptr || value == nullptr || (address & 3U) != 0U ||
        endpoints->reads >= endpoints->words.size()) {
        return DmaCallbackStatus::malformed;
    }
    *value = endpoints->words[endpoints->reads++];
    return DmaCallbackStatus::ok;
}

DmaCallbackStatus write_device(void* const context,
                               const std::uint32_t value) noexcept {
    auto* const endpoints = static_cast<DmaEndpoints*>(context);
    if (endpoints == nullptr) {
        return DmaCallbackStatus::malformed;
    }
    ++endpoints->device_words;
    endpoints->checksum =
        (endpoints->checksum * 1'099'511'628'211ULL) ^ value;
    return DmaCallbackStatus::ok;
}

std::uint32_t dma_control() noexcept {
    return 1U | (1U << 24U) | (1U << 28U);
}

bool equal_counter(const clean_aot::soc::CounterSnapshot& left,
                   const clean_aot::soc::CounterSnapshot& right) noexcept {
    return left.count == right.count && left.target == right.target &&
           left.mode == right.mode && left.running == right.running &&
           left.synchronization_level == right.synchronization_level &&
           left.synchronization_released == right.synchronization_released &&
           left.irq_armed == right.irq_armed &&
           left.phase_numerator == right.phase_numerator &&
           left.phase_denominator == right.phase_denominator &&
           left.event_ordinal == right.event_ordinal;
}

bool equal_dma_channel(const clean_aot::soc::DmaChannelSnapshot& left,
                       const clean_aot::soc::DmaChannelSnapshot& right) {
    return left.registers.base_address == right.registers.base_address &&
           left.registers.block_control == right.registers.block_control &&
           left.registers.channel_control == right.registers.channel_control &&
           left.active == right.active && left.completed == right.completed &&
           left.error == right.error && left.last_status == right.last_status &&
           left.current_address == right.current_address &&
           left.words_transferred == right.words_transferred &&
           left.words_remaining == right.words_remaining &&
           left.linked_current == right.linked_current &&
           left.linked_next == right.linked_next &&
           left.linked_nodes == right.linked_nodes &&
           left.linked_words == right.linked_words &&
           left.burst_words == right.burst_words &&
           left.block_words_remaining == right.block_words_remaining &&
           left.request_blocks_remaining == right.request_blocks_remaining &&
           left.cpu_window_remaining == right.cpu_window_remaining &&
           left.window_active == right.window_active &&
           left.request_asserted == right.request_asserted &&
           left.visited_nodes == right.visited_nodes;
}

class WholeMachine final {
public:
    explicit WholeMachine(Runner& runner)
        : runner_(runner), machine_(VBlankRate::Hz100, 9U, 1U) {
        EXPECT(runner_, soc_.dma().set_callbacks(
                            0U,
                            DmaBusCallbacks{&endpoints_, read_bus, nullptr},
                            DmaDeviceCallbacks{&endpoints_, nullptr,
                                               write_device}) == SocStatus::ok);
        soc_.dma().write_priority_control(
            (soc_.dma().read_priority_control() & ~0xfU) | 8U);
        soc_.dma().write_interrupt_control((1U << 23U) | (1U << 16U));
    }

    void step() {
        prepare_dma();
        receipt_ = machine_.step_frame(&on_event, this);
        const auto& receipt = receipt_;
        EXPECT(runner_, !event_failed_);
        EXPECT(runner_, receipt.cpu_cycles == kRetirementCyclesPerFrame);
        EXPECT(runner_, receipt.spu_samples == 441U);
        EXPECT(runner_, receipt.cdda_sectors ==
                            (((receipt.frame_index + 1U) * 3U) / 4U) -
                                ((receipt.frame_index * 3U) / 4U));
        EXPECT(runner_, receipt.elapsed_timebase_ticks ==
                            receipt.frame_index + 1U);
    }

    [[nodiscard]] const Machine& machine() const noexcept { return machine_; }
    [[nodiscard]] const FrameReceipt& receipt() const noexcept {
        return receipt_;
    }
    [[nodiscard]] SystemOnChip& soc() noexcept { return soc_; }
    [[nodiscard]] const SystemOnChip& soc() const noexcept { return soc_; }
    [[nodiscard]] const DmaEndpoints& endpoints() const noexcept {
        return endpoints_;
    }
    [[nodiscard]] std::uint64_t native_cycles() const noexcept {
        return native_cycles_;
    }
    [[nodiscard]] std::uint64_t dma_irqs() const noexcept { return dma_irqs_; }
    [[nodiscard]] std::uint64_t vblank_irqs() const noexcept {
        return vblank_irqs_;
    }

private:
    void prepare_dma() {
        endpoints_.reads = 0U;
        endpoints_.frame = static_cast<std::uint32_t>(machine_.frame_count());
        for (std::size_t index = 0U; index < endpoints_.words.size(); ++index) {
            endpoints_.words[index] =
                (endpoints_.frame << 8U) | static_cast<std::uint32_t>(index);
        }
        EXPECT(runner_, soc_.dma().write_base_address(0U, 0x1000U) ==
                            SocStatus::ok);
        EXPECT(runner_, soc_.dma().write_block_control(
                            0U, kDmaWordsPerFrame) == SocStatus::ok);
        EXPECT(runner_, soc_.dma().write_channel_control(0U, dma_control()) ==
                            SocStatus::ok);
    }

    static void on_event(const Event& event, void* const context) {
        static_cast<WholeMachine*>(context)->consume(event);
    }

    void consume(const Event& event) noexcept {
        if (event.kind == CLEAN_AOT_MACHINE_EVENT_VBLANK) {
            const std::uint64_t native_delta =
                event.auxiliary * kNativeCyclesPerFrame;
            if (event.auxiliary != 1U ||
                native_cycles_ >
                    std::numeric_limits<std::uint64_t>::max() - native_delta) {
                event_failed_ = true;
                return;
            }
            native_cycles_ += native_delta;
            if (soc_.counters().advance_cpu(CycleDelta::whole(native_delta),
                                            nullptr) != SocStatus::ok) {
                event_failed_ = true;
                return;
            }
            soc_.interrupts().raise(InterruptSource::vblank);
            if (!soc_.interrupts().pending(InterruptSource::vblank)) {
                event_failed_ = true;
                return;
            }
            ++vblank_irqs_;
            soc_.interrupts().acknowledge(InterruptSource::vblank);

            const auto dma_report = soc_.dma().run(
                DmaRunBudget{kDmaWordsPerFrame, kDmaWordsPerFrame});
            if (dma_report.status != SocStatus::ok ||
                dma_report.words_transferred != kDmaWordsPerFrame ||
                dma_report.cpu_cycles != kDmaWordsPerFrame ||
                !soc_.interrupts().pending(InterruptSource::dma)) {
                event_failed_ = true;
                return;
            }
            ++dma_irqs_;
            soc_.dma().acknowledge_interrupts(1U);
            soc_.interrupts().acknowledge(InterruptSource::dma);
        } else if (event.kind == CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES &&
                   (event.amount != kRetirementCyclesPerFrame ||
                    event.cpu_retirement_total != event.cumulative)) {
            event_failed_ = true;
        }
    }

    Runner& runner_;
    Machine machine_;
    SystemOnChip soc_{};
    DmaEndpoints endpoints_{};
    FrameReceipt receipt_{};
    std::uint64_t native_cycles_ = 0U;
    std::uint64_t dma_irqs_ = 0U;
    std::uint64_t vblank_irqs_ = 0U;
    bool event_failed_ = false;
};

void expect_same_soc(Runner& runner, const SocSnapshot& left,
                     const SocSnapshot& right) {
    EXPECT(runner, left.interrupts.status == right.interrupts.status);
    EXPECT(runner, left.interrupts.mask == right.interrupts.mask);
    EXPECT(runner, left.dma.priority_control == right.dma.priority_control);
    EXPECT(runner, left.dma.interrupt_control == right.dma.interrupt_control);
    EXPECT(runner, left.dma.interrupt_flags == right.dma.interrupt_flags);
    EXPECT(runner, left.dma.master_irq_line == right.dma.master_irq_line);
    for (std::size_t index = 0U; index < left.dma.channels.size(); ++index) {
        EXPECT(runner, equal_dma_channel(left.dma.channels[index],
                                         right.dma.channels[index]));
    }
    for (std::size_t index = 0U; index < left.counters.size(); ++index) {
        EXPECT(runner, equal_counter(left.counters[index], right.counters[index]));
    }
}

void test_whole_machine_exact_replay(Runner& runner) {
    WholeMachine baseline(runner);
    for (std::uint64_t frame = 0U; frame < kFrames / 2U; ++frame) {
        baseline.step();
    }
    const SocSnapshot midpoint = baseline.soc().snapshot();
    EXPECT(runner, baseline.soc().validate_snapshot(midpoint) == SocStatus::ok);

    WholeMachine replay(runner);
    for (std::uint64_t frame = 0U; frame < kFrames / 2U; ++frame) {
        replay.step();
    }
    replay.soc().reset();
    EXPECT(runner, replay.soc().restore(midpoint) == SocStatus::ok);
    expect_same_soc(runner, midpoint, replay.soc().snapshot());

    for (std::uint64_t frame = kFrames / 2U; frame < kFrames; ++frame) {
        baseline.step();
        replay.step();
    }

    constexpr std::uint64_t expected_native = kFrames * kNativeCyclesPerFrame;
    constexpr std::uint64_t expected_retirement =
        kFrames * kRetirementCyclesPerFrame;
    constexpr std::uint64_t expected_spu = kFrames * 441U;
    constexpr std::uint64_t expected_cdda = kFrames * 3U / 4U;
    constexpr std::uint64_t expected_dma_words =
        kFrames * kDmaWordsPerFrame;

    for (const WholeMachine* const candidate : {&baseline, &replay}) {
        EXPECT(runner, candidate->machine().frame_count() == kFrames);
        EXPECT(runner, candidate->native_cycles() == expected_native);
        EXPECT(runner, candidate->machine().receipt_digest() ==
                            baseline.machine().receipt_digest());
        EXPECT(runner,
               candidate->receipt().total_cpu_cycles == expected_retirement);
        EXPECT(runner, candidate->receipt().total_spu_samples == expected_spu);
        EXPECT(runner,
               candidate->receipt().total_cdda_sectors == expected_cdda);
        EXPECT(runner, candidate->endpoints().device_words ==
                            expected_dma_words);
        EXPECT(runner, candidate->dma_irqs() == kFrames);
        EXPECT(runner, candidate->vblank_irqs() == kFrames);
    }
    EXPECT(runner,
           baseline.receipt().total_cpu_cycles == expected_retirement);
    EXPECT(runner, baseline.receipt().total_spu_samples == expected_spu);
    EXPECT(runner, baseline.receipt().total_cdda_sectors == expected_cdda);
    EXPECT(runner, baseline.endpoints().checksum == replay.endpoints().checksum);
    expect_same_soc(runner, baseline.soc().snapshot(), replay.soc().snapshot());
}

} // namespace

int main() {
    Runner runner;
    test_whole_machine_exact_replay(runner);
    return runner.result();
}
