#include "clean_aot_soc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using clean_aot::soc::CounterCallbacks;
using clean_aot::soc::CounterEdge;
using clean_aot::soc::CounterEvent;
using clean_aot::soc::CounterEventKind;
using clean_aot::soc::CounterSnapshot;
using clean_aot::soc::CycleDelta;
using clean_aot::soc::DmaBusCallbacks;
using clean_aot::soc::DmaCallbackStatus;
using clean_aot::soc::DmaController;
using clean_aot::soc::DmaDeviceCallbacks;
using clean_aot::soc::DmaRunBudget;
using clean_aot::soc::InterruptController;
using clean_aot::soc::InterruptSource;
using clean_aot::soc::RootCounters;
using clean_aot::soc::SocSnapshot;
using clean_aot::soc::SocStatus;
using clean_aot::soc::SystemOnChip;

struct TestRunner {
    std::size_t checks = 0U;
    std::size_t failures = 0U;

    void expect(const bool condition, const char* const expression,
                const int line) noexcept {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "line " << line << ": " << expression << '\n';
        }
    }
};

#define EXPECT(runner, expression)                                             \
    (runner).expect((expression), #expression, __LINE__)

struct CounterLog {
    std::array<CounterEvent, 128> events{};
    std::size_t count = 0U;
};

void record_counter_event(void* const context,
                          const CounterEvent& event) noexcept {
    auto* const log = static_cast<CounterLog*>(context);
    if (log != nullptr && log->count < log->events.size()) {
        log->events[log->count] = event;
        ++log->count;
    }
}

void discard_counter_event(void* const /*context*/,
                           const CounterEvent& /*event*/) noexcept {
}

std::uint32_t next_random(std::uint32_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

struct Memory {
    std::array<std::uint32_t, 1024> words{};
    std::array<std::uint32_t, 128> read_addresses{};
    std::array<std::uint32_t, 128> write_addresses{};
    std::size_t reads = 0U;
    std::size_t writes = 0U;
    bool reject = false;
    bool malformed = false;
};

std::size_t memory_index(const std::uint32_t address) noexcept {
    return static_cast<std::size_t>((address >> 2U) %
                                    static_cast<std::uint32_t>(1024U));
}

DmaCallbackStatus read_memory(void* const context, const std::uint32_t address,
                              std::uint32_t* const value) noexcept {
    auto* const memory = static_cast<Memory*>(context);
    if (memory == nullptr || value == nullptr || (address & 3U) != 0U) {
        return DmaCallbackStatus::malformed;
    }
    if (memory->malformed) {
        return DmaCallbackStatus::malformed;
    }
    if (memory->reject) {
        return DmaCallbackStatus::rejected;
    }
    if (memory->reads < memory->read_addresses.size()) {
        memory->read_addresses[memory->reads] = address;
    }
    ++memory->reads;
    *value = memory->words[memory_index(address)];
    return DmaCallbackStatus::ok;
}

DmaCallbackStatus write_memory(void* const context, const std::uint32_t address,
                               const std::uint32_t value) noexcept {
    auto* const memory = static_cast<Memory*>(context);
    if (memory == nullptr || (address & 3U) != 0U) {
        return DmaCallbackStatus::malformed;
    }
    if (memory->malformed) {
        return DmaCallbackStatus::malformed;
    }
    if (memory->reject) {
        return DmaCallbackStatus::rejected;
    }
    if (memory->writes < memory->write_addresses.size()) {
        memory->write_addresses[memory->writes] = address;
    }
    ++memory->writes;
    memory->words[memory_index(address)] = value;
    return DmaCallbackStatus::ok;
}

struct Device {
    std::array<std::uint32_t, 128> values{};
    std::size_t count = 0U;
    std::array<std::uint32_t, 128> input{};
    std::size_t input_count = 0U;
    std::size_t input_cursor = 0U;
    bool reject = false;
    bool malformed = false;
    bool not_ready = false;
};

DmaCallbackStatus read_device(void* const context,
                              std::uint32_t* const value) noexcept {
    auto* const device = static_cast<Device*>(context);
    if (device == nullptr || value == nullptr) {
        return DmaCallbackStatus::malformed;
    }
    if (device->malformed) {
        return DmaCallbackStatus::malformed;
    }
    if (device->not_ready) {
        return DmaCallbackStatus::not_ready;
    }
    if (device->reject || device->input_cursor >= device->input_count) {
        return DmaCallbackStatus::rejected;
    }
    *value = device->input[device->input_cursor];
    ++device->input_cursor;
    return DmaCallbackStatus::ok;
}

DmaCallbackStatus write_device(void* const context,
                               const std::uint32_t value) noexcept {
    auto* const device = static_cast<Device*>(context);
    if (device == nullptr) {
        return DmaCallbackStatus::malformed;
    }
    if (device->malformed) {
        return DmaCallbackStatus::malformed;
    }
    if (device->not_ready) {
        return DmaCallbackStatus::not_ready;
    }
    if (device->reject || device->count >= device->values.size()) {
        return DmaCallbackStatus::rejected;
    }
    device->values[device->count] = value;
    ++device->count;
    return DmaCallbackStatus::ok;
}

DmaBusCallbacks bus_for(Memory& memory) noexcept {
    return DmaBusCallbacks{&memory, read_memory, write_memory};
}

DmaDeviceCallbacks device_for(Device& device) noexcept {
    return DmaDeviceCallbacks{&device, read_device, write_device};
}

enum class ReentrantAction { reset, rewrite, run };

struct ReentrantMemory {
    DmaController* dma = nullptr;
    ReentrantAction action = ReentrantAction::reset;
    std::uint32_t value = 0;
    SocStatus nested_status = SocStatus::idle;
};

DmaCallbackStatus read_reentrant_memory(void* const context,
                                        const std::uint32_t /*address*/,
                                        std::uint32_t* const value) noexcept {
    auto* const memory = static_cast<ReentrantMemory*>(context);
    if (memory == nullptr || memory->dma == nullptr || value == nullptr) {
        return DmaCallbackStatus::malformed;
    }
    *value = memory->value;
    switch (memory->action) {
    case ReentrantAction::reset:
        memory->dma->reset();
        break;
    case ReentrantAction::rewrite:
        memory->nested_status = memory->dma->write_block_control(0U, 7U);
        break;
    case ReentrantAction::run:
        memory->nested_status =
            memory->dma->run(DmaRunBudget{1U, 1U}).status;
        break;
    }
    return DmaCallbackStatus::ok;
}

DmaBusCallbacks bus_for(ReentrantMemory& memory) noexcept {
    return DmaBusCallbacks{&memory, read_reentrant_memory, nullptr};
}

void enable_dma_interrupt(DmaController& dma,
                          const std::size_t channel) noexcept {
    const std::uint32_t channel_bit = std::uint32_t{1}
                                      << static_cast<std::uint32_t>(channel);
    dma.write_interrupt_control((1U << 23U) | (channel_bit << 16U));
}

void enable_dma_slice_interrupt(DmaController& dma,
                                const std::size_t channel) noexcept {
    const std::uint32_t channel_bit = std::uint32_t{1}
                                      << static_cast<std::uint32_t>(channel);
    dma.write_interrupt_control(channel_bit | (channel_bit << 16U) |
                                (1U << 23U));
}

void set_priority(DmaController& dma, const std::size_t channel,
                  const std::uint32_t priority, const bool enabled) noexcept {
    std::uint32_t control = dma.read_priority_control();
    const std::uint32_t shift = static_cast<std::uint32_t>(channel * 4U);
    control &= ~(0xfU << shift);
    control |= ((priority & 7U) | (enabled ? 8U : 0U)) << shift;
    dma.write_priority_control(control);
}

std::uint32_t linear_control(const bool to_device, const bool decrement,
                             const bool chop, const std::uint32_t sync,
                             const bool busy = true,
                             const bool force = true) noexcept {
    return (to_device ? 1U : 0U) | (decrement ? (1U << 1U) : 0U) |
           (chop ? (1U << 8U) : 0U) | ((sync & 3U) << 9U) |
           (busy ? (1U << 24U) : 0U) | (force ? (1U << 28U) : 0U);
}

bool equal_counter_snapshot(const CounterSnapshot& left,
                            const CounterSnapshot& right) noexcept {
    return left.count == right.count && left.target == right.target &&
           left.mode == right.mode && left.running == right.running &&
           left.synchronization_level == right.synchronization_level &&
           left.synchronization_released == right.synchronization_released &&
           left.irq_armed == right.irq_armed &&
           left.irq_level == right.irq_level &&
           left.phase_numerator == right.phase_numerator &&
           left.phase_denominator == right.phase_denominator &&
           left.event_ordinal == right.event_ordinal;
}

bool equal_dma_snapshot(const clean_aot::soc::DmaSnapshot& left,
                        const clean_aot::soc::DmaSnapshot& right) noexcept {
    if (left.priority_control != right.priority_control ||
        left.interrupt_control != right.interrupt_control ||
        left.interrupt_flags != right.interrupt_flags ||
        left.master_irq_line != right.master_irq_line) {
        return false;
    }
    for (std::size_t index = 0U; index < DmaController::kChannelCount;
         ++index) {
        const auto& a = left.channels[index];
        const auto& b = right.channels[index];
        if (a.registers.base_address != b.registers.base_address ||
            a.registers.block_control != b.registers.block_control ||
            a.registers.channel_control != b.registers.channel_control ||
            a.active != b.active || a.completed != b.completed ||
            a.error != b.error || a.last_status != b.last_status ||
            a.current_address != b.current_address ||
            a.words_transferred != b.words_transferred ||
            a.words_remaining != b.words_remaining ||
            a.linked_current != b.linked_current ||
            a.linked_next != b.linked_next ||
            a.linked_nodes != b.linked_nodes ||
            a.linked_words != b.linked_words ||
            a.burst_words != b.burst_words ||
            a.block_words_remaining != b.block_words_remaining ||
            a.request_blocks_remaining != b.request_blocks_remaining ||
            a.cpu_window_remaining != b.cpu_window_remaining ||
            a.window_active != b.window_active ||
            a.request_asserted != b.request_asserted ||
            a.visited_nodes != b.visited_nodes) {
            return false;
        }
    }
    return true;
}

bool equal_snapshot(const SocSnapshot& left,
                    const SocSnapshot& right) noexcept {
    if (left.interrupts.status != right.interrupts.status ||
        left.interrupts.mask != right.interrupts.mask ||
        left.dma.priority_control != right.dma.priority_control ||
        left.dma.interrupt_control != right.dma.interrupt_control ||
        left.dma.interrupt_flags != right.dma.interrupt_flags ||
        left.dma.master_irq_line != right.dma.master_irq_line) {
        return false;
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        const auto& a = left.counters[index];
        const auto& b = right.counters[index];
        if (!equal_counter_snapshot(a, b)) {
            return false;
        }
    }
    return equal_dma_snapshot(left.dma, right.dma);
}

void test_interrupts(TestRunner& runner) {
    InterruptController interrupts;
    const std::uint32_t vblank =
        clean_aot::soc::interrupt_bit(InterruptSource::vblank);
    const std::uint32_t dma =
        clean_aot::soc::interrupt_bit(InterruptSource::dma);
    interrupts.write_mask(vblank | dma);
    interrupts.raise(InterruptSource::vblank);
    EXPECT(runner, interrupts.pending(InterruptSource::vblank));
    EXPECT(runner, interrupts.enabled(InterruptSource::vblank));
    EXPECT(runner, interrupts.cpu_line());
    interrupts.acknowledge(InterruptSource::vblank);
    EXPECT(runner, !interrupts.cpu_line());
    interrupts.raise_mask(0xffff'ffffU);
    EXPECT(runner, interrupts.status() == clean_aot::soc::kInterruptAllMask);
    interrupts.write_status(dma);
    EXPECT(runner, interrupts.status() == dma);
    EXPECT(runner, interrupts.cpu_line());
    interrupts.write_mask(0U);
    EXPECT(runner, !interrupts.cpu_line());
    interrupts.acknowledge_mask(0xffff'ffffU);
    EXPECT(runner, interrupts.status() == 0U);
    const auto invalid_source = static_cast<InterruptSource>(0xffU);
    EXPECT(runner, clean_aot::soc::interrupt_bit(invalid_source) == 0U);
    interrupts.raise(invalid_source);
    EXPECT(runner, interrupts.status() == 0U);
    const auto snapshot = interrupts.snapshot();
    EXPECT(runner, interrupts.restore(snapshot) == SocStatus::ok);
    EXPECT(runner, interrupts.restore({0x8000'0000U, 0U}) ==
                       SocStatus::snapshot_mismatch);
}

void test_interrupt_random_model(TestRunner& runner) {
    InterruptController interrupts;
    std::uint32_t model_status = 0U;
    std::uint32_t model_mask = 0U;
    std::uint32_t random = 0x6a09'e667U;
    for (std::size_t iteration = 0U; iteration < 512U; ++iteration) {
        const std::uint32_t value = next_random(random);
        switch (value % 6U) {
        case 0U:
            interrupts.raise_mask(value);
            model_status |= value & clean_aot::soc::kInterruptAllMask;
            break;
        case 1U:
            interrupts.acknowledge_mask(value);
            model_status &= ~(value & clean_aot::soc::kInterruptAllMask);
            break;
        case 2U:
            interrupts.write_status(value);
            model_status &= value & clean_aot::soc::kInterruptAllMask;
            break;
        case 3U:
            interrupts.write_mask(value);
            model_mask = value & clean_aot::soc::kInterruptAllMask;
            break;
        case 4U:
            interrupts.reset();
            model_status = 0U;
            model_mask = 0U;
            break;
        case 5U: {
            const auto saved = interrupts.snapshot();
            EXPECT(runner, interrupts.restore(saved) == SocStatus::ok);
            break;
        }
        default:
            break;
        }
        EXPECT(runner, interrupts.status() == model_status);
        EXPECT(runner, interrupts.mask() == model_mask);
        EXPECT(runner,
               interrupts.cpu_line() == ((model_status & model_mask) != 0U));
    }

    interrupts.raise(InterruptSource::dma);
    const auto before = interrupts.snapshot();
    EXPECT(runner,
           interrupts.restore({before.status | 0x8000'0000U, before.mask}) ==
               SocStatus::snapshot_mismatch);
    const auto after = interrupts.snapshot();
    EXPECT(runner, before.status == after.status && before.mask == after.mask);
}

void test_counters(TestRunner& runner) {
    SystemOnChip chip;
    CounterLog log{};
    chip.counters().set_callbacks(CounterCallbacks{&log, record_counter_event});
    chip.interrupts().write_mask(
        clean_aot::soc::interrupt_bit(InterruptSource::counter0) |
        clean_aot::soc::interrupt_bit(InterruptSource::counter1) |
        clean_aot::soc::interrupt_bit(InterruptSource::counter2));

    EXPECT(runner, chip.counters().write_target(0U, 3U) == SocStatus::ok);
    const std::uint16_t repeat_target_mode =
        static_cast<std::uint16_t>(0x0008U | 0x0010U | 0x0040U);
    EXPECT(runner,
           chip.counters().write_mode(0U, repeat_target_mode) == SocStatus::ok);
    clean_aot::soc::CounterAdvanceReport report{};
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(2U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 2U);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 0U);
    EXPECT(runner, chip.interrupts().pending(InterruptSource::counter0));
    EXPECT(runner,
           log.count >= 1U && log.events[0].kind == CounterEventKind::target);
    EXPECT(runner, (chip.counters().read_mode(0U) & 0x0800U) != 0U);
    EXPECT(runner, (chip.counters().read_mode(0U) & 0x0800U) == 0U);
    chip.interrupts().acknowledge(InterruptSource::counter0);

    EXPECT(runner, chip.counters().write_count(0U, 0U) == SocStatus::ok);
    const std::uint16_t toggle_mode =
        static_cast<std::uint16_t>(0x0008U | 0x0010U | 0x0040U | 0x0080U);
    EXPECT(runner,
           chip.counters().write_mode(0U, toggle_mode) == SocStatus::ok);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(3U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().irq_level(0U));
    chip.interrupts().acknowledge(InterruptSource::counter0);
    EXPECT(runner, !chip.interrupts().pending(InterruptSource::counter0));
    const CounterSnapshot asserted_snapshot = chip.counters().snapshot(0U);
    chip.counters().resample_irq_lines();
    EXPECT(runner, chip.interrupts().pending(InterruptSource::counter0));
    EXPECT(runner, equal_counter_snapshot(asserted_snapshot,
                                           chip.counters().snapshot(0U)));
    chip.interrupts().acknowledge(InterruptSource::counter0);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(3U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, !chip.counters().irq_level(0U));
    EXPECT(runner, !chip.interrupts().pending(InterruptSource::counter0));
    chip.interrupts().acknowledge(InterruptSource::counter0);

    EXPECT(runner, chip.counters().write_count(0U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_target(0U, 2U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_mode(0U, 0x0010U) == SocStatus::ok);
    const std::size_t event_count_before_one_shot = log.count;
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(2U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.interrupts().pending(InterruptSource::counter0));
    chip.interrupts().acknowledge(InterruptSource::counter0);
    chip.counters().resample_irq_lines();
    EXPECT(runner, !chip.interrupts().pending(InterruptSource::counter0));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(0x1'0000U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, !chip.interrupts().pending(InterruptSource::counter0));
    EXPECT(runner, log.count > event_count_before_one_shot);

    EXPECT(runner, chip.counters().write_mode(1U, 0x0020U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(1U, 0xffffU) == SocStatus::ok);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 0U);
    EXPECT(runner, chip.interrupts().pending(InterruptSource::counter1));
    EXPECT(runner, (chip.counters().read_mode(1U) & 0x1000U) != 0U);
    EXPECT(runner, (chip.counters().read_mode(1U) & 0x1000U) == 0U);
    bool saw_overflow = false;
    for (std::size_t index = 0U; index < log.count; ++index) {
        saw_overflow = saw_overflow ||
                       log.events[index].kind == CounterEventKind::overflow;
    }
    EXPECT(runner, saw_overflow);
    chip.interrupts().acknowledge(InterruptSource::counter1);

    EXPECT(runner, chip.counters().write_mode(2U, 0x0000U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_target(2U, 2U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(2U, 1U) == SocStatus::ok);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(2U) == 2U);
    EXPECT(runner, (chip.counters().read_mode(2U) & 0x0800U) != 0U);
    EXPECT(runner, (chip.counters().read_mode(2U) & 0x1000U) == 0U);
    EXPECT(runner, !chip.interrupts().pending(InterruptSource::counter2));

    EXPECT(runner, chip.counters().write_target(2U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(2U, 0xffffU) == SocStatus::ok);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(2U) == 0U);
    const std::uint16_t wrapped_status = chip.counters().read_mode(2U);
    EXPECT(runner, (wrapped_status & 0x0800U) != 0U);
    EXPECT(runner, (wrapped_status & 0x1000U) != 0U);

    EXPECT(runner, chip.counters().write_count(0U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_mode(0U, 0x0001U) == SocStatus::ok);
    EXPECT(runner, chip.counters().running(0U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(2U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 2U);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::hblank_begin,
                                              &report) == SocStatus::ok);
    EXPECT(runner, !chip.counters().running(0U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(3U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 2U);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::hblank_end,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().running(0U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 3U);

    EXPECT(runner, chip.counters().write_mode(1U, 0x0100U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(1U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::hblank_begin,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 1U);
    EXPECT(runner, chip.counters().write_mode(1U, 0x0003U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(1U, 5U) == SocStatus::ok);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::vblank_begin,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 0U);
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(1U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 1U);

    EXPECT(runner, chip.counters().write_mode(1U, 0x0005U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_count(1U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::vblank_begin,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().running(1U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(2U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 2U);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::vblank_end,
                                              &report) == SocStatus::ok);
    EXPECT(runner, !chip.counters().running(1U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(10U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 2U);
    EXPECT(runner, chip.counters().write_mode(1U, 0x0007U) == SocStatus::ok);
    EXPECT(runner, !chip.counters().running(1U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(10U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 0U);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::vblank_begin,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().running(1U));
    EXPECT(runner, chip.counters().advance_cpu(CycleDelta::whole(2U),
                                               &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(1U) == 2U);
    EXPECT(runner, chip.counters().write_count(0U, 0U) == SocStatus::ok);
    EXPECT(runner, chip.counters().write_mode(0U, 0x0100U) == SocStatus::ok);
    EXPECT(runner, chip.counters().input_edge(CounterEdge::dot_clock,
                                              &report) == SocStatus::ok);
    EXPECT(runner, chip.counters().read_count(0U) == 1U);

    RootCounters first;
    RootCounters second;
    EXPECT(runner, first.write_mode(2U, 0x0200U) == SocStatus::ok);
    EXPECT(runner, second.write_mode(2U, 0x0200U) == SocStatus::ok);
    EXPECT(runner,
           first.advance_cpu(CycleDelta{8U, 3U}, nullptr) == SocStatus::ok);
    EXPECT(runner,
           first.advance_cpu(CycleDelta{16U, 3U}, nullptr) == SocStatus::ok);
    EXPECT(runner,
           second.advance_cpu(CycleDelta::whole(8U), nullptr) == SocStatus::ok);
    EXPECT(runner, first.snapshot(2U).count == second.snapshot(2U).count);
    EXPECT(runner, first.snapshot(2U).phase_numerator ==
                       second.snapshot(2U).phase_numerator);
    EXPECT(runner, first.snapshot(2U).phase_denominator ==
                       second.snapshot(2U).phase_denominator);

    RootCounters huge;
    clean_aot::soc::CounterAdvanceReport huge_report{};
    EXPECT(runner,
           huge.advance_cpu(
               CycleDelta::whole(std::numeric_limits<std::uint64_t>::max()),
               &huge_report) == SocStatus::ok);
    EXPECT(runner,
           huge.read_count(0U) ==
               static_cast<std::uint16_t>(
                   std::numeric_limits<std::uint64_t>::max() % 0x1'0000ULL));
    EXPECT(runner,
           huge_report.events == std::numeric_limits<std::uint32_t>::max());

    const auto saved = first.snapshot(2U);
    EXPECT(runner,
           first.advance_cpu(CycleDelta{7U, 5U}, nullptr) == SocStatus::ok);
    const auto after = first.snapshot(2U);
    EXPECT(runner, first.restore(2U, saved) == SocStatus::ok);
    EXPECT(runner,
           first.advance_cpu(CycleDelta{7U, 5U}, nullptr) == SocStatus::ok);
    EXPECT(runner, first.snapshot(2U).count == after.count);
    EXPECT(runner, first.snapshot(2U).phase_numerator == after.phase_numerator);
    auto malformed_counter = saved;
    malformed_counter.phase_denominator = 0U;
    EXPECT(runner, first.restore(2U, malformed_counter) ==
                       SocStatus::snapshot_mismatch);
    EXPECT(runner, chip.counters().write_mode(2U, 0x0001U) == SocStatus::ok);
}

void test_counter_adversarial_state(TestRunner& runner) {
    InterruptController interrupts;
    RootCounters counters(&interrupts);
    CounterLog log{};
    counters.set_callbacks(CounterCallbacks{&log, record_counter_event});

    EXPECT(runner, counters.write_target(0U, 0x1234U) == SocStatus::ok);
    EXPECT(runner, counters.write_mode(0U, 0U) == SocStatus::ok);
    EXPECT(runner, counters.write_count(0U, 0xfffeU) == SocStatus::ok);
    EXPECT(runner, counters.advance_cpu(CycleDelta::whole(1U), nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.read_count(0U) == 0xffffU);
    EXPECT(runner, log.count == 0U);
    EXPECT(runner, counters.advance_cpu(CycleDelta::whole(1U), nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.read_count(0U) == 0U);
    EXPECT(runner,
           log.count == 1U && log.events[0].kind == CounterEventKind::overflow);

    log = CounterLog{};
    EXPECT(runner, counters.write_mode(0U, 0x0007U) == SocStatus::ok);
    EXPECT(runner, !counters.running(0U));
    EXPECT(runner, counters.input_edge(CounterEdge::hblank_begin, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.running(0U));
    EXPECT(runner, counters.snapshot(0U).synchronization_released);
    const std::size_t counter0_release_events = log.count;
    EXPECT(runner, counter0_release_events == 1U);
    EXPECT(runner, counters.input_edge(CounterEdge::hblank_end, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.input_edge(CounterEdge::hblank_begin, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, log.count == counter0_release_events);
    EXPECT(runner, counters.advance_cpu(CycleDelta::whole(3U), nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.read_count(0U) == 3U);

    EXPECT(runner, counters.write_mode(0U, 0x0001U) == SocStatus::ok);
    EXPECT(runner, counters.write_count(0U, 0U) == SocStatus::ok);
    EXPECT(runner,
           counters.advance_cpu(CycleDelta{1U, 2U}, nullptr) ==
               SocStatus::ok);
    EXPECT(runner, counters.snapshot(0U).phase_numerator == 1U &&
                       counters.snapshot(0U).phase_denominator == 2U);
    EXPECT(runner, counters.input_edge(CounterEdge::hblank_begin, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, !counters.running(0U));
    EXPECT(runner,
           counters.advance_cpu(CycleDelta{1U, 2U}, nullptr) ==
               SocStatus::ok);
    EXPECT(runner, counters.read_count(0U) == 0U);
    EXPECT(runner, counters.input_edge(CounterEdge::hblank_end, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.running(0U));
    EXPECT(runner,
           counters.advance_cpu(CycleDelta{1U, 2U}, nullptr) ==
               SocStatus::ok);
    EXPECT(runner, counters.read_count(0U) == 1U);

    EXPECT(runner, counters.write_mode(1U, 0x0007U) == SocStatus::ok);
    EXPECT(runner, !counters.running(1U));
    const std::size_t before_counter1_release = log.count;
    EXPECT(runner, counters.input_edge(CounterEdge::vblank_begin, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.running(1U));
    EXPECT(runner, counters.snapshot(1U).synchronization_released);
    EXPECT(runner, log.count == before_counter1_release + 1U);
    EXPECT(runner, counters.input_edge(CounterEdge::vblank_end, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, counters.input_edge(CounterEdge::vblank_begin, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, log.count == before_counter1_release + 1U);
    const CounterSnapshot released_counter1 = counters.snapshot(1U);
    RootCounters released_replay;
    EXPECT(runner,
           released_replay.restore(1U, released_counter1) == SocStatus::ok);
    EXPECT(runner,
           equal_counter_snapshot(released_counter1,
                                  released_replay.snapshot(1U)));
    auto malformed_release = released_counter1;
    malformed_release.running = false;
    EXPECT(runner,
           released_replay.restore(1U, malformed_release) ==
               SocStatus::snapshot_mismatch);
    EXPECT(runner,
           equal_counter_snapshot(released_counter1,
                                  released_replay.snapshot(1U)));

    InterruptController toggle_interrupts;
    RootCounters toggle(&toggle_interrupts);
    CounterLog toggle_log{};
    toggle.set_callbacks(CounterCallbacks{&toggle_log, record_counter_event});
    EXPECT(runner, toggle.write_target(0U, 1U) == SocStatus::ok);
    EXPECT(runner, toggle.write_mode(0U, 0x00d8U) == SocStatus::ok);
    EXPECT(runner,
           toggle.advance_cpu(CycleDelta::whole(1U), nullptr) == SocStatus::ok);
    toggle_interrupts.acknowledge(InterruptSource::counter0);
    EXPECT(runner,
           toggle.advance_cpu(CycleDelta::whole(1U), nullptr) == SocStatus::ok);
    EXPECT(runner, toggle_log.count == 2U);
    EXPECT(runner, toggle_log.events[0].irq_requested);
    EXPECT(runner, !toggle_log.events[1].irq_requested);
    EXPECT(runner, !toggle_interrupts.pending(InterruptSource::counter0));

    RootCounters reduced;
    EXPECT(runner, reduced.write_mode(2U, 0x0200U) == SocStatus::ok);
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT(runner, reduced.advance_cpu(CycleDelta{maximum, maximum}, nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, reduced.snapshot(2U).phase_numerator == 1U);
    EXPECT(runner, reduced.snapshot(2U).phase_denominator == 8U);

    InterruptController atomic_interrupts;
    RootCounters atomic(&atomic_interrupts);
    CounterLog atomic_log{};
    atomic.set_callbacks(CounterCallbacks{&atomic_log, record_counter_event});
    auto large_phase = atomic.snapshot(2U);
    large_phase.phase_numerator = maximum - 1U;
    large_phase.phase_denominator = maximum;
    EXPECT(runner, atomic.restore(2U, large_phase) == SocStatus::ok);
    atomic_interrupts.raise(InterruptSource::serial);
    const auto before_interrupt = atomic_interrupts.snapshot();
    const std::array<CounterSnapshot, 3> before_counters{
        atomic.snapshot(0U), atomic.snapshot(1U), atomic.snapshot(2U)};
    const std::size_t before_log = atomic_log.count;
    clean_aot::soc::CounterAdvanceReport overflow_report{};
    EXPECT(runner, atomic.advance_cpu(CycleDelta{maximum - 1U, maximum},
                                      &overflow_report) ==
                       SocStatus::arithmetic_overflow);
    EXPECT(runner,
           equal_counter_snapshot(before_counters[0], atomic.snapshot(0U)));
    EXPECT(runner,
           equal_counter_snapshot(before_counters[1], atomic.snapshot(1U)));
    EXPECT(runner,
           equal_counter_snapshot(before_counters[2], atomic.snapshot(2U)));
    const auto after_interrupt = atomic_interrupts.snapshot();
    EXPECT(runner, before_interrupt.status == after_interrupt.status &&
                       before_interrupt.mask == after_interrupt.mask);
    EXPECT(runner, atomic_log.count == before_log);

    auto malformed = atomic.snapshot(0U);
    const auto before_malformed = atomic.snapshot(0U);
    malformed.phase_numerator = 2U;
    malformed.phase_denominator = 4U;
    EXPECT(runner,
           atomic.restore(0U, malformed) == SocStatus::snapshot_mismatch);
    EXPECT(runner,
           equal_counter_snapshot(before_malformed, atomic.snapshot(0U)));
    malformed = before_malformed;
    malformed.synchronization_released = true;
    EXPECT(runner,
           atomic.restore(0U, malformed) == SocStatus::snapshot_mismatch);
    malformed = before_malformed;
    malformed.irq_armed = false;
    EXPECT(runner,
           atomic.restore(0U, malformed) == SocStatus::snapshot_mismatch);
    malformed = before_malformed;
    malformed.irq_level = !malformed.irq_level;
    EXPECT(runner,
           atomic.restore(0U, malformed) == SocStatus::snapshot_mismatch);
    malformed = before_malformed;
    malformed.mode =
        static_cast<std::uint16_t>((malformed.mode | 0x0010U) & 0xfbffU);
    malformed.irq_level = true;
    EXPECT(runner,
           atomic.restore(0U, malformed) == SocStatus::snapshot_mismatch);

    InterruptController reset_interrupts;
    RootCounters reset_counters(&reset_interrupts);
    CounterLog reset_log{};
    reset_counters.set_callbacks(
        CounterCallbacks{&reset_log, record_counter_event});
    EXPECT(runner, reset_counters.write_target(0U, 1U) == SocStatus::ok);
    EXPECT(runner, reset_counters.write_mode(0U, 0x0018U) == SocStatus::ok);
    reset_counters.reset();
    EXPECT(runner, reset_counters.read_count(0U) == 0U);
    EXPECT(runner, reset_counters.snapshot(0U).phase_denominator == 1U);
    EXPECT(runner, reset_counters.write_target(0U, 1U) == SocStatus::ok);
    EXPECT(runner, reset_counters.write_mode(0U, 0x0018U) == SocStatus::ok);
    EXPECT(runner, reset_counters.advance_cpu(CycleDelta::whole(1U), nullptr) ==
                       SocStatus::ok);
    EXPECT(runner, reset_log.count == 1U);
    EXPECT(runner, reset_interrupts.pending(InterruptSource::counter0));
}

void test_counter_random_differential(TestRunner& runner) {
    constexpr std::array<std::uint16_t, 4> targets{0U, 1U, 0xfffeU, 0xffffU};
    constexpr std::array<std::uint16_t, 2> starts{0xfffeU, 0xffffU};
    std::uint32_t random = 0xbb67'ae85U;

    for (const std::uint16_t target : targets) {
        for (const std::uint16_t start : starts) {
            for (const bool reset_at_target : {false, true}) {
                for (const bool divided_by_eight : {false, true}) {
                    RootCounters one_shot;
                    RootCounters chunked;
                    RootCounters callback_path;
                    callback_path.set_callbacks(
                        CounterCallbacks{nullptr, discard_counter_event});
                    const std::uint16_t mode = static_cast<std::uint16_t>(
                        (reset_at_target ? 0x0008U : 0U) |
                        (divided_by_eight ? 0x0200U : 0U));
                    for (RootCounters* const candidate :
                         {&one_shot, &chunked, &callback_path}) {
                        EXPECT(runner, candidate->write_target(2U, target) ==
                                           SocStatus::ok);
                        EXPECT(runner, candidate->write_mode(2U, mode) ==
                                           SocStatus::ok);
                        EXPECT(runner, candidate->write_count(2U, start) ==
                                           SocStatus::ok);
                    }

                    const std::uint64_t total_numerator =
                        140'000U + (next_random(random) % 10'000U);
                    constexpr std::uint64_t denominator = 7U;
                    EXPECT(runner, one_shot.advance_cpu(
                                       CycleDelta{total_numerator, denominator},
                                       nullptr) == SocStatus::ok);
                    EXPECT(runner, callback_path.advance_cpu(
                                       CycleDelta{total_numerator, denominator},
                                       nullptr) == SocStatus::ok);

                    std::uint64_t remaining = total_numerator;
                    std::size_t chunks = 0U;
                    while (remaining != 0U && chunks < 256U) {
                        const std::uint64_t proposed =
                            1U + (next_random(random) % 4096U);
                        const std::uint64_t piece =
                            proposed < remaining ? proposed : remaining;
                        EXPECT(runner, chunked.advance_cpu(
                                           CycleDelta{piece, denominator},
                                           nullptr) == SocStatus::ok);
                        remaining -= piece;
                        ++chunks;
                    }
                    EXPECT(runner, remaining == 0U);
                    EXPECT(runner,
                           equal_counter_snapshot(one_shot.snapshot(2U),
                                                  chunked.snapshot(2U)));
                    EXPECT(runner,
                           equal_counter_snapshot(one_shot.snapshot(2U),
                                                  callback_path.snapshot(2U)));
                }
            }
        }
    }
}

void test_native_timing_is_not_retirement_scaled(TestRunner& runner) {
    // Nine is an adversarial example only; it is not a product default.
    constexpr std::uint64_t retirement_multiplier = 9U;

    RootCounters native_time;
    CounterLog native_log{};
    native_time.set_callbacks(
        CounterCallbacks{&native_log, record_counter_event});
    EXPECT(runner, native_time.write_target(0U, 4U) == SocStatus::ok);
    EXPECT(runner, native_time.write_mode(0U, 0x0010U) == SocStatus::ok);

    // The scheduler may retire multiplier-scaled CPU work during this native
    // interval, but the peripheral clock sees only the native three cycles.
    EXPECT(runner,
           native_time.advance_cpu(CycleDelta::whole(3U), nullptr) ==
               SocStatus::ok);
    EXPECT(runner, native_time.read_count(0U) == 3U);
    EXPECT(runner, native_log.count == 0U);

    // The target boundary is reached on the next native cycle, independent
    // of how much translated CPU work was retired in the prior interval.
    EXPECT(runner,
           native_time.advance_cpu(CycleDelta::whole(1U), nullptr) ==
               SocStatus::ok);
    EXPECT(runner, native_time.read_count(0U) == 4U);
    EXPECT(runner, native_log.count == 1U);
    EXPECT(runner, native_log.events[0].kind == CounterEventKind::target);

    // Feeding the retirement quota as device time is observably wrong: it
    // crosses the same boundary and leaves the counter at 27, not 4.
    RootCounters incorrectly_scaled;
    EXPECT(runner,
           incorrectly_scaled.write_target(0U, 4U) == SocStatus::ok);
    EXPECT(runner,
           incorrectly_scaled.write_mode(0U, 0x0010U) == SocStatus::ok);
    EXPECT(runner,
           incorrectly_scaled.advance_cpu(
               CycleDelta::whole(3U * retirement_multiplier)) ==
               SocStatus::ok);
    EXPECT(runner, incorrectly_scaled.read_count(0U) == 27U);
    EXPECT(runner, incorrectly_scaled.snapshot(0U).count !=
                       native_time.snapshot(0U).count);
}

void test_dma_registers_and_linear(TestRunner& runner) {
    InterruptController interrupts;
    DmaController dma(&interrupts);
    Memory memory{};
    std::array<Device, 7> devices{};
    for (std::size_t index = 0U; index < DmaController::kChannelCount;
         ++index) {
        EXPECT(runner,
               dma.set_callbacks(index, bus_for(memory),
                                 device_for(devices[index])) == SocStatus::ok);
        EXPECT(runner, dma.write_base_address(
                           index, static_cast<std::uint32_t>(index * 4U)) ==
                           SocStatus::ok);
        EXPECT(runner, dma.write_block_control(index, 1U) == SocStatus::ok);
        EXPECT(runner, dma.write_channel_control(index, 0U) == SocStatus::ok);
        EXPECT(runner, dma.registers(index).base_address == index * 4U);
    }

    memory.words[memory_index(0x100U)] = 0x1111U;
    memory.words[memory_index(0x104U)] = 0x2222U;
    memory.words[memory_index(0x108U)] = 0x3333U;
    EXPECT(runner, dma.write_base_address(0U, 0x100U) == SocStatus::ok);
    EXPECT(runner, dma.write_block_control(0U, 3U) == SocStatus::ok);
    EXPECT(runner,
           dma.write_channel_control(
               0U, linear_control(true, false, false, 0U)) == SocStatus::ok);
    enable_dma_interrupt(dma, 0U);
    dma.write_priority_control(0x0888'8888U);
    const auto run = dma.run(DmaRunBudget{8U, 32U});
    EXPECT(runner, run.status == SocStatus::ok);
    EXPECT(runner, run.words_transferred == 3U);
    EXPECT(runner, devices[0].count == 3U);
    EXPECT(runner,
           devices[0].values[0] == 0x1111U && devices[0].values[2] == 0x3333U);
    EXPECT(runner, dma.channel_status(0U).completed);
    EXPECT(runner, (dma.interrupt_flags() & 1U) != 0U);
    EXPECT(runner, dma.irq_line());
    EXPECT(runner, interrupts.pending(InterruptSource::dma));
    EXPECT(runner, (dma.read_interrupt_control() & (1U << 24U)) != 0U);
    dma.write_interrupt_control(1U << 23U);
    EXPECT(runner, dma.irq_line());
    dma.write_interrupt_control((1U << 23U) | (1U << 16U) | (1U << 24U));
    EXPECT(runner, dma.interrupt_flags() == 0U);
    EXPECT(runner, !dma.irq_line());
    dma.acknowledge_interrupts(1U);
    interrupts.acknowledge(InterruptSource::dma);
    EXPECT(runner, !dma.irq_line());
    EXPECT(runner, !interrupts.pending(InterruptSource::dma));

    EXPECT(runner, dma.write_base_address(1U, 0x120U) == SocStatus::ok);
    memory.words[memory_index(0x120U)] = 0xaaaaU;
    EXPECT(runner, dma.write_block_control(1U, 0U) == SocStatus::ok);
    EXPECT(runner, dma.write_channel_control(
                       1U, linear_control(true, false, false, 1U, true,
                                          false)) == SocStatus::ok);
    EXPECT(runner, dma.set_request(1U, false) == SocStatus::ok);
    EXPECT(runner, dma.run(DmaRunBudget{1U, 4U}).status == SocStatus::idle);
    EXPECT(runner, dma.set_request(1U, true) == SocStatus::ok);
    const auto request = dma.run(DmaRunBudget{2U, 8U});
    EXPECT(runner, request.status == SocStatus::ok);
    EXPECT(runner, request.words_transferred == 2U);
    EXPECT(runner, dma.channel_status(1U).words_remaining ==
                       0x1'0000ULL * 0x1'0000ULL - 2U);

    set_priority(dma, 0U, 5U, false);
    set_priority(dma, 1U, 1U, false);
    const auto blocked = dma.run(DmaRunBudget{2U, 8U});
    EXPECT(runner, blocked.status == SocStatus::idle);

    dma.write_channel_control(1U, 0U);
    EXPECT(runner, dma.write_base_address(5U, 0x160U) == SocStatus::ok);
    EXPECT(runner, dma.write_block_control(5U, 0U) == SocStatus::ok);
    EXPECT(runner,
           dma.write_channel_control(
               5U, linear_control(true, false, false, 0U)) == SocStatus::ok);
    const auto manual_zero = dma.run(DmaRunBudget{1U, 4U});
    EXPECT(runner, manual_zero.status == SocStatus::ok);
    EXPECT(runner, dma.channel_status(5U).words_remaining == 0xffffU);
}

void test_dma_dpcr_endpoints_and_reset(TestRunner& runner) {
    DmaController reset_probe;
    EXPECT(runner, reset_probe.read_priority_control() == 0x0765'4321U);
    reset_probe.write_priority_control(0xf123'4567U);
    EXPECT(runner, reset_probe.read_priority_control() == 0xf123'4567U);
    reset_probe.write_interrupt_control(1U << 15U);
    EXPECT(runner, reset_probe.irq_line());
    EXPECT(runner, reset_probe.set_request(0U, false) == SocStatus::ok);
    reset_probe.reset();
    EXPECT(runner, reset_probe.read_priority_control() == 0x0765'4321U);
    EXPECT(runner, reset_probe.read_interrupt_control() == 0U);
    EXPECT(runner, reset_probe.channel_status(0U).request_asserted);
    EXPECT(runner, (reset_probe.registers(6U).channel_control & 3U) == 2U);

    for (std::size_t channel = 0U; channel < 6U; ++channel) {
        for (const bool to_device : {false, true}) {
            InterruptController interrupts;
            DmaController dma(&interrupts);
            Memory memory{};
            Device device{};
            EXPECT(runner,
                   dma.set_callbacks(channel, bus_for(memory),
                                     device_for(device)) == SocStatus::ok);
            set_priority(dma, channel, 0U, true);
            const std::uint32_t base =
                0x600U + static_cast<std::uint32_t>(channel * 0x20U);
            if (to_device) {
                memory.words[memory_index(base)] =
                    0x1000U + static_cast<std::uint32_t>(channel);
                memory.words[memory_index(base + 4U)] =
                    0x2000U + static_cast<std::uint32_t>(channel);
            } else {
                device.input[0] = 0x3000U + static_cast<std::uint32_t>(channel);
                device.input[1] = 0x4000U + static_cast<std::uint32_t>(channel);
                device.input_count = 2U;
            }
            EXPECT(runner,
                   dma.write_base_address(channel, base) == SocStatus::ok);
            EXPECT(runner,
                   dma.write_block_control(channel, 2U) == SocStatus::ok);
            EXPECT(runner, dma.write_channel_control(
                               channel, linear_control(to_device, false, false,
                                                       0U)) == SocStatus::ok);
            const auto before = dma.registers(channel);
            const auto report = dma.run(DmaRunBudget{4U, 8U});
            EXPECT(runner, report.status == SocStatus::ok);
            EXPECT(runner, report.words_transferred == 2U);
            EXPECT(runner, dma.channel_status(channel).completed);
            EXPECT(runner, (dma.registers(channel).channel_control &
                            ((1U << 24U) | (1U << 28U))) == 0U);
            EXPECT(runner,
                   dma.registers(channel).base_address == before.base_address);
            EXPECT(runner, dma.registers(channel).block_control ==
                               before.block_control);
            if (to_device) {
                EXPECT(runner, device.count == 2U);
                EXPECT(runner,
                       device.values[0] ==
                               0x1000U + static_cast<std::uint32_t>(channel) &&
                           device.values[1] ==
                               0x2000U + static_cast<std::uint32_t>(channel));
            } else {
                EXPECT(runner,
                       memory.words[memory_index(base)] ==
                           0x3000U + static_cast<std::uint32_t>(channel));
                EXPECT(runner,
                       memory.words[memory_index(base + 4U)] ==
                           0x4000U + static_cast<std::uint32_t>(channel));
            }
        }
    }

    InterruptController otc_interrupts;
    DmaController otc(&otc_interrupts);
    Memory otc_memory{};
    Device unused_device{};
    EXPECT(runner,
           otc.set_callbacks(6U, bus_for(otc_memory),
                             device_for(unused_device)) == SocStatus::ok);
    set_priority(otc, 6U, 0U, true);
    EXPECT(runner, otc.write_base_address(6U, 0x900U) == SocStatus::ok);
    EXPECT(runner, otc.write_block_control(6U, 3U) == SocStatus::ok);
    EXPECT(runner,
           otc.write_channel_control(
               6U, linear_control(true, false, false, 1U)) == SocStatus::ok);
    const auto normalized = otc.registers(6U);
    EXPECT(runner, (normalized.channel_control & 1U) == 0U);
    EXPECT(runner, (normalized.channel_control & (1U << 1U)) != 0U);
    EXPECT(runner, (normalized.channel_control & (3U << 9U)) == 0U);
    const auto otc_report = otc.run(DmaRunBudget{4U, 8U});
    EXPECT(runner, otc_report.status == SocStatus::ok);
    EXPECT(runner, otc_memory.write_addresses[0] == 0x900U &&
                       otc_memory.write_addresses[1] == 0x8fcU &&
                       otc_memory.write_addresses[2] == 0x8f8U);
    EXPECT(runner, otc_memory.words[memory_index(0x8f8U)] == 0x00ff'ffffU);
    EXPECT(runner,
           unused_device.count == 0U && unused_device.input_cursor == 0U);
    EXPECT(runner, otc.registers(6U).base_address == 0x900U);
    EXPECT(runner, otc.registers(6U).block_control == 3U);

    InterruptController tie_interrupts;
    DmaController tied(&tie_interrupts);
    Memory tie_memory{};
    std::array<Device, 7> tie_devices{};
    for (std::size_t channel = 0U; channel < 7U; ++channel) {
        EXPECT(runner, tied.set_callbacks(channel, bus_for(tie_memory),
                                          device_for(tie_devices[channel])) ==
                           SocStatus::ok);
        const std::uint32_t address =
            0xa00U + static_cast<std::uint32_t>(channel * 4U);
        tie_memory.words[memory_index(address)] =
            0x5000U + static_cast<std::uint32_t>(channel);
        EXPECT(runner,
               tied.write_base_address(channel, address) == SocStatus::ok);
        EXPECT(runner, tied.write_block_control(channel, 1U) == SocStatus::ok);
        EXPECT(runner, tied.write_channel_control(
                           channel, linear_control(true, false, false, 0U)) ==
                           SocStatus::ok);
    }
    tied.write_priority_control(0xfaaa'aaaaU);
    EXPECT(runner, tied.read_priority_control() == 0xfaaa'aaaaU);
    for (std::size_t ordinal = 0U; ordinal < 7U; ++ordinal) {
        const std::size_t expected = 6U - ordinal;
        const auto report = tied.run(DmaRunBudget{1U, 4U});
        EXPECT(runner, report.status == SocStatus::ok);
        EXPECT(runner, report.channel == expected);
    }

    DmaController callback_reset;
    Memory reset_memory{};
    Device reset_device{};
    EXPECT(runner, callback_reset.set_callbacks(0U, bus_for(reset_memory),
                                                device_for(reset_device)) ==
                       SocStatus::ok);
    callback_reset.reset();
    set_priority(callback_reset, 0U, 0U, true);
    reset_memory.words[memory_index(0xb00U)] = 0x55aaU;
    callback_reset.write_base_address(0U, 0xb00U);
    callback_reset.write_block_control(0U, 1U);
    callback_reset.write_channel_control(
        0U, linear_control(true, false, false, 0U));
    EXPECT(runner,
           callback_reset.run(DmaRunBudget{1U, 2U}).status == SocStatus::ok);
    EXPECT(runner,
           reset_device.count == 1U && reset_device.values[0] == 0x55aaU);
}

void test_dma_reentrant_callbacks_fail_closed(TestRunner& runner) {
    for (const ReentrantAction action : {ReentrantAction::reset,
                                         ReentrantAction::rewrite,
                                         ReentrantAction::run}) {
        InterruptController interrupts;
        DmaController dma(&interrupts);
        ReentrantMemory memory{&dma, action, 0x55aaU, SocStatus::idle};
        Device device{};
        EXPECT(runner, dma.set_callbacks(0U, bus_for(memory),
                                         device_for(device)) == SocStatus::ok);
        set_priority(dma, 0U, 0U, true);
        EXPECT(runner, dma.write_base_address(0U, 0xb00U) == SocStatus::ok);
        EXPECT(runner, dma.write_block_control(0U, 1U) == SocStatus::ok);
        EXPECT(runner, dma.write_channel_control(
                           0U, linear_control(true, false, false, 0U)) ==
                           SocStatus::ok);

        const auto report = dma.run(DmaRunBudget{1U, 2U});
        EXPECT(runner, report.status == SocStatus::callback_rejected);
        EXPECT(runner, report.words_transferred == 0U);
        EXPECT(runner, device.count == 0U);
        const auto status = dma.channel_status(0U);
        EXPECT(runner, status.error && !status.active &&
                           status.last_status == SocStatus::callback_rejected);
        EXPECT(runner, status.words_remaining !=
                           std::numeric_limits<std::uint64_t>::max());
        const auto snapshot = dma.snapshot();
        EXPECT(runner, dma.validate_snapshot(snapshot) == SocStatus::ok);
        if (action == ReentrantAction::rewrite) {
            EXPECT(runner, memory.nested_status == SocStatus::ok);
        } else if (action == ReentrantAction::run) {
            EXPECT(runner,
                   memory.nested_status == SocStatus::callback_rejected);
        }
    }
}

void test_dma_arbitration_and_edges(TestRunner& runner) {
    InterruptController interrupts;
    DmaController dma(&interrupts);
    Memory memory{};
    Device first{};
    Device second{};
    EXPECT(runner, dma.set_callbacks(0U, bus_for(memory), device_for(first)) ==
                       SocStatus::ok);
    EXPECT(runner, dma.set_callbacks(1U, bus_for(memory), device_for(second)) ==
                       SocStatus::ok);
    memory.words[memory_index(0x40U)] = 0x40U;
    memory.words[memory_index(0x44U)] = 0x44U;
    dma.write_base_address(0U, 0x40U);
    dma.write_base_address(1U, 0x44U);
    dma.write_block_control(0U, 1U);
    dma.write_block_control(1U, 1U);
    dma.write_channel_control(0U, linear_control(true, false, false, 0U));
    dma.write_channel_control(1U, linear_control(true, false, false, 0U));
    set_priority(dma, 0U, 5U, true);
    set_priority(dma, 1U, 1U, true);
    const auto first_run = dma.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, first_run.channel == 1U);
    EXPECT(runner, second.count == 1U && second.values[0] == 0x44U);
    const auto second_run = dma.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, second_run.channel == 0U);
    EXPECT(runner, first.count == 1U && first.values[0] == 0x40U);

    InterruptController tied_interrupts;
    DmaController tied(&tied_interrupts);
    Memory tied_memory{};
    Device tied_first{};
    Device tied_second{};
    tied.set_callbacks(0U, bus_for(tied_memory), device_for(tied_first));
    tied.set_callbacks(1U, bus_for(tied_memory), device_for(tied_second));
    tied_memory.words[memory_index(0x60U)] = 0x60U;
    tied_memory.words[memory_index(0x64U)] = 0x64U;
    tied.write_base_address(0U, 0x60U);
    tied.write_base_address(1U, 0x64U);
    tied.write_block_control(0U, 1U);
    tied.write_block_control(1U, 1U);
    tied.write_channel_control(0U, linear_control(true, false, false, 0U));
    tied.write_channel_control(1U, linear_control(true, false, false, 0U));
    set_priority(tied, 0U, 2U, true);
    set_priority(tied, 1U, 2U, true);
    EXPECT(runner, tied.run(DmaRunBudget{1U, 2U}).channel == 1U);
    EXPECT(runner, tied_second.count == 1U && tied_first.count == 0U);

    InterruptController unmasked_interrupts;
    DmaController unmasked(&unmasked_interrupts);
    Memory unmasked_memory{};
    Device unmasked_device{};
    unmasked.set_callbacks(0U, bus_for(unmasked_memory),
                           device_for(unmasked_device));
    unmasked_memory.words[memory_index(0x70U)] = 0x70U;
    unmasked.write_base_address(0U, 0x70U);
    unmasked.write_block_control(0U, 1U);
    unmasked.write_channel_control(0U, linear_control(true, false, false, 0U));
    set_priority(unmasked, 0U, 0U, true);
    EXPECT(runner, unmasked.run(DmaRunBudget{1U, 2U}).status == SocStatus::ok);
    EXPECT(runner, unmasked.interrupt_flags() == 0U);
    EXPECT(runner, !unmasked.irq_line());

    InterruptController disabled_interrupts;
    DmaController disabled(&disabled_interrupts);
    Memory disabled_memory{};
    Device disabled_device{};
    disabled.set_callbacks(0U, bus_for(disabled_memory),
                           device_for(disabled_device));
    disabled.write_base_address(0U, 0x80U);
    disabled.write_block_control(0U, 1U);
    disabled.write_channel_control(0U, linear_control(true, false, false, 0U));
    set_priority(disabled, 0U, 0U, false);
    EXPECT(runner,
           disabled.run(DmaRunBudget{1U, 2U}).status == SocStatus::idle);
}

void test_dma_start_windows_and_irq_edges(TestRunner& runner) {
    InterruptController manual_interrupts;
    DmaController manual(&manual_interrupts);
    Memory manual_memory{};
    Device manual_device{};
    manual.set_callbacks(0U, bus_for(manual_memory), device_for(manual_device));
    set_priority(manual, 0U, 0U, true);
    manual_memory.words[memory_index(0xc00U)] = 0x1111U;
    manual_memory.words[memory_index(0xc04U)] = 0x2222U;
    manual.set_request(0U, false);
    manual.write_base_address(0U, 0xc00U);
    manual.write_block_control(0U, 2U);
    manual.write_channel_control(
        0U, linear_control(true, false, false, 0U, true, true));
    EXPECT(runner, !manual.channel_status(0U).request_asserted);
    const auto first_manual = manual.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, first_manual.status == SocStatus::ok);
    EXPECT(runner, first_manual.words_transferred == 1U);
    EXPECT(runner, !manual.channel_status(0U).request_asserted);
    EXPECT(runner, (manual.registers(0U).channel_control & (1U << 28U)) == 0U);
    EXPECT(runner, (manual.registers(0U).channel_control & (1U << 24U)) != 0U);
    EXPECT(runner, manual.run(DmaRunBudget{2U, 4U}).status == SocStatus::ok);
    EXPECT(runner, manual.channel_status(0U).completed);
    EXPECT(runner, (manual.registers(0U).channel_control &
                    ((1U << 24U) | (1U << 28U))) == 0U);

    InterruptController request_interrupts;
    DmaController request(&request_interrupts);
    Memory request_memory{};
    Device request_device{};
    request.set_callbacks(1U, bus_for(request_memory),
                          device_for(request_device));
    set_priority(request, 1U, 0U, true);
    for (std::uint32_t word = 0U; word < 4U; ++word) {
        request_memory.words[memory_index(0xd00U + word * 4U)] = 0x7000U + word;
    }
    request.write_base_address(1U, 0xd00U);
    request.write_block_control(1U, (2U << 16U) | 2U);
    request.write_channel_control(
        1U, linear_control(true, false, false, 1U, true, false));
    const std::uint32_t request_bit = 1U << 1U;
    const std::uint32_t request_dicr =
        request_bit | (request_bit << 16U) | (1U << 23U);
    request.write_interrupt_control(request_dicr | 0x0000'7f80U);
    EXPECT(runner, (request.read_interrupt_control() & 0x0000'7f80U) == 0U);
    const auto first_block = request.run(DmaRunBudget{16U, 32U});
    EXPECT(runner, first_block.status == SocStatus::ok);
    EXPECT(runner, first_block.words_transferred == 2U);
    EXPECT(runner, first_block.irq_boundary);
    EXPECT(runner, request.channel_status(1U).active);
    EXPECT(runner, request.registers(1U).base_address == 0xd08U);
    EXPECT(runner,
           ((request.registers(1U).block_control >> 16U) & 0xffffU) == 1U);
    EXPECT(runner, (request.registers(1U).channel_control & (1U << 28U)) == 0U);
    EXPECT(runner, request_interrupts.pending(InterruptSource::dma));

    request_interrupts.acknowledge(InterruptSource::dma);
    request.write_interrupt_control(request_dicr);
    EXPECT(runner, request.irq_line());
    EXPECT(runner, !request_interrupts.pending(InterruptSource::dma));

    const auto high_line = request.snapshot();
    InterruptController restored_interrupts;
    DmaController restored(&restored_interrupts);
    Memory restored_memory = request_memory;
    Device restored_device = request_device;
    restored.set_callbacks(1U, bus_for(restored_memory),
                           device_for(restored_device));
    EXPECT(runner, restored.restore(high_line) == SocStatus::ok);
    EXPECT(runner, restored.irq_line());
    EXPECT(runner, !restored_interrupts.pending(InterruptSource::dma));
    EXPECT(runner, equal_dma_snapshot(high_line, restored.snapshot()));
    auto malformed_request = high_line;
    malformed_request.channels[1U].block_words_remaining = 0U;
    EXPECT(runner,
           restored.restore(malformed_request) == SocStatus::snapshot_mismatch);
    EXPECT(runner, equal_dma_snapshot(high_line, restored.snapshot()));

    request.acknowledge_interrupts(request_bit);
    restored.acknowledge_interrupts(request_bit);
    request.set_request(1U, true);
    restored.set_request(1U, true);
    const auto request_finish = request.run(DmaRunBudget{16U, 32U});
    const auto restored_finish = restored.run(DmaRunBudget{16U, 32U});
    EXPECT(runner, request_finish.words_transferred == 2U &&
                       restored_finish.words_transferred == 2U);
    EXPECT(runner, request_finish.irq_boundary && restored_finish.irq_boundary);
    EXPECT(runner, request.registers(1U).base_address == 0xd10U);
    EXPECT(runner,
           ((request.registers(1U).block_control >> 16U) & 0xffffU) == 0U);
    EXPECT(runner, (request.registers(1U).channel_control & (1U << 24U)) == 0U);
    EXPECT(runner, equal_dma_snapshot(request.snapshot(), restored.snapshot()));

    InterruptController held_interrupts;
    DmaController held(&held_interrupts);
    Memory held_memory{};
    Device held_device{};
    held.set_callbacks(3U, bus_for(held_memory), device_for(held_device));
    set_priority(held, 3U, 0U, true);
    for (std::uint32_t word = 0U; word < 6U; ++word) {
        held_memory.words[memory_index(0xe00U + word * 4U)] = 0x8000U + word;
    }
    held.write_base_address(3U, 0xe00U);
    held.write_block_control(3U, (2U << 16U) | 3U);
    held.write_channel_control(
        3U, linear_control(true, false, false, 1U, true, false));
    EXPECT(runner, held.run(DmaRunBudget{1U, 2U}).words_transferred == 1U);
    held.set_request(3U, false);
    const auto finish_window = held.run(DmaRunBudget{8U, 16U});
    EXPECT(runner, finish_window.status == SocStatus::ok);
    EXPECT(runner, finish_window.words_transferred == 2U);
    EXPECT(runner, held.channel_status(3U).active);
    EXPECT(runner, held.channel_status(3U).words_remaining == 3U);
    EXPECT(runner, held.run(DmaRunBudget{8U, 16U}).status == SocStatus::idle);
    const auto before_force = held.snapshot();
    const std::uint32_t force_control =
        held.registers(3U).channel_control | (1U << 28U);
    EXPECT(runner,
           held.write_channel_control(3U, force_control) == SocStatus::ok);
    EXPECT(runner, held.channel_status(3U).words_transferred ==
                       before_force.channels[3U].words_transferred);
    const auto forced_window = held.run(DmaRunBudget{8U, 16U});
    EXPECT(runner, forced_window.words_transferred == 3U);
    EXPECT(runner, held.channel_status(3U).completed);
    EXPECT(runner, !held.channel_status(3U).request_asserted);

    InterruptController list_interrupts;
    DmaController list(&list_interrupts);
    Memory list_memory{};
    Device list_device{};
    list.set_callbacks(2U, bus_for(list_memory), device_for(list_device));
    set_priority(list, 2U, 0U, true);
    list_memory.words[memory_index(0xf00U)] = (2U << 24U) | 0xf20U;
    list_memory.words[memory_index(0xf04U)] = 0xa1U;
    list_memory.words[memory_index(0xf08U)] = 0xa2U;
    list_memory.words[memory_index(0xf20U)] = (1U << 24U) | 0x00ff'ffffU;
    list_memory.words[memory_index(0xf24U)] = 0xb1U;
    list.write_base_address(2U, 0xf00U);
    list.write_channel_control(
        2U, linear_control(true, false, false, 2U, true, false));
    enable_dma_slice_interrupt(list, 2U);
    const auto header_only = list.run(DmaRunBudget{4U, 1U});
    EXPECT(runner, header_only.status == SocStatus::ok);
    EXPECT(runner,
           header_only.words_transferred == 0U && header_only.cpu_cycles == 1U);
    list.set_request(2U, false);
    EXPECT(runner, list.run(DmaRunBudget{1U, 1U}).words_transferred == 1U);
    const auto node_end = list.run(DmaRunBudget{1U, 1U});
    EXPECT(runner, node_end.words_transferred == 1U);
    EXPECT(runner, node_end.irq_boundary);
    EXPECT(runner, list.registers(2U).base_address == 0xf20U);
    EXPECT(runner, list.channel_status(2U).active);
    EXPECT(runner, list.run(DmaRunBudget{4U, 8U}).status == SocStatus::idle);
    list.acknowledge_interrupts(1U << 2U);
    list_interrupts.acknowledge(InterruptSource::dma);
    const auto list_before_force = list.snapshot();
    EXPECT(runner,
           list.write_channel_control(2U, list.registers(2U).channel_control |
                                              (1U << 28U)) == SocStatus::ok);
    EXPECT(runner, list.channel_status(2U).linked_nodes ==
                       list_before_force.channels[2U].linked_nodes);
    const auto list_finish = list.run(DmaRunBudget{4U, 8U});
    EXPECT(runner, list_finish.status == SocStatus::ok);
    EXPECT(runner, list_finish.words_transferred == 1U);
    EXPECT(runner, list_finish.irq_boundary);
    EXPECT(runner, list.channel_status(2U).completed);
    EXPECT(runner, list.registers(2U).base_address == 0x00ff'ffffU);
}

void test_dma_address_chop_otc(TestRunner& runner) {
    InterruptController interrupts;
    DmaController dma(&interrupts);
    dma.write_priority_control(0x0888'8888U);
    Memory memory{};
    std::array<Device, 7> devices{};
    for (std::size_t index = 0U; index < 7U; ++index) {
        dma.set_callbacks(index, bus_for(memory), device_for(devices[index]));
    }
    memory.words[memory_index(0U)] = 0x1U;
    memory.words[memory_index(0xfffffcU)] = 0x2U;
    EXPECT(runner, dma.write_base_address(2U, 0x123U) == SocStatus::ok);
    EXPECT(runner, dma.registers(2U).base_address == 0x123U);
    EXPECT(runner, dma.channel_status(2U).current_address == 0x120U);
    EXPECT(runner, dma.write_base_address(2U, 0U) == SocStatus::ok);
    dma.write_block_control(2U, 2U);
    dma.write_channel_control(2U, linear_control(true, true, false, 0U));
    const auto wrapped = dma.run(DmaRunBudget{2U, 4U});
    EXPECT(runner, wrapped.status == SocStatus::ok);
    EXPECT(runner, memory.read_addresses[0] == 0U &&
                       memory.read_addresses[1] == 0xfffffcU);
    EXPECT(runner,
           devices[2].values[0] == 0x1U && devices[2].values[1] == 0x2U);

    memory.words[memory_index(0x180U)] = 3U;
    memory.words[memory_index(0x184U)] = 4U;
    memory.words[memory_index(0x188U)] = 5U;
    memory.words[memory_index(0x18cU)] = 6U;
    dma.write_base_address(3U, 0x180U);
    dma.write_block_control(3U, 4U);
    const std::uint32_t chop_control =
        linear_control(true, false, true, 0U) | (1U << 20U);
    dma.write_channel_control(3U, chop_control);
    const auto chopped = dma.run(DmaRunBudget{4U, 16U});
    EXPECT(runner, chopped.status == SocStatus::ok);
    EXPECT(runner, chopped.words_transferred == 4U);
    EXPECT(runner, chopped.cpu_cycles == 10U);

    memory.read_addresses.fill(0U);
    memory.write_addresses.fill(0U);
    memory.reads = 0U;
    memory.writes = 0U;
    dma.write_base_address(6U, 0x200U);
    dma.write_block_control(6U, 3U);
    dma.write_channel_control(6U, linear_control(false, true, false, 0U));
    const auto otc = dma.run(DmaRunBudget{3U, 8U});
    EXPECT(runner, otc.status == SocStatus::ok);
    EXPECT(runner, memory.write_addresses[0] == 0x200U &&
                       memory.write_addresses[1] == 0x1fcU &&
                       memory.write_addresses[2] == 0x1f8U);
    EXPECT(runner, memory.words[memory_index(0x200U)] == 0x1fcU);
    EXPECT(runner, memory.words[memory_index(0x1f8U)] == 0x00ff'ffffU);
}

void test_dma_chopping_chunk_invariance(TestRunner& runner) {
    InterruptController one_interrupts;
    InterruptController split_interrupts;
    DmaController one_shot(&one_interrupts);
    DmaController split(&split_interrupts);
    Memory one_memory{};
    Memory split_memory{};
    Device one_device{};
    Device split_device{};
    one_shot.set_callbacks(3U, bus_for(one_memory), device_for(one_device));
    split.set_callbacks(3U, bus_for(split_memory), device_for(split_device));
    set_priority(one_shot, 3U, 0U, true);
    set_priority(split, 3U, 0U, true);
    for (std::uint32_t word = 0U; word < 5U; ++word) {
        one_memory.words[memory_index(0x1600U + word * 4U)] = 0x9000U + word;
        split_memory.words[memory_index(0x1600U + word * 4U)] = 0x9000U + word;
    }
    const std::uint32_t control =
        linear_control(true, false, true, 0U) | (1U << 16U) | (3U << 20U);
    for (DmaController* const dma : {&one_shot, &split}) {
        dma->write_base_address(3U, 0x1600U);
        dma->write_block_control(3U, 5U);
        dma->write_channel_control(3U, control);
    }

    const auto whole = one_shot.run(DmaRunBudget{16U, 64U});
    EXPECT(runner, whole.status == SocStatus::ok);
    EXPECT(runner, whole.words_transferred == 5U);
    EXPECT(runner, whole.cpu_cycles == 21U);

    std::uint64_t split_words = 0U;
    std::uint64_t split_cycles = 0U;
    bool saw_cycle_only_progress = false;
    for (std::size_t call = 0U;
         call < 64U && !split.channel_status(3U).completed; ++call) {
        const auto report = split.run(DmaRunBudget{16U, 1U});
        EXPECT(runner, report.status == SocStatus::ok);
        split_words += report.words_transferred;
        split_cycles += report.cpu_cycles;
        saw_cycle_only_progress =
            saw_cycle_only_progress ||
            (report.words_transferred == 0U && report.cpu_cycles == 1U);
        if (call == 0U) {
            EXPECT(runner, split.registers(3U).base_address == 0x1604U);
            EXPECT(runner, (split.registers(3U).block_control & 0xffffU) == 4U);
        }
    }
    EXPECT(runner, split.channel_status(3U).completed);
    EXPECT(runner, saw_cycle_only_progress);
    EXPECT(runner, split_words == whole.words_transferred);
    EXPECT(runner, split_cycles == whole.cpu_cycles);
    EXPECT(runner, equal_dma_snapshot(one_shot.snapshot(), split.snapshot()));
    EXPECT(runner, one_device.count == split_device.count);
    for (std::size_t index = 0U; index < one_device.count; ++index) {
        EXPECT(runner, one_device.values[index] == split_device.values[index]);
    }
}

void test_dma_linked_and_errors(TestRunner& runner) {
    InterruptController interrupts;
    DmaController dma(&interrupts, 4U, 8U);
    set_priority(dma, 2U, 0U, true);
    Memory memory{};
    Device device{};
    EXPECT(runner, dma.set_callbacks(2U, bus_for(memory), device_for(device)) ==
                       SocStatus::ok);
    memory.words[memory_index(0x200U)] = (2U << 24U) | 0x240U;
    memory.words[memory_index(0x204U)] = 0xaaaaU;
    memory.words[memory_index(0x208U)] = 0xbbbbU;
    memory.words[memory_index(0x240U)] = (1U << 24U) | 0x00ff'ffffU;
    memory.words[memory_index(0x244U)] = 0xccccU;
    dma.write_base_address(2U, 0x200U);
    dma.write_block_control(2U, 0U);
    dma.write_channel_control(2U, linear_control(true, false, false, 2U));
    enable_dma_interrupt(dma, 2U);
    const auto linked = dma.run(DmaRunBudget{8U, 16U});
    EXPECT(runner, linked.status == SocStatus::ok);
    EXPECT(runner, linked.words_transferred == 3U);
    EXPECT(runner, device.count == 3U);
    EXPECT(runner, device.values[0] == 0xaaaaU && device.values[2] == 0xccccU);
    EXPECT(runner, dma.channel_status(2U).completed);
    EXPECT(runner, dma.channel_status(2U).linked_nodes == 2U);
    EXPECT(runner, (dma.interrupt_flags() & (1U << 2U)) != 0U);

    InterruptController stalled_interrupts;
    DmaController stalled(&stalled_interrupts, 4U, 8U);
    set_priority(stalled, 2U, 0U, true);
    Memory stalled_memory{};
    Device stalled_device{};
    stalled_device.not_ready = true;
    EXPECT(runner, stalled.set_callbacks(2U, bus_for(stalled_memory),
                                         device_for(stalled_device)) == SocStatus::ok);
    stalled_memory.words[memory_index(0x280U)] = (2U << 24U) | 0x00ff'ffffU;
    stalled_memory.words[memory_index(0x284U)] = 0x1111U;
    stalled_memory.words[memory_index(0x288U)] = 0x2222U;
    stalled.write_base_address(2U, 0x280U);
    stalled.write_channel_control(2U, linear_control(true, false, false, 2U));
    enable_dma_interrupt(stalled, 2U);
    const auto blocked = stalled.run(DmaRunBudget{8U, 16U});
    const auto blocked_state = stalled.channel_status(2U);
    EXPECT(runner, blocked.status == SocStatus::ok);
    EXPECT(runner, blocked.words_transferred == 0U && blocked.cpu_cycles == 1U);
    EXPECT(runner, blocked_state.active && !blocked_state.error &&
                       !blocked_state.completed && blocked_state.linked_nodes == 1U &&
                       blocked_state.words_remaining == 2U);
    EXPECT(runner, stalled_device.count == 0U && stalled.interrupt_flags() == 0U &&
                       !stalled.irq_line());
    const auto stalled_snapshot = stalled.snapshot();
    stalled_device.not_ready = false;
    const auto resumed = stalled.run(DmaRunBudget{8U, 16U});
    EXPECT(runner, resumed.status == SocStatus::ok && resumed.words_transferred == 2U);
    EXPECT(runner, stalled.channel_status(2U).completed &&
                       !stalled.channel_status(2U).error);
    EXPECT(runner, stalled_device.count == 2U && stalled_device.values[0] == 0x1111U &&
                       stalled_device.values[1] == 0x2222U);
    EXPECT(runner, (stalled.interrupt_flags() & (1U << 2U)) != 0U &&
                       stalled.irq_line());

    DmaController replay(&stalled_interrupts, 4U, 8U);
    Device replay_device{};
    replay_device.not_ready = false;
    EXPECT(runner, replay.set_callbacks(2U, bus_for(stalled_memory),
                                        device_for(replay_device)) == SocStatus::ok);
    EXPECT(runner, replay.restore(stalled_snapshot) == SocStatus::ok);
    const auto replayed = replay.run(DmaRunBudget{8U, 16U});
    EXPECT(runner, replayed.words_transferred == resumed.words_transferred &&
                       replayed.cpu_cycles == resumed.cpu_cycles && replay_device.count == 2U);

    InterruptController loop_interrupts;
    DmaController loop_dma(&loop_interrupts, 4U, 8U);
    set_priority(loop_dma, 2U, 0U, true);
    Memory loop_memory{};
    Device loop_device{};
    loop_dma.set_callbacks(2U, bus_for(loop_memory), device_for(loop_device));
    loop_memory.words[memory_index(0x300U)] = 0x300U;
    loop_dma.write_base_address(2U, 0x300U);
    loop_dma.write_channel_control(2U, linear_control(true, false, false, 2U));
    enable_dma_interrupt(loop_dma, 2U);
    const auto loop = loop_dma.run(DmaRunBudget{4U, 8U});
    EXPECT(runner, loop.status == SocStatus::linked_list_loop);
    EXPECT(runner, loop_dma.channel_status(2U).error);
    EXPECT(runner, loop_dma.channel_status(2U).last_status ==
                       SocStatus::linked_list_loop);
    EXPECT(runner, loop_dma.interrupt_flags() == 0U);
    EXPECT(runner, (loop_dma.read_interrupt_control() & (1U << 15U)) == 0U);

    InterruptController malformed_interrupts;
    DmaController malformed(&malformed_interrupts);
    set_priority(malformed, 4U, 0U, true);
    malformed.write_base_address(4U, 0x100U);
    malformed.write_block_control(4U, 1U);
    malformed.write_channel_control(4U, linear_control(true, false, false, 0U));
    enable_dma_interrupt(malformed, 4U);
    const auto missing = malformed.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, missing.status == SocStatus::malformed_callback);
    EXPECT(runner, malformed.channel_status(4U).error);
    EXPECT(runner, malformed.interrupt_flags() == 0U);
    EXPECT(runner, (malformed.read_interrupt_control() & (1U << 15U)) == 0U);
    EXPECT(runner, (malformed.read_interrupt_control() & (1U << 31U)) == 0U);
    malformed.write_interrupt_control((1U << 15U) | (1U << 20U) | (1U << 23U) |
                                      (1U << 28U));
    EXPECT(runner, (malformed.read_interrupt_control() & (1U << 15U)) != 0U);
    EXPECT(runner, (malformed.read_interrupt_control() & 0x0000'7f80U) == 0U);
    EXPECT(runner, malformed.interrupt_flags() == 0U);
    malformed.write_interrupt_control((1U << 20U) | (1U << 23U));
    EXPECT(runner, !malformed.irq_line());

    Memory rejected_memory{};
    rejected_memory.reject = true;
    InterruptController rejected_interrupts;
    DmaController rejected(&rejected_interrupts);
    set_priority(rejected, 0U, 0U, true);
    Device rejected_device{};
    rejected.set_callbacks(0U, bus_for(rejected_memory),
                           device_for(rejected_device));
    rejected.write_base_address(0U, 0x100U);
    rejected.write_block_control(0U, 1U);
    rejected.write_channel_control(0U, linear_control(true, false, false, 0U));
    enable_dma_interrupt(rejected, 0U);
    EXPECT(runner,
           rejected.run(DmaRunBudget{1U, 2U}).status == SocStatus::bus_fault);
    EXPECT(runner, rejected.interrupt_flags() == 0U);
    EXPECT(runner, (rejected.read_interrupt_control() & (1U << 15U)) != 0U);
    EXPECT(runner, rejected.irq_line());

    Memory alignment_memory{};
    Device alignment_device{};
    DmaController alignment(&interrupts);
    set_priority(alignment, 2U, 0U, true);
    alignment.set_callbacks(2U, bus_for(alignment_memory),
                            device_for(alignment_device));
    alignment_memory.words[memory_index(0x380U)] = (1U << 24U) | 0x382U;
    alignment.write_base_address(2U, 0x380U);
    alignment.write_channel_control(2U, linear_control(true, false, false, 2U));
    enable_dma_interrupt(alignment, 2U);
    EXPECT(runner, alignment.run(DmaRunBudget{1U, 4U}).status ==
                       SocStatus::invalid_argument);
    EXPECT(runner, alignment.interrupt_flags() == 0U);
    EXPECT(runner, (alignment.read_interrupt_control() & (1U << 15U)) == 0U);

    InterruptController limited_interrupts;
    DmaController limited_nodes(&limited_interrupts, 1U, 8U);
    set_priority(limited_nodes, 2U, 0U, true);
    Memory limited_memory{};
    Device limited_device{};
    limited_nodes.set_callbacks(2U, bus_for(limited_memory),
                                device_for(limited_device));
    limited_memory.words[memory_index(0x400U)] = (1U << 24U) | 0x420U;
    limited_memory.words[memory_index(0x404U)] = 0x111U;
    limited_nodes.write_base_address(2U, 0x400U);
    limited_nodes.write_channel_control(2U,
                                        linear_control(true, false, false, 2U));
    enable_dma_interrupt(limited_nodes, 2U);
    EXPECT(runner, limited_nodes.run(DmaRunBudget{4U, 8U}).status ==
                       SocStatus::traversal_limit);
    EXPECT(runner, limited_device.count == 1U);
    EXPECT(runner, limited_nodes.interrupt_flags() == 0U);
    EXPECT(runner,
           (limited_nodes.read_interrupt_control() & (1U << 15U)) == 0U);

    InterruptController word_limit_interrupts;
    DmaController limited_words(&word_limit_interrupts, 8U, 1U);
    set_priority(limited_words, 2U, 0U, true);
    Memory word_limit_memory{};
    Device word_limit_device{};
    limited_words.set_callbacks(2U, bus_for(word_limit_memory),
                                device_for(word_limit_device));
    word_limit_memory.words[memory_index(0x440U)] = (2U << 24U) | 0x00ff'ffffU;
    word_limit_memory.words[memory_index(0x444U)] = 0x222U;
    limited_words.write_base_address(2U, 0x440U);
    limited_words.write_channel_control(2U,
                                        linear_control(true, false, false, 2U));
    enable_dma_interrupt(limited_words, 2U);
    EXPECT(runner, limited_words.run(DmaRunBudget{4U, 8U}).status ==
                       SocStatus::traversal_limit);
    EXPECT(runner, word_limit_device.count == 1U);
    EXPECT(runner, limited_words.interrupt_flags() == 0U);
    EXPECT(runner,
           (limited_words.read_interrupt_control() & (1U << 15U)) == 0U);
}

void test_dma_fail_closed_modes_and_flags(TestRunner& runner) {
    InterruptController wrong_channel_interrupts;
    DmaController wrong_channel(&wrong_channel_interrupts);
    Memory wrong_memory{};
    Device wrong_device{};
    wrong_channel.set_callbacks(1U, bus_for(wrong_memory),
                                device_for(wrong_device));
    set_priority(wrong_channel, 1U, 0U, true);
    wrong_memory.words[memory_index(0x1000U)] = (1U << 24U) | 0x00ff'ffffU;
    wrong_channel.write_base_address(1U, 0x1000U);
    wrong_channel.write_channel_control(1U,
                                        linear_control(true, false, false, 2U));
    enable_dma_interrupt(wrong_channel, 1U);
    const auto wrong = wrong_channel.run(DmaRunBudget{4U, 8U});
    EXPECT(runner, wrong.status == SocStatus::invalid_mode);
    EXPECT(runner, wrong_memory.reads == 0U && wrong_memory.writes == 0U);
    EXPECT(runner, wrong_device.count == 0U && wrong_device.input_cursor == 0U);
    EXPECT(runner, wrong_channel.interrupt_flags() == 0U);
    EXPECT(runner,
           (wrong_channel.read_interrupt_control() & (1U << 15U)) == 0U);
    EXPECT(runner, (wrong_channel.registers(1U).channel_control &
                    ((1U << 24U) | (1U << 28U))) == 0U);

    InterruptController request_chop_interrupts;
    DmaController request_chop(&request_chop_interrupts);
    Memory request_chop_memory{};
    Device request_chop_device{};
    request_chop.set_callbacks(0U, bus_for(request_chop_memory),
                               device_for(request_chop_device));
    set_priority(request_chop, 0U, 0U, true);
    request_chop.write_base_address(0U, 0x1100U);
    request_chop.write_block_control(0U, (1U << 16U) | 1U);
    request_chop.write_channel_control(0U,
                                       linear_control(true, false, true, 1U));
    EXPECT(runner, request_chop.run(DmaRunBudget{1U, 2U}).status ==
                       SocStatus::invalid_mode);
    EXPECT(runner,
           request_chop_memory.reads == 0U && request_chop_device.count == 0U);
    EXPECT(runner, request_chop.interrupt_flags() == 0U);
    EXPECT(runner, (request_chop.read_interrupt_control() & (1U << 15U)) == 0U);

    InterruptController list_chop_interrupts;
    DmaController list_chop(&list_chop_interrupts);
    Memory list_chop_memory{};
    Device list_chop_device{};
    list_chop.set_callbacks(2U, bus_for(list_chop_memory),
                            device_for(list_chop_device));
    set_priority(list_chop, 2U, 0U, true);
    list_chop.write_base_address(2U, 0x1200U);
    list_chop.write_channel_control(2U, linear_control(true, false, true, 2U));
    EXPECT(runner, list_chop.run(DmaRunBudget{1U, 2U}).status ==
                       SocStatus::invalid_mode);
    EXPECT(runner,
           list_chop_memory.reads == 0U && list_chop_device.count == 0U);
    EXPECT(runner, list_chop.interrupt_flags() == 0U);

    InterruptController device_interrupts;
    DmaController device_failure(&device_interrupts);
    Memory device_memory{};
    Device rejecting_device{};
    rejecting_device.reject = true;
    device_failure.set_callbacks(0U, bus_for(device_memory),
                                 device_for(rejecting_device));
    set_priority(device_failure, 0U, 0U, true);
    enable_dma_interrupt(device_failure, 0U);
    device_memory.words[memory_index(0x1300U)] = 0xdeadU;
    device_failure.write_base_address(0U, 0x1300U);
    device_failure.write_block_control(0U, 1U);
    device_failure.write_channel_control(
        0U, linear_control(true, false, false, 0U));
    EXPECT(runner, device_failure.run(DmaRunBudget{1U, 2U}).status ==
                       SocStatus::device_fault);
    EXPECT(runner, device_failure.interrupt_flags() == 0U);
    EXPECT(runner,
           (device_failure.read_interrupt_control() & (1U << 15U)) == 0U);
    EXPECT(runner, !device_failure.irq_line());

    InterruptController sticky_interrupts;
    DmaController sticky(&sticky_interrupts);
    Memory sticky_memory{};
    Device sticky_device{};
    sticky.set_callbacks(6U, bus_for(sticky_memory), device_for(sticky_device));
    set_priority(sticky, 6U, 0U, true);
    enable_dma_interrupt(sticky, 6U);
    sticky.write_base_address(6U, 0x1400U);
    sticky.write_block_control(6U, 1U);
    sticky.write_channel_control(6U, linear_control(false, true, false, 0U));
    EXPECT(runner, sticky.run(DmaRunBudget{1U, 2U}).status == SocStatus::ok);
    EXPECT(runner, sticky.interrupt_flags() == (1U << 6U));
    sticky_interrupts.acknowledge(InterruptSource::dma);
    set_priority(sticky, 4U, 0U, true);
    sticky.write_interrupt_control((1U << 20U) | (1U << 23U));
    sticky.write_base_address(4U, 0x1500U);
    sticky.write_block_control(4U, 1U);
    sticky.write_channel_control(4U, linear_control(true, false, false, 0U));
    const auto malformed = sticky.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, malformed.status == SocStatus::malformed_callback);
    EXPECT(runner, sticky.interrupt_flags() == (1U << 6U));
    EXPECT(runner, (sticky.interrupt_flags() & (1U << 4U)) == 0U);
    EXPECT(runner, (sticky.read_interrupt_control() & (1U << 15U)) == 0U);
}

void test_dma_snapshot(TestRunner& runner) {
    InterruptController interrupts;
    DmaController dma(&interrupts);
    set_priority(dma, 0U, 0U, true);
    Memory memory{};
    Device device{};
    dma.set_callbacks(0U, bus_for(memory), device_for(device));
    memory.words[memory_index(0x500U)] = 0x10U;
    memory.words[memory_index(0x504U)] = 0x20U;
    memory.words[memory_index(0x508U)] = 0x30U;
    dma.write_base_address(0U, 0x500U);
    dma.write_block_control(0U, 3U);
    dma.write_channel_control(0U, linear_control(true, false, false, 0U));
    EXPECT(runner, dma.run(DmaRunBudget{1U, 2U}).words_transferred == 1U);
    const auto saved = dma.snapshot();
    const auto rest = dma.run(DmaRunBudget{8U, 8U});
    EXPECT(runner, rest.status == SocStatus::ok);
    const auto after = dma.snapshot();
    EXPECT(runner, dma.restore(saved) == SocStatus::ok);
    const auto replay = dma.run(DmaRunBudget{8U, 8U});
    EXPECT(runner, replay.status == SocStatus::ok);
    const auto replayed = dma.snapshot();
    EXPECT(runner, equal_dma_snapshot(after, replayed));
    EXPECT(runner, dma.restore(after) == SocStatus::ok);
    EXPECT(runner, after.channels[0].current_address ==
                       replayed.channels[0].current_address);
    EXPECT(runner, after.channels[0].words_transferred ==
                       replayed.channels[0].words_transferred);
    EXPECT(runner,
           after.channels[0].completed == replayed.channels[0].completed);
    EXPECT(runner, saved.channels[0].words_remaining == 2U);
    const auto reject_without_mutation = [&](const auto& rejected) {
        const auto before_reject = dma.snapshot();
        EXPECT(runner, dma.restore(rejected) == SocStatus::snapshot_mismatch);
        EXPECT(runner, equal_dma_snapshot(before_reject, dma.snapshot()));
    };
    auto malformed = saved;
    malformed.channels[0].active = true;
    malformed.channels[0].completed = true;
    reject_without_mutation(malformed);
    malformed = saved;
    malformed.channels[0].current_address |= 1U;
    reject_without_mutation(malformed);
    malformed = saved;
    malformed.master_irq_line = !malformed.master_irq_line;
    reject_without_mutation(malformed);
    malformed = saved;
    malformed.channels[6U].last_status = static_cast<SocStatus>(0xffU);
    reject_without_mutation(malformed);
    malformed = saved;
    malformed.interrupt_control |= 1U << 14U;
    reject_without_mutation(malformed);

    InterruptController otc_interrupts;
    DmaController otc(&otc_interrupts);
    Memory otc_memory{};
    Device otc_device{};
    otc.set_callbacks(6U, bus_for(otc_memory), device_for(otc_device));
    set_priority(otc, 6U, 0U, true);
    otc.write_base_address(6U, 0x1700U);
    otc.write_block_control(6U, 2U);
    otc.write_channel_control(6U, linear_control(false, true, false, 0U));
    EXPECT(runner, otc.run(DmaRunBudget{1U, 1U}).words_transferred == 1U);
    const auto valid_otc = otc.snapshot();
    EXPECT(runner, otc.restore(valid_otc) == SocStatus::ok);
    auto invalid_otc = valid_otc;
    invalid_otc.channels[6U].registers.channel_control |= 1U;
    EXPECT(runner, otc.restore(invalid_otc) == SocStatus::snapshot_mismatch);
    EXPECT(runner, equal_dma_snapshot(valid_otc, otc.snapshot()));

    InterruptController list_interrupts;
    DmaController list(&list_interrupts);
    Memory list_memory{};
    Device list_device{};
    list.set_callbacks(2U, bus_for(list_memory), device_for(list_device));
    set_priority(list, 2U, 0U, true);
    list_memory.words[memory_index(0x1800U)] = (1U << 24U) | 0x00ff'ffffU;
    list_memory.words[memory_index(0x1804U)] = 0xabcdU;
    list.write_base_address(2U, 0x1800U);
    list.write_channel_control(2U, linear_control(true, false, false, 2U));
    EXPECT(runner, list.run(DmaRunBudget{4U, 1U}).cpu_cycles == 1U);
    const auto valid_list = list.snapshot();
    EXPECT(runner, list.restore(valid_list) == SocStatus::ok);
    auto duplicate_node = valid_list;
    duplicate_node.channels[2U].visited_nodes.push_back(0x1800U);
    duplicate_node.channels[2U].linked_nodes = 2U;
    EXPECT(runner,
           list.restore(duplicate_node) == SocStatus::snapshot_mismatch);
    EXPECT(runner, equal_dma_snapshot(valid_list, list.snapshot()));
    auto excessive_words = valid_list;
    excessive_words.channels[2U].linked_words = 1'000'001U;
    EXPECT(runner,
           list.restore(excessive_words) == SocStatus::snapshot_mismatch);
    EXPECT(runner, equal_dma_snapshot(valid_list, list.snapshot()));

    InterruptController empty_list_interrupts;
    DmaController empty_list(&empty_list_interrupts);
    Memory empty_list_memory{};
    Device empty_list_device{};
    empty_list.set_callbacks(2U, bus_for(empty_list_memory),
                             device_for(empty_list_device));
    set_priority(empty_list, 2U, 0U, true);
    empty_list_memory.words[memory_index(0x1a00U)] = 0x00ff'ffffU;
    empty_list.write_base_address(2U, 0x1a00U);
    empty_list.write_channel_control(2U,
                                     linear_control(true, false, false, 2U));
    const auto empty_report = empty_list.run(DmaRunBudget{1U, 2U});
    EXPECT(runner, empty_report.status == SocStatus::ok);
    EXPECT(runner, empty_report.words_transferred == 0U &&
                       empty_report.cpu_cycles == 1U);
    const auto completed_empty_list = empty_list.snapshot();
    EXPECT(runner, empty_list.restore(completed_empty_list) == SocStatus::ok);
}

void test_soc_snapshot(TestRunner& runner) {
    SystemOnChip first;
    SystemOnChip second;
    first.interrupts().write_mask(
        clean_aot::soc::interrupt_bit(InterruptSource::counter2));
    first.counters().write_mode(2U, 0x0200U);
    first.counters().write_target(2U, 0x1234U);
    first.counters().advance_cpu(CycleDelta{11U, 7U}, nullptr);
    const auto saved = first.snapshot();
    EXPECT(runner, second.restore(saved) == SocStatus::ok);
    EXPECT(runner, equal_snapshot(first.snapshot(), second.snapshot()));
    first.counters().advance_cpu(CycleDelta{29U, 11U}, nullptr);
    second.counters().advance_cpu(CycleDelta{29U, 11U}, nullptr);
    EXPECT(runner, equal_snapshot(first.snapshot(), second.snapshot()));
    EXPECT(runner, second.restore(saved) == SocStatus::ok);
    EXPECT(runner, second.counters().read_target(2U) == 0x1234U);

    const auto before_atomic_reject = second.snapshot();
    auto malformed = before_atomic_reject;
    malformed.interrupts.status =
        clean_aot::soc::interrupt_bit(InterruptSource::graphics);
    malformed.dma.priority_control ^= 0xf000'0000U;
    malformed.counters[2U].phase_denominator = 0U;
    EXPECT(runner, second.restore(malformed) == SocStatus::snapshot_mismatch);
    EXPECT(runner, equal_snapshot(before_atomic_reject, second.snapshot()));

    SystemOnChip reset_chip;
    SystemOnChip pristine;
    Memory reset_memory{};
    Device reset_device{};
    CounterLog reset_log{};
    reset_chip.dma().set_callbacks(0U, bus_for(reset_memory),
                                   device_for(reset_device));
    reset_chip.counters().set_callbacks(
        CounterCallbacks{&reset_log, record_counter_event});
    reset_chip.interrupts().raise(InterruptSource::serial);
    reset_chip.dma().write_priority_control(0xffff'ffffU);
    reset_chip.counters().write_count(1U, 0xbeefU);
    reset_chip.reset();
    EXPECT(runner, equal_snapshot(reset_chip.snapshot(), pristine.snapshot()));

    set_priority(reset_chip.dma(), 0U, 0U, true);
    reset_memory.words[memory_index(0x1900U)] = 0xcafeU;
    reset_chip.dma().write_base_address(0U, 0x1900U);
    reset_chip.dma().write_block_control(0U, 1U);
    reset_chip.dma().write_channel_control(
        0U, linear_control(true, false, false, 0U));
    EXPECT(runner,
           reset_chip.dma().run(DmaRunBudget{1U, 2U}).status == SocStatus::ok);
    EXPECT(runner,
           reset_device.count == 1U && reset_device.values[0] == 0xcafeU);
    reset_chip.counters().write_target(0U, 1U);
    reset_chip.counters().write_mode(0U, 0x0018U);
    EXPECT(runner, reset_chip.counters().advance_cpu(CycleDelta::whole(1U),
                                                     nullptr) == SocStatus::ok);
    EXPECT(runner, reset_log.count == 1U);
    EXPECT(runner, reset_chip.interrupts().pending(InterruptSource::counter0));
}

} // namespace

int main() {
    TestRunner runner;
    test_interrupts(runner);
    test_interrupt_random_model(runner);
    test_counters(runner);
    test_counter_adversarial_state(runner);
    test_counter_random_differential(runner);
    test_native_timing_is_not_retirement_scaled(runner);
    test_dma_registers_and_linear(runner);
    test_dma_dpcr_endpoints_and_reset(runner);
    test_dma_reentrant_callbacks_fail_closed(runner);
    test_dma_arbitration_and_edges(runner);
    test_dma_start_windows_and_irq_edges(runner);
    test_dma_address_chop_otc(runner);
    test_dma_chopping_chunk_invariance(runner);
    test_dma_linked_and_errors(runner);
    test_dma_fail_closed_modes_and_flags(runner);
    test_dma_snapshot(runner);
    test_soc_snapshot(runner);
    std::cout << "checks=" << runner.checks << " failures=" << runner.failures
              << '\n';
    return runner.failures == 0U ? 0 : 1;
}
