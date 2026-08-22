#include "clean_aot_soc.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

namespace clean_aot::soc {
namespace {

constexpr std::uint16_t kCounterConfigMask = 0x03ffU;
constexpr std::uint16_t kCounterIrqLevelBit = 0x0400U;
constexpr std::uint16_t kCounterTargetSeenBit = 0x0800U;
constexpr std::uint16_t kCounterOverflowSeenBit = 0x1000U;
constexpr std::uint16_t kCounterSyncEnableBit = 0x0001U;
constexpr std::uint16_t kCounterResetTargetBit = 0x0008U;
constexpr std::uint16_t kCounterIrqTargetBit = 0x0010U;
constexpr std::uint16_t kCounterIrqOverflowBit = 0x0020U;
constexpr std::uint16_t kCounterIrqRepeatBit = 0x0040U;
constexpr std::uint16_t kCounterIrqToggleBit = 0x0080U;

constexpr std::uint32_t kDmaEnableBit = 1U << 24U;
constexpr std::uint32_t kDmaTriggerBit = 1U << 28U;
constexpr std::uint32_t kDmaStepBit = 1U << 1U;
constexpr std::uint32_t kDmaChopBit = 1U << 8U;
constexpr std::uint32_t kDmaSyncShift = 9U;
constexpr std::uint32_t kDmaDirectionBit = 1U;
constexpr std::uint32_t kDmaChannelMask = 0x7fU;
constexpr std::uint32_t kDmaBusErrorBit = 1U << 15U;
constexpr std::uint32_t kDmaInterruptControlMask = 0x00ff'807fU;
constexpr std::uint32_t kDmaDpcrReset = 0x0765'4321U;
constexpr std::uint32_t kDmaEndAddress = 0x00ff'ffffU;
constexpr std::uint32_t kDmaControlMask =
    0x0000'0003U | 0x0000'0100U | 0x0000'0600U | 0x0007'0000U | 0x0070'0000U |
    kDmaEnableBit | kDmaTriggerBit;

[[nodiscard]] std::uint64_t gcd_u64(std::uint64_t left,
                                    std::uint64_t right) noexcept {
    while (right != 0U) {
        const std::uint64_t remainder = left % right;
        left = right;
        right = remainder;
    }
    return left;
}

[[nodiscard]] bool valid_soc_status(const SocStatus status) noexcept {
    switch (status) {
    case SocStatus::ok:
    case SocStatus::idle:
    case SocStatus::invalid_argument:
    case SocStatus::malformed_callback:
    case SocStatus::callback_rejected:
    case SocStatus::bus_fault:
    case SocStatus::device_fault:
    case SocStatus::invalid_mode:
    case SocStatus::traversal_limit:
    case SocStatus::linked_list_loop:
    case SocStatus::arithmetic_overflow:
    case SocStatus::snapshot_mismatch:
        return true;
    }
    return false;
}

[[nodiscard]] bool checked_add(const std::uint64_t left,
                               const std::uint64_t right,
                               std::uint64_t& result) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_mul(const std::uint64_t left,
                               const std::uint64_t right,
                               std::uint64_t& result) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

struct Fraction {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 1;
};

[[nodiscard]] bool normalize(Fraction& value) noexcept {
    if (value.denominator == 0U) {
        return false;
    }
    if (value.numerator == 0U) {
        value.denominator = 1U;
        return true;
    }
    const std::uint64_t divisor = gcd_u64(value.numerator, value.denominator);
    value.numerator /= divisor;
    value.denominator /= divisor;
    return true;
}

[[nodiscard]] bool add_fraction(Fraction& left, Fraction right) noexcept {
    if (!normalize(left) || !normalize(right)) {
        return false;
    }
    const std::uint64_t divisor = gcd_u64(left.denominator, right.denominator);
    const std::uint64_t left_multiplier = right.denominator / divisor;
    const std::uint64_t right_multiplier = left.denominator / divisor;
    std::uint64_t new_denominator = 0;
    if (!checked_mul(left.denominator, left_multiplier, new_denominator)) {
        return false;
    }
    std::uint64_t left_numerator = 0;
    std::uint64_t right_numerator = 0;
    if (!checked_mul(left.numerator, left_multiplier, left_numerator) ||
        !checked_mul(right.numerator, right_multiplier, right_numerator)) {
        return false;
    }
    std::uint64_t new_numerator = 0;
    if (!checked_add(left_numerator, right_numerator, new_numerator)) {
        return false;
    }
    left = Fraction{new_numerator, new_denominator};
    return normalize(left);
}

[[nodiscard]] std::uint64_t take_whole(Fraction& value) noexcept {
    const std::uint64_t whole = value.numerator / value.denominator;
    value.numerator %= value.denominator;
    if (value.numerator == 0U) {
        value.denominator = 1U;
    } else {
        const std::uint64_t divisor =
            gcd_u64(value.numerator, value.denominator);
        value.numerator /= divisor;
        value.denominator /= divisor;
    }
    return whole;
}

[[nodiscard]] CounterClockSource counter_clock_source(
    const std::size_t index, const std::uint16_t mode) noexcept {
    const std::uint8_t selector = static_cast<std::uint8_t>((mode >> 8U) & 3U);
    if (index == 0U) {
        return (selector == 1U || selector == 3U)
                   ? CounterClockSource::dot_clock
                   : CounterClockSource::system;
    }
    if (index == 1U) {
        return (selector == 1U || selector == 3U) ? CounterClockSource::hblank
                                                  : CounterClockSource::system;
    }
    if (selector >= 2U) {
        return CounterClockSource::system_div8;
    }
    return CounterClockSource::system;
}

[[nodiscard]] CounterSyncMode counter_sync_mode(
    const std::uint16_t mode) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>((mode >> 1U) & 3U);
    return static_cast<CounterSyncMode>(value);
}

[[nodiscard]] bool is_rising_sync_edge(const std::size_t index,
                                       const CounterEdge edge) noexcept {
    if (index == 2U) {
        return false;
    }
    if (index == 0U) {
        return edge == CounterEdge::hblank_begin;
    }
    if (index == 1U) {
        return edge == CounterEdge::vblank_begin;
    }
    return edge == CounterEdge::vblank_begin;
}

[[nodiscard]] bool is_falling_sync_edge(const std::size_t index,
                                        const CounterEdge edge) noexcept {
    if (index == 2U) {
        return false;
    }
    if (index == 0U) {
        return edge == CounterEdge::hblank_end;
    }
    if (index == 1U) {
        return edge == CounterEdge::vblank_end;
    }
    return edge == CounterEdge::vblank_end;
}

[[nodiscard]] InterruptSource counter_interrupt_source(
    const std::size_t index) noexcept {
    return static_cast<InterruptSource>(
        static_cast<std::uint8_t>(InterruptSource::counter0) +
        static_cast<std::uint8_t>(index));
}

[[nodiscard]] std::uint32_t dma_irq_bit(const std::size_t index) noexcept {
    return std::uint32_t{1} << static_cast<std::uint32_t>(index);
}

} // namespace

void InterruptController::reset() noexcept {
    status_ = 0U;
    mask_ = 0U;
}

void InterruptController::raise(const InterruptSource source) noexcept {
    raise_mask(interrupt_bit(source));
}

void InterruptController::raise_mask(const std::uint32_t bits) noexcept {
    status_ |= bits & kInterruptAllMask;
}

void InterruptController::acknowledge(const InterruptSource source) noexcept {
    acknowledge_mask(interrupt_bit(source));
}

void InterruptController::acknowledge_mask(const std::uint32_t bits) noexcept {
    status_ &= ~(bits & kInterruptAllMask);
}

void InterruptController::write_status(const std::uint32_t value) noexcept {
    // The architectural write is zero-to-clear: one bits preserve status.
    status_ &= value & kInterruptAllMask;
}

void InterruptController::write_mask(const std::uint32_t value) noexcept {
    mask_ = value & kInterruptAllMask;
}

std::uint32_t InterruptController::status() const noexcept {
    return status_;
}

std::uint32_t InterruptController::mask() const noexcept {
    return mask_;
}

bool InterruptController::pending(const InterruptSource source) const noexcept {
    return (status_ & interrupt_bit(source)) != 0U;
}

bool InterruptController::enabled(const InterruptSource source) const noexcept {
    return (mask_ & interrupt_bit(source)) != 0U;
}

bool InterruptController::cpu_line() const noexcept {
    return (status_ & mask_) != 0U;
}

InterruptSnapshot InterruptController::snapshot() const noexcept {
    return InterruptSnapshot{status_, mask_};
}

SocStatus InterruptController::validate_snapshot(
    const InterruptSnapshot& state) const noexcept {
    if ((state.status & ~kInterruptAllMask) != 0U ||
        (state.mask & ~kInterruptAllMask) != 0U) {
        return SocStatus::snapshot_mismatch;
    }
    return SocStatus::ok;
}

SocStatus InterruptController::restore(
    const InterruptSnapshot& state) noexcept {
    const SocStatus validation = validate_snapshot(state);
    if (validation != SocStatus::ok) {
        return validation;
    }
    status_ = state.status;
    mask_ = state.mask;
    return SocStatus::ok;
}

RootCounters::RootCounters(InterruptController* const interrupt_sink) noexcept
    : interrupt_sink_(interrupt_sink) {
    reset();
}

void RootCounters::reset() noexcept {
    for (CounterState& counter : state_storage_) {
        counter = CounterState{};
        counter.phase_denominator = 1U;
    }
}

void RootCounters::set_callbacks(const CounterCallbacks callbacks) noexcept {
    callbacks_ = callbacks;
}

RootCounters::CounterState* RootCounters::state(
    const std::size_t index) noexcept {
    return index < kCount ? &state_storage_[index] : nullptr;
}

const RootCounters::CounterState* RootCounters::state(
    const std::size_t index) const noexcept {
    return index < kCount ? &state_storage_[index] : nullptr;
}

std::uint16_t RootCounters::read_count(const std::size_t index) const noexcept {
    const CounterState* const counter = state(index);
    return counter == nullptr ? 0U : counter->count;
}

std::uint16_t RootCounters::read_mode(const std::size_t index) noexcept {
    CounterState* const counter = state(index);
    if (counter == nullptr) {
        return 0U;
    }
    std::uint16_t value = counter->mode;
    if (!counter->irq_level) {
        value |= kCounterIrqLevelBit;
    }
    if (counter->target_seen) {
        value |= kCounterTargetSeenBit;
    }
    if (counter->overflow_seen) {
        value |= kCounterOverflowSeenBit;
    }
    counter->target_seen = false;
    counter->overflow_seen = false;
    return value;
}

std::uint16_t RootCounters::read_target(
    const std::size_t index) const noexcept {
    const CounterState* const counter = state(index);
    return counter == nullptr ? 0U : counter->target;
}

SocStatus RootCounters::write_count(const std::size_t index,
                                    const std::uint16_t value) noexcept {
    CounterState* const counter = state(index);
    if (counter == nullptr) {
        return SocStatus::invalid_argument;
    }
    counter->count = value;
    counter->phase_numerator = 0U;
    counter->phase_denominator = 1U;
    return SocStatus::ok;
}

SocStatus RootCounters::write_mode(const std::size_t index,
                                   const std::uint16_t value) noexcept {
    CounterState* const counter = state(index);
    if (counter == nullptr) {
        return SocStatus::invalid_argument;
    }
    const std::uint16_t config = value & kCounterConfigMask;
    if (counter_sync_mode(config) == CounterSyncMode::reserved) {
        return SocStatus::invalid_mode;
    }
    counter->mode = config;
    counter->count = 0U;
    counter->phase_numerator = 0U;
    counter->phase_denominator = 1U;
    counter->synchronization_level = false;
    counter->synchronization_released = false;
    counter->irq_armed = true;
    counter->irq_level = false;
    counter->target_seen = false;
    counter->overflow_seen = false;
    if ((config & kCounterSyncEnableBit) == 0U) {
        counter->running = true;
    } else {
        if (index == 2U) {
            const CounterSyncMode sync = counter_sync_mode(config);
            counter->running =
                sync == CounterSyncMode::reset_at_gate ||
                sync == CounterSyncMode::reset_at_gate_pause_outside;
        } else {
            switch (counter_sync_mode(config)) {
            case CounterSyncMode::pause_during_gate:
            case CounterSyncMode::reset_at_gate:
                counter->running = true;
                break;
            case CounterSyncMode::reset_at_gate_pause_outside:
            case CounterSyncMode::pause_until_gate:
                counter->running = false;
                break;
            case CounterSyncMode::reserved:
                return SocStatus::invalid_mode;
            }
        }
    }
    return SocStatus::ok;
}

SocStatus RootCounters::write_target(const std::size_t index,
                                     const std::uint16_t value) noexcept {
    CounterState* const counter = state(index);
    if (counter == nullptr) {
        return SocStatus::invalid_argument;
    }
    counter->target = value;
    return SocStatus::ok;
}

void RootCounters::emit_event(const std::size_t index,
                              const CounterEventKind kind,
                              CounterAdvanceReport& report) noexcept {
    CounterState& counter = state_storage_[index];
    if (kind == CounterEventKind::target) {
        counter.target_seen = true;
    } else if (kind == CounterEventKind::overflow) {
        counter.overflow_seen = true;
    }

    const bool irq_enabled =
        kind == CounterEventKind::target
            ? (counter.mode & kCounterIrqTargetBit) != 0U
            : (counter.mode & kCounterIrqOverflowBit) != 0U;
    const bool irq_requested = irq_enabled && counter.irq_armed;
    bool raise_interrupt = false;
    bool event_irq_level = false;
    if (irq_requested) {
        if ((counter.mode & kCounterIrqToggleBit) != 0U) {
            counter.irq_level = !counter.irq_level;
            event_irq_level = counter.irq_level;
            raise_interrupt = event_irq_level;
        } else {
            counter.irq_level = true;
            event_irq_level = true;
            raise_interrupt = true;
        }
        if ((counter.mode & kCounterIrqRepeatBit) == 0U) {
            counter.irq_armed = false;
        }
    }

    if (raise_interrupt && interrupt_sink_ != nullptr) {
        interrupt_sink_->raise(counter_interrupt_source(index));
    }

    if (report.events != std::numeric_limits<std::uint32_t>::max()) {
        ++report.events;
    }
    if (counter.event_ordinal != std::numeric_limits<std::uint64_t>::max()) {
        ++counter.event_ordinal;
    }
    const CounterEvent event{static_cast<std::uint8_t>(index), kind,
                             raise_interrupt, event_irq_level,
                             counter.event_ordinal};
    if (callbacks_.on_event != nullptr) {
        callbacks_.on_event(callbacks_.context, event);
    }
    if (irq_requested && (counter.mode & kCounterIrqToggleBit) == 0U) {
        counter.irq_level = false;
    }
}

SocStatus RootCounters::add_ticks(const std::size_t index, std::uint64_t ticks,
                                  CounterAdvanceReport& report) noexcept {
    CounterState& counter = state_storage_[index];
    if (!counter.running || ticks == 0U) {
        return SocStatus::ok;
    }

    constexpr std::uint64_t modulus = 0x1'0000ULL;
    const bool target_reset = (counter.mode & kCounterResetTargetBit) != 0U;
    const bool irq_state_observable =
        counter.irq_armed &&
        (counter.mode & (kCounterIrqTargetBit | kCounterIrqOverflowBit)) != 0U;

    if (callbacks_.on_event == nullptr && !irq_state_observable) {
        const std::uint64_t original_ticks = ticks;
        if (original_ticks >
            std::numeric_limits<std::uint64_t>::max() - report.ticks[index]) {
            return SocStatus::arithmetic_overflow;
        }
        const std::uint64_t current = counter.count;
        const std::uint64_t to_overflow = modulus - current;
        const std::uint64_t to_target =
            counter.target > counter.count
                ? static_cast<std::uint64_t>(counter.target) - current
                : (modulus - current) + counter.target;

        auto record_quiet_events =
            [&](const CounterEventKind kind,
                const std::uint64_t occurrences) noexcept {
                if (occurrences == 0U) {
                    return;
                }
                if (kind == CounterEventKind::target) {
                    counter.target_seen = true;
                } else {
                    counter.overflow_seen = true;
                }
                if (occurrences >
                    std::numeric_limits<std::uint32_t>::max() - report.events) {
                    report.events = std::numeric_limits<std::uint32_t>::max();
                } else {
                    report.events += static_cast<std::uint32_t>(occurrences);
                }
                if (occurrences > std::numeric_limits<std::uint64_t>::max() -
                                      counter.event_ordinal) {
                    counter.event_ordinal =
                        std::numeric_limits<std::uint64_t>::max();
                } else {
                    counter.event_ordinal += occurrences;
                }
            };

        if (target_reset) {
            const std::uint64_t first_event = std::min(to_overflow, to_target);
            if (original_ticks < first_event) {
                counter.count =
                    static_cast<std::uint16_t>(current + original_ticks);
                report.ticks[index] += original_ticks;
                return SocStatus::ok;
            }

            ticks -= first_event;
            if (to_target == first_event) {
                record_quiet_events(CounterEventKind::target, 1U);
            }
            if (to_overflow == first_event) {
                record_quiet_events(CounterEventKind::overflow, 1U);
            }
            counter.count = 0U;

            const std::uint64_t repeat_period =
                counter.target == 0U
                    ? modulus
                    : static_cast<std::uint64_t>(counter.target);
            const std::uint64_t repeats = ticks / repeat_period;
            const std::uint64_t remainder = ticks % repeat_period;
            if (counter.target == 0U) {
                record_quiet_events(CounterEventKind::target, repeats);
                record_quiet_events(CounterEventKind::overflow, repeats);
            } else {
                record_quiet_events(CounterEventKind::target, repeats);
            }
            counter.count = static_cast<std::uint16_t>(remainder);
            report.ticks[index] += original_ticks;
            return SocStatus::ok;
        }

        const std::uint64_t target_events =
            original_ticks >= to_target
                ? 1U + (original_ticks - to_target) / modulus
                : 0U;
        const std::uint64_t overflow_events =
            original_ticks >= to_overflow
                ? 1U + (original_ticks - to_overflow) / modulus
                : 0U;
        record_quiet_events(CounterEventKind::target, target_events);
        record_quiet_events(CounterEventKind::overflow, overflow_events);
        const std::uint64_t remainder = original_ticks % modulus;
        counter.count =
            static_cast<std::uint16_t>((current + remainder) % modulus);
        report.ticks[index] += original_ticks;
        return SocStatus::ok;
    }

    while (ticks != 0U) {
        const std::uint64_t current = counter.count;
        const std::uint64_t to_overflow = modulus - current;
        const std::uint64_t target = counter.target;
        const std::uint64_t to_target =
            target > current ? target - current : (modulus - current) + target;
        const std::uint64_t step =
            std::min(ticks, std::min(to_overflow, to_target));
        if (step >
            std::numeric_limits<std::uint64_t>::max() - report.ticks[index]) {
            return SocStatus::arithmetic_overflow;
        }
        counter.count = static_cast<std::uint16_t>((current + step) % modulus);
        ticks -= step;
        report.ticks[index] += step;

        const bool reached_target = step == to_target;
        const bool reached_overflow = step == to_overflow;
        if (reached_target) {
            emit_event(index, CounterEventKind::target, report);
            if (target_reset) {
                counter.count = 0U;
            }
        }
        if (reached_overflow) {
            emit_event(index, CounterEventKind::overflow, report);
            counter.count = 0U;
        }
    }
    return SocStatus::ok;
}

SocStatus RootCounters::advance_cpu(
    const CycleDelta& delta, CounterAdvanceReport* const report) noexcept {
    CounterAdvanceReport prepared{};
    prepared.cpu_numerator = delta.numerator;
    prepared.cpu_denominator = delta.denominator;
    if (delta.denominator == 0U) {
        prepared.status = SocStatus::invalid_argument;
        if (report != nullptr) {
            *report = prepared;
        }
        return prepared.status;
    }

    Fraction incoming{delta.numerator, delta.denominator};
    if (!normalize(incoming)) {
        prepared.status = SocStatus::arithmetic_overflow;
        if (report != nullptr) {
            *report = prepared;
        }
        return prepared.status;
    }

    std::array<Fraction, kCount> next_phases{};
    std::array<std::uint64_t, kCount> pending_ticks{};
    for (std::size_t index = 0U; index < kCount; ++index) {
        const CounterState& counter = state_storage_[index];
        next_phases[index] =
            Fraction{counter.phase_numerator, counter.phase_denominator};
        if (!counter.running) {
            // A synchronized pause freezes the fractional clock as well as
            // the visible count; paused native time must not be discarded as
            // a synthetic tick when the gate opens again.
            continue;
        }
        const CounterClockSource source =
            counter_clock_source(index, counter.mode);
        if (source != CounterClockSource::system &&
            source != CounterClockSource::system_div8) {
            continue;
        }

        Fraction contribution = incoming;
        if (source == CounterClockSource::system_div8) {
            const std::uint64_t divisor = gcd_u64(contribution.numerator, 8U);
            contribution.numerator /= divisor;
            const std::uint64_t scale = 8U / divisor;
            if (!checked_mul(contribution.denominator, scale,
                             contribution.denominator)) {
                prepared.status = SocStatus::arithmetic_overflow;
                if (report != nullptr) {
                    *report = prepared;
                }
                return prepared.status;
            }
        }
        if (!add_fraction(next_phases[index], contribution)) {
            prepared.status = SocStatus::arithmetic_overflow;
            if (report != nullptr) {
                *report = prepared;
            }
            return prepared.status;
        }
        pending_ticks[index] = take_whole(next_phases[index]);
    }

    CounterAdvanceReport local{};
    CounterAdvanceReport& result = report == nullptr ? local : *report;
    result = prepared;
    for (std::size_t index = 0U; index < kCount; ++index) {
        state_storage_[index].phase_numerator = next_phases[index].numerator;
        state_storage_[index].phase_denominator =
            next_phases[index].denominator;
    }
    for (std::size_t index = 0U; index < kCount; ++index) {
        const SocStatus status = add_ticks(index, pending_ticks[index], result);
        if (status != SocStatus::ok) {
            result.status = status;
            return status;
        }
    }
    result.status = SocStatus::ok;
    return result.status;
}

SocStatus RootCounters::synchronization_edge(
    const std::size_t index, const CounterEdge edge,
    CounterAdvanceReport& report) noexcept {
    CounterState& counter = state_storage_[index];
    if ((counter.mode & kCounterSyncEnableBit) == 0U) {
        return SocStatus::ok;
    }
    const bool rising = is_rising_sync_edge(index, edge);
    const bool falling = is_falling_sync_edge(index, edge);
    if (!rising && !falling) {
        return SocStatus::ok;
    }
    const CounterSyncMode sync = counter_sync_mode(counter.mode);
    if (sync == CounterSyncMode::pause_until_gate &&
        counter.synchronization_released) {
        return SocStatus::ok;
    }
    bool released_on_edge = false;
    switch (sync) {
    case CounterSyncMode::pause_during_gate:
        if (rising) {
            counter.running = false;
        } else if (falling) {
            counter.running = true;
        }
        break;
    case CounterSyncMode::reset_at_gate:
        if (rising) {
            counter.count = 0U;
            counter.phase_numerator = 0U;
            counter.phase_denominator = 1U;
            counter.running = true;
        }
        break;
    case CounterSyncMode::reset_at_gate_pause_outside:
        if (rising) {
            counter.count = 0U;
            counter.phase_numerator = 0U;
            counter.phase_denominator = 1U;
            counter.running = true;
        } else if (falling) {
            counter.running = false;
        }
        break;
    case CounterSyncMode::pause_until_gate:
        if (rising) {
            counter.running = true;
            counter.synchronization_released = true;
            released_on_edge = true;
        }
        break;
    case CounterSyncMode::reserved:
        return SocStatus::invalid_mode;
    }

    if (rising) {
        counter.synchronization_level = !released_on_edge;
        if (report.events != std::numeric_limits<std::uint32_t>::max()) {
            ++report.events;
        }
        if (counter.event_ordinal !=
            std::numeric_limits<std::uint64_t>::max()) {
            ++counter.event_ordinal;
        }
        const CounterEvent event{static_cast<std::uint8_t>(index),
                                 CounterEventKind::synchronization, false,
                                 counter.irq_level, counter.event_ordinal};
        if (callbacks_.on_event != nullptr) {
            callbacks_.on_event(callbacks_.context, event);
        }
    } else if (falling) {
        counter.synchronization_level = false;
    }
    return SocStatus::ok;
}

SocStatus RootCounters::input_edge(
    const CounterEdge edge, CounterAdvanceReport* const report) noexcept {
    CounterAdvanceReport local{};
    CounterAdvanceReport& result = report == nullptr ? local : *report;
    result = CounterAdvanceReport{};
    result.cpu_denominator = 1U;

    for (std::size_t index = 0U; index < kCount; ++index) {
        const SocStatus sync_status = synchronization_edge(index, edge, result);
        if (sync_status != SocStatus::ok) {
            result.status = sync_status;
            return sync_status;
        }
        const CounterClockSource source =
            counter_clock_source(index, state_storage_[index].mode);
        const bool clock_edge = (source == CounterClockSource::hblank &&
                                 edge == CounterEdge::hblank_begin) ||
                                (source == CounterClockSource::dot_clock &&
                                 edge == CounterEdge::dot_clock);
        if (clock_edge) {
            const SocStatus tick_status = add_ticks(index, 1U, result);
            if (tick_status != SocStatus::ok) {
                result.status = tick_status;
                return tick_status;
            }
        }
    }
    result.status = SocStatus::ok;
    return result.status;
}

bool RootCounters::running(const std::size_t index) const noexcept {
    const CounterState* const counter = state(index);
    return counter != nullptr && counter->running;
}

bool RootCounters::irq_level(const std::size_t index) const noexcept {
    const CounterState* const counter = state(index);
    return counter != nullptr && counter->irq_level;
}

CounterSnapshot RootCounters::snapshot(const std::size_t index) const noexcept {
    const CounterState* const counter = state(index);
    if (counter == nullptr) {
        return CounterSnapshot{};
    }
    std::uint16_t mode = counter->mode;
    if (!counter->irq_level) {
        mode |= kCounterIrqLevelBit;
    }
    if (counter->target_seen) {
        mode |= kCounterTargetSeenBit;
    }
    if (counter->overflow_seen) {
        mode |= kCounterOverflowSeenBit;
    }
    return CounterSnapshot{counter->count,
                           counter->target,
                           mode,
                           counter->running,
                           counter->synchronization_level,
                           counter->synchronization_released,
                           counter->irq_armed,
                           counter->irq_level,
                           counter->phase_numerator,
                           counter->phase_denominator,
                           counter->event_ordinal};
}

SocStatus RootCounters::validate_snapshot(
    const std::size_t index,
    const CounterSnapshot& snapshot_state) const noexcept {
    if (index >= kCount || snapshot_state.phase_denominator == 0U ||
        snapshot_state.phase_numerator >= snapshot_state.phase_denominator ||
        gcd_u64(snapshot_state.phase_numerator,
                snapshot_state.phase_denominator) != 1U ||
        (snapshot_state.mode & static_cast<std::uint16_t>(~0x1fffU)) != 0U ||
        ((snapshot_state.mode & kCounterIrqLevelBit) == 0U) !=
            snapshot_state.irq_level ||
        counter_sync_mode(snapshot_state.mode) == CounterSyncMode::reserved) {
        return SocStatus::snapshot_mismatch;
    }

    const std::uint16_t config = snapshot_state.mode & kCounterConfigMask;
    const bool irq_source_enabled =
        (config & (kCounterIrqTargetBit | kCounterIrqOverflowBit)) != 0U;
    if ((!snapshot_state.irq_armed &&
         (((config & kCounterIrqRepeatBit) != 0U) || !irq_source_enabled)) ||
        (snapshot_state.irq_level &&
         (!irq_source_enabled || (config & kCounterIrqToggleBit) == 0U))) {
        return SocStatus::snapshot_mismatch;
    }

    if ((config & kCounterSyncEnableBit) == 0U) {
        if (!snapshot_state.running || snapshot_state.synchronization_level ||
            snapshot_state.synchronization_released) {
            return SocStatus::snapshot_mismatch;
        }
        return SocStatus::ok;
    }

    const CounterSyncMode sync = counter_sync_mode(config);
    if (index == 2U) {
        const bool expected_running =
            sync == CounterSyncMode::reset_at_gate ||
            sync == CounterSyncMode::reset_at_gate_pause_outside;
        if (snapshot_state.running != expected_running ||
            snapshot_state.synchronization_level ||
            snapshot_state.synchronization_released) {
            return SocStatus::snapshot_mismatch;
        }
        return SocStatus::ok;
    }

    bool valid_sync_state = false;
    switch (sync) {
    case CounterSyncMode::pause_during_gate:
        valid_sync_state =
            !snapshot_state.synchronization_released &&
            snapshot_state.running == !snapshot_state.synchronization_level;
        break;
    case CounterSyncMode::reset_at_gate:
        valid_sync_state =
            !snapshot_state.synchronization_released && snapshot_state.running;
        break;
    case CounterSyncMode::reset_at_gate_pause_outside:
        valid_sync_state =
            !snapshot_state.synchronization_released &&
            snapshot_state.running == snapshot_state.synchronization_level;
        break;
    case CounterSyncMode::pause_until_gate:
        valid_sync_state =
            !snapshot_state.synchronization_level &&
            snapshot_state.running == snapshot_state.synchronization_released;
        break;
    case CounterSyncMode::reserved:
        valid_sync_state = false;
        break;
    }
    return valid_sync_state ? SocStatus::ok : SocStatus::snapshot_mismatch;
}

SocStatus RootCounters::restore(
    const std::size_t index, const CounterSnapshot& snapshot_state) noexcept {
    CounterState* const counter = state(index);
    const SocStatus validation = validate_snapshot(index, snapshot_state);
    if (counter == nullptr || validation != SocStatus::ok) {
        return validation;
    }
    counter->count = snapshot_state.count;
    counter->target = snapshot_state.target;
    counter->mode = snapshot_state.mode & kCounterConfigMask;
    counter->running = snapshot_state.running;
    counter->synchronization_level = snapshot_state.synchronization_level;
    counter->synchronization_released = snapshot_state.synchronization_released;
    counter->irq_armed = snapshot_state.irq_armed;
    counter->irq_level = (snapshot_state.mode & kCounterIrqLevelBit) == 0U;
    counter->target_seen = (snapshot_state.mode & kCounterTargetSeenBit) != 0U;
    counter->overflow_seen =
        (snapshot_state.mode & kCounterOverflowSeenBit) != 0U;
    counter->phase_numerator = snapshot_state.phase_numerator;
    counter->phase_denominator = snapshot_state.phase_denominator;
    counter->event_ordinal = snapshot_state.event_ordinal;
    return SocStatus::ok;
}

DmaController::DmaController(InterruptController* const interrupt_sink,
                             const std::uint32_t max_linked_nodes,
                             const std::uint64_t max_linked_words) noexcept
    : interrupt_sink_(interrupt_sink),
      max_linked_nodes_(max_linked_nodes == 0U ? 1U : max_linked_nodes),
      max_linked_words_(max_linked_words == 0U ? 1U : max_linked_words) {
    reset();
}

void DmaController::reset() noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    for (ChannelState& current : channel_storage_) {
        current = ChannelState{};
    }
    channel_storage_[kOrderingTableChannel].registers.channel_control =
        kDmaStepBit;
    priority_control_ = kDmaDpcrReset;
    interrupt_control_ = 0U;
    interrupt_flags_ = 0U;
    master_irq_line_ = false;
}

DmaController::ChannelState* DmaController::channel(
    const std::size_t index) noexcept {
    return index < kChannelCount ? &channel_storage_[index] : nullptr;
}

const DmaController::ChannelState* DmaController::channel(
    const std::size_t index) const noexcept {
    return index < kChannelCount ? &channel_storage_[index] : nullptr;
}

SocStatus DmaController::set_callbacks(
    const std::size_t index, const DmaBusCallbacks bus,
    const DmaDeviceCallbacks device) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    if (index >= kChannelCount) {
        return SocStatus::invalid_argument;
    }
    buses_[index] = bus;
    devices_[index] = device;
    return SocStatus::ok;
}

DmaChannelRegisters DmaController::registers(
    const std::size_t index) const noexcept {
    const ChannelState* const current = channel(index);
    if (current == nullptr) {
        return DmaChannelRegisters{};
    }
    DmaChannelRegisters result = current->registers;
    if (index == kOrderingTableChannel) {
        result.channel_control &= ~(kDmaDirectionBit | (3U << kDmaSyncShift));
        result.channel_control |= kDmaStepBit;
    }
    return result;
}

SocStatus DmaController::write_base_address(
    const std::size_t index, const std::uint32_t value) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    ChannelState* const current = channel(index);
    if (current == nullptr) {
        return SocStatus::invalid_argument;
    }
    current->registers.base_address = value & DmaController::kAddressMask;
    current->current_address =
        current->registers.base_address & ~std::uint32_t{3U};
    return SocStatus::ok;
}

SocStatus DmaController::write_block_control(
    const std::size_t index, const std::uint32_t value) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    ChannelState* const current = channel(index);
    if (current == nullptr) {
        return SocStatus::invalid_argument;
    }
    current->registers.block_control = value;
    return SocStatus::ok;
}

SocStatus DmaController::write_channel_control(
    const std::size_t index, const std::uint32_t value) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    ChannelState* const current = channel(index);
    if (current == nullptr) {
        return SocStatus::invalid_argument;
    }
    const bool request_asserted = current->request_asserted;
    std::uint32_t control = value & kDmaControlMask;
    if (index == kOrderingTableChannel) {
        control &= ~(kDmaDirectionBit | (3U << kDmaSyncShift));
        control |= kDmaStepBit;
    }
    const std::uint32_t old_control = current->registers.channel_control;
    if (current->active && (control & kDmaEnableBit) != 0U &&
        (control & ~kDmaTriggerBit) == (old_control & ~kDmaTriggerBit)) {
        current->registers.channel_control = control;
        return SocStatus::ok;
    }
    current->registers.channel_control = control;
    current->active = false;
    current->completed = false;
    current->error = false;
    current->last_status = SocStatus::idle;
    current->current_address =
        current->registers.base_address & kAddressMask & ~std::uint32_t{3U};
    current->words_transferred = 0U;
    current->words_remaining = 0U;
    current->linked_current = current->current_address;
    current->linked_next = 0U;
    current->linked_nodes = 0U;
    current->linked_words = 0U;
    current->burst_words = 0U;
    current->block_words_remaining = 0U;
    current->request_blocks_remaining = 0U;
    current->cpu_window_remaining = 0U;
    current->window_active = false;
    current->request_asserted = request_asserted;
    current->visited_nodes.clear();
    return SocStatus::ok;
}

SocStatus DmaController::set_request(const std::size_t index,
                                     const bool asserted) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    ChannelState* const current = channel(index);
    if (current == nullptr) {
        return SocStatus::invalid_argument;
    }
    current->request_asserted = asserted;
    return SocStatus::ok;
}

std::uint32_t DmaController::read_priority_control() const noexcept {
    return priority_control_;
}

void DmaController::write_priority_control(const std::uint32_t value) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    priority_control_ = value;
}

std::uint32_t DmaController::read_interrupt_control() const noexcept {
    const std::uint32_t writable =
        interrupt_control_ & kDmaInterruptControlMask;
    const std::uint32_t flags = (interrupt_flags_ & kDmaChannelMask) << 24U;
    const std::uint32_t master = master_irq_line_ ? (1U << 31U) : 0U;
    return writable | flags | master;
}

void DmaController::write_interrupt_control(
    const std::uint32_t value) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    interrupt_flags_ &= ~((value >> 24U) & kDmaChannelMask);
    interrupt_control_ = value & kDmaInterruptControlMask;
    update_irq_line();
}

void DmaController::acknowledge_interrupts(
    const std::uint32_t channel_mask) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    interrupt_flags_ &= ~(channel_mask & kDmaChannelMask);
    update_irq_line();
}

DmaChannelStatus DmaController::channel_status(
    const std::size_t index) const noexcept {
    const ChannelState* const current = channel(index);
    if (current == nullptr) {
        return DmaChannelStatus{};
    }
    return DmaChannelStatus{current->active,
                            current->completed,
                            current->error,
                            current->request_asserted,
                            current->last_status,
                            current->current_address,
                            current->words_transferred,
                            current->words_remaining,
                            current->linked_nodes};
}

bool DmaController::irq_line() const noexcept {
    return master_irq_line_;
}

std::uint32_t DmaController::interrupt_flags() const noexcept {
    return interrupt_flags_ & kDmaChannelMask;
}

DmaSyncMode DmaController::sync_mode(
    const ChannelState& current) const noexcept {
    return static_cast<DmaSyncMode>(
        (current.registers.channel_control >> kDmaSyncShift) & 3U);
}

DmaDirection DmaController::direction(
    const ChannelState& current) const noexcept {
    return (current.registers.channel_control & kDmaDirectionBit) != 0U
               ? DmaDirection::to_device
               : DmaDirection::to_memory;
}

bool DmaController::decrement(const ChannelState& current) const noexcept {
    return (current.registers.channel_control & kDmaStepBit) != 0U;
}

bool DmaController::chopping(const ChannelState& current) const noexcept {
    return (current.registers.channel_control & kDmaChopBit) != 0U;
}

std::uint32_t DmaController::dma_window(
    const ChannelState& current) const noexcept {
    const std::uint32_t field = (current.registers.channel_control >> 16U) & 7U;
    return 1U << field;
}

std::uint32_t DmaController::cpu_window(
    const ChannelState& current) const noexcept {
    const std::uint32_t field = (current.registers.channel_control >> 20U) & 7U;
    return 1U << field;
}

std::uint64_t DmaController::encoded_word_count(
    const ChannelState& current) const noexcept {
    const std::uint64_t block_size = encoded_block_size(current);
    if (sync_mode(current) == DmaSyncMode::manual) {
        return block_size;
    }
    return block_size * encoded_block_count(current);
}

std::uint64_t DmaController::encoded_block_size(
    const ChannelState& current) const noexcept {
    const std::uint32_t encoded = current.registers.block_control & 0xffffU;
    return encoded == 0U ? 0x1'0000ULL : encoded;
}

std::uint32_t DmaController::encoded_block_count(
    const ChannelState& current) const noexcept {
    const std::uint32_t encoded =
        (current.registers.block_control >> 16U) & 0xffffU;
    return encoded == 0U ? 0x1'0000U : encoded;
}

bool DmaController::ready(const std::size_t index) const noexcept {
    const ChannelState* const current = channel(index);
    if (current == nullptr || current->completed || current->error) {
        return false;
    }
    const std::uint32_t nibble =
        (priority_control_ >> static_cast<std::uint32_t>(index * 4U)) & 0xfU;
    if ((nibble & 8U) == 0U) {
        return false;
    }
    const std::uint32_t control = current->registers.channel_control;
    if ((control & kDmaEnableBit) == 0U) {
        return false;
    }
    if (current->active) {
        if (sync_mode(*current) == DmaSyncMode::manual ||
            current->window_active || current->cpu_window_remaining != 0U) {
            return true;
        }
    }
    return current->request_asserted || (control & kDmaTriggerBit) != 0U;
}

std::size_t DmaController::choose_channel() const noexcept {
    std::size_t selected = kChannelCount;
    std::uint32_t selected_priority = 8U;
    for (std::size_t index = 0U; index < kChannelCount; ++index) {
        if (!ready(index)) {
            continue;
        }
        const std::uint32_t nibble =
            (priority_control_ >> static_cast<std::uint32_t>(index * 4U)) &
            0xfU;
        const std::uint32_t priority = nibble & 7U;
        if (selected == kChannelCount || priority < selected_priority ||
            (priority == selected_priority && index > selected)) {
            selected = index;
            selected_priority = priority;
        }
    }
    return selected;
}

bool DmaController::complete(const std::size_t index) noexcept {
    ChannelState& current = channel_storage_[index];
    current.active = false;
    current.completed = true;
    current.error = false;
    current.last_status = SocStatus::ok;
    current.window_active = false;
    current.cpu_window_remaining = 0U;
    current.burst_words = 0U;
    current.registers.channel_control &= ~(kDmaEnableBit | kDmaTriggerBit);
    return assert_channel_flag(index, false);
}

void DmaController::fail(const std::size_t index,
                         const SocStatus status) noexcept {
    ChannelState& current = channel_storage_[index];
    if (status == SocStatus::callback_rejected) {
        // The callback may have rewritten any public DMA state. Rebuild a
        // canonical, snapshot-valid failed channel instead of interpreting or
        // decrementing fields whose invariants no longer belong to this run.
        const bool request_asserted = current.request_asserted;
        current = ChannelState{};
        current.request_asserted = request_asserted;
        current.registers.block_control = 1U;
        current.words_remaining = 1U;
        current.block_words_remaining = 1U;
        if (index == kOrderingTableChannel) {
            current.registers.channel_control = kDmaStepBit;
        }
    }
    current.active = false;
    current.completed = false;
    current.error = true;
    current.last_status = status;
    current.window_active = false;
    current.cpu_window_remaining = 0U;
    current.registers.channel_control &= ~(kDmaEnableBit | kDmaTriggerBit);
    if (status == SocStatus::bus_fault) {
        interrupt_control_ |= kDmaBusErrorBit;
    }
    update_irq_line();
}

bool DmaController::assert_channel_flag(const std::size_t index,
                                        const bool slice) noexcept {
    const std::uint32_t bit = dma_irq_bit(index);
    const bool channel_enabled = (interrupt_control_ & (bit << 16U)) != 0U;
    const bool master_enabled = (interrupt_control_ & (1U << 23U)) != 0U;
    const bool slice_enabled = !slice || (interrupt_control_ & bit) != 0U;
    if (!channel_enabled || !master_enabled || !slice_enabled) {
        update_irq_line();
        return false;
    }
    const bool newly_asserted = (interrupt_flags_ & bit) == 0U;
    interrupt_flags_ |= bit;
    update_irq_line();
    return newly_asserted;
}

void DmaController::update_irq_line(const bool emit_edge) noexcept {
    const bool master_enabled = (interrupt_control_ & (1U << 23U)) != 0U;
    const bool line = (interrupt_control_ & kDmaBusErrorBit) != 0U ||
                      (master_enabled && interrupt_flags_ != 0U);
    const bool rising = !master_irq_line_ && line;
    master_irq_line_ = line;
    if (emit_edge && rising && interrupt_sink_ != nullptr) {
        interrupt_sink_->raise(InterruptSource::dma);
    }
}

std::uint32_t DmaController::step_address(const std::uint32_t address,
                                          const bool decrement_step) noexcept {
    const std::uint32_t masked = address & kAddressMask;
    return decrement_step ? (masked - 4U) & kAddressMask
                          : (masked + 4U) & kAddressMask;
}

SocStatus DmaController::read_bus(const std::size_t index,
                                  const std::uint32_t address,
                                  std::uint32_t& value) noexcept {
    const DmaBusCallbacks& bus = buses_[index];
    if (bus.read_word == nullptr) {
        return SocStatus::malformed_callback;
    }
    callback_mutated_ = false;
    const DmaCallbackStatus status =
        bus.read_word(bus.context, address & kAddressMask, &value);
    if (callback_mutated_) {
        return SocStatus::callback_rejected;
    }
    switch (status) {
    case DmaCallbackStatus::ok:
        return SocStatus::ok;
    case DmaCallbackStatus::not_ready:
        return SocStatus::bus_fault;
    case DmaCallbackStatus::rejected:
        return SocStatus::bus_fault;
    case DmaCallbackStatus::malformed:
        return SocStatus::malformed_callback;
    }
    return SocStatus::malformed_callback;
}

SocStatus DmaController::write_bus(const std::size_t index,
                                   const std::uint32_t address,
                                   const std::uint32_t value) noexcept {
    const DmaBusCallbacks& bus = buses_[index];
    if (bus.write_word == nullptr) {
        return SocStatus::malformed_callback;
    }
    callback_mutated_ = false;
    const DmaCallbackStatus status =
        bus.write_word(bus.context, address & kAddressMask, value);
    if (callback_mutated_) {
        return SocStatus::callback_rejected;
    }
    switch (status) {
    case DmaCallbackStatus::ok:
        return SocStatus::ok;
    case DmaCallbackStatus::not_ready:
        return SocStatus::bus_fault;
    case DmaCallbackStatus::rejected:
        return SocStatus::bus_fault;
    case DmaCallbackStatus::malformed:
        return SocStatus::malformed_callback;
    }
    return SocStatus::malformed_callback;
}

SocStatus DmaController::read_device(const std::size_t index,
                                     std::uint32_t& value) noexcept {
    const DmaDeviceCallbacks& device = devices_[index];
    if (device.read_word == nullptr) {
        return SocStatus::malformed_callback;
    }
    callback_mutated_ = false;
    const DmaCallbackStatus status = device.read_word(device.context, &value);
    if (callback_mutated_) {
        return SocStatus::callback_rejected;
    }
    switch (status) {
    case DmaCallbackStatus::ok:
        return SocStatus::ok;
    case DmaCallbackStatus::not_ready:
        return SocStatus::idle;
    case DmaCallbackStatus::rejected:
        return SocStatus::device_fault;
    case DmaCallbackStatus::malformed:
        return SocStatus::malformed_callback;
    }
    return SocStatus::malformed_callback;
}

SocStatus DmaController::write_device(const std::size_t index,
                                      const std::uint32_t value) noexcept {
    const DmaDeviceCallbacks& device = devices_[index];
    if (device.write_word == nullptr) {
        return SocStatus::malformed_callback;
    }
    callback_mutated_ = false;
    const DmaCallbackStatus status = device.write_word(device.context, value);
    if (callback_mutated_) {
        return SocStatus::callback_rejected;
    }
    switch (status) {
    case DmaCallbackStatus::ok:
        return SocStatus::ok;
    case DmaCallbackStatus::not_ready:
        return SocStatus::idle;
    case DmaCallbackStatus::rejected:
        return SocStatus::device_fault;
    case DmaCallbackStatus::malformed:
        return SocStatus::malformed_callback;
    }
    return SocStatus::malformed_callback;
}

SocStatus DmaController::move_word_between_endpoints(
    const std::size_t index, const std::uint32_t address) noexcept {
    std::uint32_t value = 0U;
    if (direction(channel_storage_[index]) == DmaDirection::to_device) {
        SocStatus status = read_bus(index, address, value);
        if (status != SocStatus::ok) {
            return status;
        }
        return write_device(index, value);
    }
    SocStatus status = read_device(index, value);
    if (status != SocStatus::ok) {
        return status;
    }
    return write_bus(index, address, value);
}

SocStatus DmaController::start_if_needed(const std::size_t index) noexcept {
    ChannelState& current = channel_storage_[index];
    if (!ready(index)) {
        return SocStatus::idle;
    }
    const DmaSyncMode mode = sync_mode(current);
    if (mode == DmaSyncMode::reserved) {
        fail(index, SocStatus::invalid_mode);
        return SocStatus::invalid_mode;
    }
    if (mode == DmaSyncMode::linked_list && index != 2U) {
        fail(index, SocStatus::invalid_mode);
        return SocStatus::invalid_mode;
    }
    if (mode == DmaSyncMode::linked_list &&
        direction(current) != DmaDirection::to_device) {
        fail(index, SocStatus::invalid_mode);
        return SocStatus::invalid_mode;
    }
    if (mode != DmaSyncMode::manual && chopping(current)) {
        fail(index, SocStatus::invalid_mode);
        return SocStatus::invalid_mode;
    }
    if (index == kOrderingTableChannel && mode != DmaSyncMode::manual) {
        fail(index, SocStatus::invalid_mode);
        return SocStatus::invalid_mode;
    }

    if (!current.active) {
        current.active = true;
        current.completed = false;
        current.error = false;
        current.last_status = SocStatus::ok;
        current.current_address =
            current.registers.base_address & kAddressMask & ~std::uint32_t{3U};
        current.words_transferred = 0U;
        current.words_remaining = encoded_word_count(current);
        current.linked_current = current.current_address;
        current.linked_next = 0U;
        current.linked_nodes = 0U;
        current.linked_words = 0U;
        current.burst_words = 0U;
        current.block_words_remaining = 0U;
        current.request_blocks_remaining = 0U;
        current.cpu_window_remaining = 0U;
        current.window_active = true;
        current.visited_nodes.clear();
        if (mode == DmaSyncMode::manual) {
            current.block_words_remaining = current.words_remaining;
        } else if (mode == DmaSyncMode::request) {
            current.block_words_remaining = encoded_block_size(current);
            current.request_blocks_remaining = encoded_block_count(current);
        } else {
            current.words_remaining = 0U;
        }
    } else if (!current.window_active && current.cpu_window_remaining == 0U) {
        current.window_active = true;
    }
    current.registers.channel_control &= ~kDmaTriggerBit;
    return SocStatus::ok;
}

SocStatus DmaController::transfer_linear_word(
    const std::size_t index, DmaRunReport& report,
    const DmaRunBudget& /*budget*/) noexcept {
    ChannelState& current = channel_storage_[index];
    if (current.words_remaining == 0U) {
        report.irq_boundary = complete(index) || report.irq_boundary;
        return SocStatus::ok;
    }
    const SocStatus status =
        move_word_between_endpoints(index, current.current_address);
    if (status != SocStatus::ok) {
        return status;
    }
    current.current_address =
        step_address(current.current_address, decrement(current));
    --current.words_remaining;
    if (current.block_words_remaining != 0U) {
        --current.block_words_remaining;
    }
    ++current.words_transferred;
    ++report.words_transferred;

    const DmaSyncMode mode = sync_mode(current);
    if (mode == DmaSyncMode::request) {
        current.registers.base_address = current.current_address;
        if (current.block_words_remaining == 0U) {
            if (current.request_blocks_remaining != 0U) {
                --current.request_blocks_remaining;
            }
            const std::uint32_t block_size =
                current.registers.block_control & 0xffffU;
            current.registers.block_control =
                block_size |
                ((current.request_blocks_remaining & 0xffffU) << 16U);
            if (current.request_blocks_remaining != 0U) {
                current.block_words_remaining = encoded_block_size(current);
                current.window_active = false;
                report.irq_boundary =
                    assert_channel_flag(index, true) || report.irq_boundary;
            }
        }
    } else if (chopping(current)) {
        current.registers.base_address = current.current_address;
        current.registers.block_control =
            (current.registers.block_control & 0xffff'0000U) |
            static_cast<std::uint32_t>(current.words_remaining & 0xffffU);
    }

    if (current.words_remaining == 0U) {
        report.irq_boundary = complete(index) || report.irq_boundary;
    }
    return SocStatus::ok;
}

SocStatus DmaController::transfer_otc_word(
    const std::size_t index, DmaRunReport& report,
    const DmaRunBudget& /*budget*/) noexcept {
    ChannelState& current = channel_storage_[index];
    if (current.words_remaining == 0U) {
        report.irq_boundary = complete(index) || report.irq_boundary;
        return SocStatus::ok;
    }
    const std::uint32_t next =
        current.words_remaining == 1U
            ? kDmaEndAddress
            : step_address(current.current_address, decrement(current));
    const SocStatus status = write_bus(index, current.current_address, next);
    if (status != SocStatus::ok) {
        return status;
    }
    current.current_address = step_address(current.current_address, true);
    --current.words_remaining;
    current.block_words_remaining = current.words_remaining;
    ++current.words_transferred;
    ++report.words_transferred;
    if (chopping(current)) {
        current.registers.base_address = current.current_address;
        current.registers.block_control =
            (current.registers.block_control & 0xffff'0000U) |
            static_cast<std::uint32_t>(current.words_remaining & 0xffffU);
    }
    if (current.words_remaining == 0U) {
        report.irq_boundary = complete(index) || report.irq_boundary;
    }
    return SocStatus::ok;
}

SocStatus DmaController::transfer_linked_node(
    const std::size_t index, DmaRunReport& report,
    const DmaRunBudget& /*budget*/) noexcept {
    ChannelState& current = channel_storage_[index];
    if (current.words_remaining != 0U) {
        if (current.linked_words >= max_linked_words_) {
            return SocStatus::traversal_limit;
        }
        const SocStatus status =
            move_word_between_endpoints(index, current.current_address);
        if (status != SocStatus::ok) {
            return status;
        }
        current.current_address = step_address(current.current_address, false);
        --current.words_remaining;
        current.block_words_remaining = current.words_remaining;
        ++current.words_transferred;
        ++report.words_transferred;
        ++current.linked_words;
        if (current.words_remaining == 0U) {
            current.registers.base_address = current.linked_next;
            if (current.linked_next == kDmaEndAddress) {
                report.irq_boundary = complete(index) || report.irq_boundary;
            } else {
                current.linked_current = current.linked_next;
                current.current_address = current.linked_current;
                current.window_active = false;
                report.irq_boundary =
                    assert_channel_flag(index, true) || report.irq_boundary;
            }
        }
        return SocStatus::ok;
    }

    if (current.linked_words >= max_linked_words_) {
        return SocStatus::traversal_limit;
    }
    if (current.linked_current == kDmaEndAddress) {
        current.registers.base_address = kDmaEndAddress;
        report.irq_boundary = complete(index) || report.irq_boundary;
        return SocStatus::ok;
    }
    if ((current.linked_current & 3U) != 0U) {
        return SocStatus::invalid_argument;
    }
    if (current.linked_nodes >= max_linked_nodes_) {
        return SocStatus::traversal_limit;
    }
    if (std::find(current.visited_nodes.begin(), current.visited_nodes.end(),
                  current.linked_current) != current.visited_nodes.end()) {
        return SocStatus::linked_list_loop;
    }
    try {
        current.visited_nodes.push_back(current.linked_current);
    } catch (...) {
        return SocStatus::arithmetic_overflow;
    }
    ++current.linked_nodes;

    std::uint32_t header = 0U;
    const SocStatus read_status =
        read_bus(index, current.linked_current, header);
    if (read_status != SocStatus::ok) {
        return read_status;
    }
    const std::uint32_t count = (header >> 24U) & 0xffU;
    const std::uint32_t next = header & DmaController::kAddressMask;
    if (next != kDmaEndAddress && (next & 3U) != 0U) {
        return SocStatus::invalid_argument;
    }
    current.linked_next = next;
    current.current_address = step_address(current.linked_current, false);
    current.words_remaining = count;
    current.block_words_remaining = count;
    if (count == 0U) {
        current.registers.base_address = next;
        if (next == kDmaEndAddress) {
            report.irq_boundary = complete(index) || report.irq_boundary;
        } else {
            current.linked_current = next;
            current.current_address = next;
            current.window_active = false;
            report.irq_boundary =
                assert_channel_flag(index, true) || report.irq_boundary;
        }
    }
    return SocStatus::ok;
}

SocStatus DmaController::transfer_one(const std::size_t index,
                                      DmaRunReport& report,
                                      const DmaRunBudget& budget) noexcept {
    ChannelState& current = channel_storage_[index];
    const DmaSyncMode mode = sync_mode(current);
    if (report.cpu_cycles >= budget.max_cpu_cycles) {
        return SocStatus::idle;
    }

    if (current.cpu_window_remaining != 0U) {
        const std::uint64_t available =
            budget.max_cpu_cycles - report.cpu_cycles;
        const std::uint64_t consumed =
            std::min<std::uint64_t>(current.cpu_window_remaining, available);
        current.cpu_window_remaining -= static_cast<std::uint32_t>(consumed);
        report.cpu_cycles += consumed;
        if (current.cpu_window_remaining == 0U) {
            current.burst_words = 0U;
            current.window_active = true;
        }
        return SocStatus::ok;
    }

    const bool linked_header =
        mode == DmaSyncMode::linked_list && current.words_remaining == 0U;
    if (!linked_header) {
        if (report.words_transferred >= budget.max_words) {
            return SocStatus::idle;
        }
    }

    SocStatus status = SocStatus::ok;
    if (index == kOrderingTableChannel && mode == DmaSyncMode::manual) {
        status = transfer_otc_word(index, report, budget);
    } else if (mode == DmaSyncMode::linked_list) {
        status = transfer_linked_node(index, report, budget);
    } else {
        status = transfer_linear_word(index, report, budget);
    }
    if (status == SocStatus::ok) {
        ++report.cpu_cycles;
        if (current.active && !linked_header && mode == DmaSyncMode::manual &&
            chopping(current)) {
            ++current.burst_words;
            if (current.burst_words == dma_window(current) &&
                current.words_remaining != 0U) {
                current.cpu_window_remaining = cpu_window(current);
                current.window_active = false;
            }
        }
    }
    return status;
}

DmaRunReport DmaController::run(const DmaRunBudget& budget) noexcept {
    DmaRunReport report{};
    if (running_) {
        callback_mutated_ = true;
        report.status = SocStatus::callback_rejected;
        return report;
    }
    running_ = true;
    struct RunGuard final {
        bool& running;
        ~RunGuard() { running = false; }
    } guard{running_};
    if (budget.max_words == 0U || budget.max_cpu_cycles == 0U) {
        report.status = SocStatus::invalid_argument;
        return report;
    }

    while (report.words_transferred < budget.max_words &&
           report.cpu_cycles < budget.max_cpu_cycles) {
        const std::size_t selected = choose_channel();
        if (selected == kChannelCount) {
            report.status =
                report.words_transferred == 0U && report.cpu_cycles == 0U
                    ? SocStatus::idle
                    : SocStatus::ok;
            report.became_idle = true;
            return report;
        }
        report.channel = static_cast<std::uint32_t>(selected);
        const SocStatus start_status = start_if_needed(selected);
        if (start_status != SocStatus::ok) {
            if (start_status == SocStatus::idle) {
                continue;
            }
            report.status = start_status;
            return report;
        }
        const std::uint64_t before_words = report.words_transferred;
        const std::uint64_t before_cycles = report.cpu_cycles;
        const SocStatus status = transfer_one(selected, report, budget);
        if (status == SocStatus::idle) {
            report.status =
                report.words_transferred == 0U && report.cpu_cycles == 0U
                    ? SocStatus::idle
                    : SocStatus::ok;
            return report;
        }
        if (status != SocStatus::ok) {
            fail(selected, status);
            report.status = status;
            return report;
        }
        if (before_words == report.words_transferred &&
            before_cycles == report.cpu_cycles) {
            fail(selected, SocStatus::arithmetic_overflow);
            report.status = SocStatus::arithmetic_overflow;
            return report;
        }
        if (report.irq_boundary) {
            report.status = SocStatus::ok;
            report.became_idle = choose_channel() == kChannelCount;
            return report;
        }
    }
    report.status = report.words_transferred == 0U && report.cpu_cycles == 0U
                        ? SocStatus::idle
                        : SocStatus::ok;
    report.became_idle = choose_channel() == kChannelCount;
    return report;
}

DmaSnapshot DmaController::snapshot() const {
    DmaSnapshot result{};
    for (std::size_t index = 0U; index < kChannelCount; ++index) {
        const ChannelState& current = channel_storage_[index];
        DmaChannelSnapshot& destination = result.channels[index];
        destination.registers = current.registers;
        destination.active = current.active;
        destination.completed = current.completed;
        destination.error = current.error;
        destination.last_status = current.last_status;
        destination.current_address = current.current_address;
        destination.words_transferred = current.words_transferred;
        destination.words_remaining = current.words_remaining;
        destination.linked_current = current.linked_current;
        destination.linked_next = current.linked_next;
        destination.linked_nodes = current.linked_nodes;
        destination.linked_words = current.linked_words;
        destination.burst_words = current.burst_words;
        destination.block_words_remaining = current.block_words_remaining;
        destination.request_blocks_remaining = current.request_blocks_remaining;
        destination.cpu_window_remaining = current.cpu_window_remaining;
        destination.window_active = current.window_active;
        destination.request_asserted = current.request_asserted;
        destination.visited_nodes = current.visited_nodes;
    }
    result.priority_control = priority_control_;
    result.interrupt_control = interrupt_control_;
    result.interrupt_flags = interrupt_flags_;
    result.master_irq_line = master_irq_line_;
    return result;
}

SocStatus DmaController::validate_snapshot(
    const DmaSnapshot& state) const noexcept {
    if ((state.interrupt_control & ~kDmaInterruptControlMask) != 0U ||
        (state.interrupt_flags & ~kDmaChannelMask) != 0U) {
        return SocStatus::snapshot_mismatch;
    }
    const bool expected_irq_line =
        (state.interrupt_control & kDmaBusErrorBit) != 0U ||
        (((state.interrupt_control & (1U << 23U)) != 0U) &&
         state.interrupt_flags != 0U);
    if (state.master_irq_line != expected_irq_line) {
        return SocStatus::snapshot_mismatch;
    }
    for (std::size_t index = 0U; index < kChannelCount; ++index) {
        const DmaChannelSnapshot& snapshot_state = state.channels[index];
        const std::uint32_t control = snapshot_state.registers.channel_control;
        const DmaSyncMode mode =
            static_cast<DmaSyncMode>((control >> kDmaSyncShift) & 3U);
        const bool is_chopping = (control & kDmaChopBit) != 0U;
        const std::uint64_t block_size =
            (snapshot_state.registers.block_control & 0xffffU) == 0U
                ? 0x1'0000ULL
                : snapshot_state.registers.block_control & 0xffffU;
        const std::uint32_t dma_window_words = 1U << ((control >> 16U) & 7U);
        const std::uint32_t cpu_window_cycles = 1U << ((control >> 20U) & 7U);
        const bool busy = (control & kDmaEnableBit) != 0U;
        const bool force = (control & kDmaTriggerBit) != 0U;

        if ((snapshot_state.registers.base_address & ~kAddressMask) != 0U ||
            (control & ~kDmaControlMask) != 0U ||
            (snapshot_state.current_address & ~kAddressMask) != 0U ||
            (snapshot_state.current_address & 3U) != 0U ||
            (snapshot_state.linked_current & ~kAddressMask) != 0U ||
            (snapshot_state.linked_next & ~kAddressMask) != 0U ||
            (snapshot_state.linked_current != kDmaEndAddress &&
             (snapshot_state.linked_current & 3U) != 0U) ||
            (snapshot_state.linked_next != kDmaEndAddress &&
             (snapshot_state.linked_next & 3U) != 0U) ||
            !valid_soc_status(snapshot_state.last_status) ||
            (snapshot_state.active &&
             (snapshot_state.completed || snapshot_state.error || !busy ||
              force || snapshot_state.last_status != SocStatus::ok)) ||
            (snapshot_state.completed && snapshot_state.error) ||
            ((snapshot_state.completed || snapshot_state.error) &&
             (busy || force)) ||
            (snapshot_state.completed &&
             snapshot_state.last_status != SocStatus::ok) ||
            (snapshot_state.error &&
             (snapshot_state.last_status == SocStatus::ok ||
              snapshot_state.last_status == SocStatus::idle ||
              snapshot_state.last_status == SocStatus::snapshot_mismatch)) ||
            (!snapshot_state.active && !snapshot_state.completed &&
             !snapshot_state.error &&
             snapshot_state.last_status != SocStatus::idle) ||
            snapshot_state.cpu_window_remaining > cpu_window_cycles ||
            snapshot_state.burst_words > dma_window_words ||
            snapshot_state.linked_words > max_linked_words_ ||
            snapshot_state.visited_nodes.size() > max_linked_nodes_) {
            return SocStatus::snapshot_mismatch;
        }

        if ((!snapshot_state.active &&
             (snapshot_state.window_active ||
              snapshot_state.cpu_window_remaining != 0U)) ||
            (snapshot_state.active && mode == DmaSyncMode::manual &&
             !snapshot_state.window_active &&
             snapshot_state.cpu_window_remaining == 0U)) {
            return SocStatus::snapshot_mismatch;
        }

        if (!snapshot_state.active && !snapshot_state.completed &&
            !snapshot_state.error &&
            (snapshot_state.words_transferred != 0U ||
             snapshot_state.words_remaining != 0U ||
             snapshot_state.block_words_remaining != 0U ||
             snapshot_state.request_blocks_remaining != 0U ||
             snapshot_state.burst_words != 0U ||
             snapshot_state.linked_nodes != 0U ||
             snapshot_state.linked_words != 0U ||
             !snapshot_state.visited_nodes.empty())) {
            return SocStatus::snapshot_mismatch;
        }

        if (snapshot_state.visited_nodes.size() >
                std::numeric_limits<std::uint32_t>::max() ||
            snapshot_state.linked_nodes !=
                static_cast<std::uint32_t>(
                    snapshot_state.visited_nodes.size())) {
            return SocStatus::snapshot_mismatch;
        }

        if (index == kOrderingTableChannel &&
            (control & (kDmaDirectionBit | kDmaStepBit |
                        (3U << kDmaSyncShift))) != kDmaStepBit) {
            return SocStatus::snapshot_mismatch;
        }

        if (!is_chopping && (snapshot_state.burst_words != 0U ||
                             snapshot_state.cpu_window_remaining != 0U)) {
            return SocStatus::snapshot_mismatch;
        }
        if (snapshot_state.cpu_window_remaining != 0U &&
            (!snapshot_state.active || mode != DmaSyncMode::manual ||
             !is_chopping || snapshot_state.window_active ||
             snapshot_state.burst_words != dma_window_words)) {
            return SocStatus::snapshot_mismatch;
        }

        if (snapshot_state.active) {
            if (mode == DmaSyncMode::reserved ||
                (mode == DmaSyncMode::linked_list &&
                 (index != 2U || (control & kDmaDirectionBit) == 0U)) ||
                (mode != DmaSyncMode::manual && is_chopping)) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (snapshot_state.completed &&
            (mode == DmaSyncMode::reserved ||
             (mode == DmaSyncMode::linked_list &&
              (index != 2U || (control & kDmaDirectionBit) == 0U)) ||
             (mode != DmaSyncMode::manual && is_chopping))) {
            return SocStatus::snapshot_mismatch;
        }

        if (mode == DmaSyncMode::manual) {
            if (snapshot_state.words_transferred > 0x1'0000ULL ||
                snapshot_state.words_remaining > 0x1'0000ULL ||
                snapshot_state.block_words_remaining > 0x1'0000ULL ||
                snapshot_state.request_blocks_remaining != 0U ||
                snapshot_state.words_transferred +
                        snapshot_state.words_remaining >
                    0x1'0000ULL) {
                return SocStatus::snapshot_mismatch;
            }
            if ((snapshot_state.active || snapshot_state.completed ||
                 snapshot_state.error) &&
                !is_chopping &&
                snapshot_state.words_transferred +
                        snapshot_state.words_remaining !=
                    block_size) {
                return SocStatus::snapshot_mismatch;
            }
            if ((snapshot_state.active || snapshot_state.completed) &&
                is_chopping &&
                (snapshot_state.registers.base_address !=
                     snapshot_state.current_address ||
                 (snapshot_state.registers.block_control & 0xffffU) !=
                     (snapshot_state.words_remaining & 0xffffU))) {
                return SocStatus::snapshot_mismatch;
            }
        } else if (mode == DmaSyncMode::request) {
            if (snapshot_state.words_transferred > 0x1'0000'0000ULL ||
                snapshot_state.words_remaining > 0x1'0000'0000ULL ||
                snapshot_state.request_blocks_remaining > 0x1'0000U ||
                snapshot_state.block_words_remaining > block_size) {
                return SocStatus::snapshot_mismatch;
            }
            if (snapshot_state.request_blocks_remaining != 0U &&
                (snapshot_state.block_words_remaining == 0U ||
                 snapshot_state.words_remaining !=
                     (static_cast<std::uint64_t>(
                          snapshot_state.request_blocks_remaining - 1U) *
                      block_size) +
                         snapshot_state.block_words_remaining ||
                 ((snapshot_state.registers.block_control >> 16U) & 0xffffU) !=
                     (snapshot_state.request_blocks_remaining & 0xffffU) ||
                 snapshot_state.registers.base_address !=
                     snapshot_state.current_address)) {
                return SocStatus::snapshot_mismatch;
            }
        } else if (mode == DmaSyncMode::linked_list) {
            if (snapshot_state.words_remaining > 0xffU ||
                snapshot_state.block_words_remaining !=
                    snapshot_state.words_remaining ||
                snapshot_state.request_blocks_remaining != 0U ||
                snapshot_state.words_transferred !=
                    snapshot_state.linked_words) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (mode == DmaSyncMode::manual && snapshot_state.active) {
            if (snapshot_state.words_remaining == 0U ||
                snapshot_state.words_remaining > 0x1'0000ULL ||
                snapshot_state.block_words_remaining !=
                    snapshot_state.words_remaining ||
                snapshot_state.request_blocks_remaining != 0U ||
                snapshot_state.words_transferred > 0x1'0000ULL ||
                snapshot_state.words_transferred +
                        snapshot_state.words_remaining >
                    0x1'0000ULL) {
                return SocStatus::snapshot_mismatch;
            }
            if (is_chopping && snapshot_state.window_active &&
                snapshot_state.burst_words >= dma_window_words) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (mode == DmaSyncMode::request && snapshot_state.active) {
            if (snapshot_state.request_blocks_remaining == 0U ||
                snapshot_state.block_words_remaining == 0U ||
                snapshot_state.block_words_remaining > block_size ||
                snapshot_state.words_remaining == 0U ||
                snapshot_state.words_remaining > 0x1'0000'0000ULL ||
                snapshot_state.words_remaining !=
                    (static_cast<std::uint64_t>(
                         snapshot_state.request_blocks_remaining - 1U) *
                     block_size) +
                        snapshot_state.block_words_remaining ||
                ((snapshot_state.registers.block_control >> 16U) & 0xffffU) !=
                    (snapshot_state.request_blocks_remaining & 0xffffU) ||
                snapshot_state.registers.base_address !=
                    snapshot_state.current_address ||
                snapshot_state.burst_words != 0U ||
                snapshot_state.cpu_window_remaining != 0U) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (mode == DmaSyncMode::linked_list && snapshot_state.active) {
            if (snapshot_state.words_remaining > 0xffU ||
                snapshot_state.block_words_remaining !=
                    snapshot_state.words_remaining ||
                snapshot_state.request_blocks_remaining != 0U ||
                snapshot_state.words_transferred !=
                    snapshot_state.linked_words ||
                snapshot_state.linked_current == kDmaEndAddress ||
                snapshot_state.registers.base_address !=
                    snapshot_state.linked_current ||
                (!snapshot_state.window_active &&
                 snapshot_state.words_remaining != 0U) ||
                snapshot_state.burst_words != 0U ||
                snapshot_state.cpu_window_remaining != 0U) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (snapshot_state.completed) {
            if (snapshot_state.words_remaining != 0U ||
                snapshot_state.block_words_remaining != 0U ||
                snapshot_state.request_blocks_remaining != 0U ||
                (mode != DmaSyncMode::linked_list &&
                 snapshot_state.words_transferred == 0U)) {
                return SocStatus::snapshot_mismatch;
            }
            if (mode == DmaSyncMode::request &&
                (((snapshot_state.registers.block_control >> 16U) & 0xffffU) !=
                     0U ||
                 snapshot_state.registers.base_address !=
                     snapshot_state.current_address ||
                 snapshot_state.words_transferred % block_size != 0U)) {
                return SocStatus::snapshot_mismatch;
            }
            if (mode == DmaSyncMode::linked_list &&
                (snapshot_state.registers.base_address != kDmaEndAddress ||
                 snapshot_state.linked_next != kDmaEndAddress ||
                 snapshot_state.linked_nodes == 0U)) {
                return SocStatus::snapshot_mismatch;
            }
        }

        if (mode != DmaSyncMode::linked_list &&
            (snapshot_state.linked_nodes != 0U ||
             snapshot_state.linked_words != 0U ||
             !snapshot_state.visited_nodes.empty())) {
            return SocStatus::snapshot_mismatch;
        }

        for (std::size_t outer = 0U;
             outer < snapshot_state.visited_nodes.size(); ++outer) {
            const std::uint32_t address = snapshot_state.visited_nodes[outer];
            if ((address & 3U) != 0U || (address & ~kAddressMask) != 0U) {
                return SocStatus::snapshot_mismatch;
            }
            for (std::size_t inner = 0U; inner < outer; ++inner) {
                if (snapshot_state.visited_nodes[inner] == address) {
                    return SocStatus::snapshot_mismatch;
                }
            }
        }
    }

    return SocStatus::ok;
}

SocStatus DmaController::restore(const DmaSnapshot& state) noexcept {
    callback_mutated_ = callback_mutated_ || running_;
    const SocStatus validation = validate_snapshot(state);
    if (validation != SocStatus::ok) {
        return validation;
    }

    std::array<std::vector<std::uint32_t>, kChannelCount> copied_visited{};
    try {
        for (std::size_t index = 0U; index < kChannelCount; ++index) {
            copied_visited[index] = state.channels[index].visited_nodes;
        }
    } catch (...) {
        return SocStatus::snapshot_mismatch;
    }

    for (std::size_t index = 0U; index < kChannelCount; ++index) {
        const DmaChannelSnapshot& source = state.channels[index];
        ChannelState& destination = channel_storage_[index];
        destination.registers = source.registers;
        destination.active = source.active;
        destination.completed = source.completed;
        destination.error = source.error;
        destination.last_status = source.last_status;
        destination.current_address = source.current_address;
        destination.words_transferred = source.words_transferred;
        destination.words_remaining = source.words_remaining;
        destination.linked_current = source.linked_current;
        destination.linked_next = source.linked_next;
        destination.linked_nodes = source.linked_nodes;
        destination.linked_words = source.linked_words;
        destination.burst_words = source.burst_words;
        destination.block_words_remaining = source.block_words_remaining;
        destination.request_blocks_remaining = source.request_blocks_remaining;
        destination.cpu_window_remaining = source.cpu_window_remaining;
        destination.window_active = source.window_active;
        destination.request_asserted = source.request_asserted;
        destination.visited_nodes = std::move(copied_visited[index]);
    }
    priority_control_ = state.priority_control;
    interrupt_control_ = state.interrupt_control;
    interrupt_flags_ = state.interrupt_flags;
    master_irq_line_ = state.master_irq_line;
    return SocStatus::ok;
}

SystemOnChip::SystemOnChip() noexcept
    : dma_(&interrupts_), counters_(&interrupts_) {
}

void SystemOnChip::reset() noexcept {
    interrupts_.reset();
    dma_.reset();
    counters_.reset();
}

InterruptController& SystemOnChip::interrupts() noexcept {
    return interrupts_;
}

DmaController& SystemOnChip::dma() noexcept {
    return dma_;
}

RootCounters& SystemOnChip::counters() noexcept {
    return counters_;
}

const InterruptController& SystemOnChip::interrupts() const noexcept {
    return interrupts_;
}

const DmaController& SystemOnChip::dma() const noexcept {
    return dma_;
}

const RootCounters& SystemOnChip::counters() const noexcept {
    return counters_;
}

SocSnapshot SystemOnChip::snapshot() const {
    SocSnapshot result{};
    result.interrupts = interrupts_.snapshot();
    result.dma = dma_.snapshot();
    for (std::size_t index = 0U; index < RootCounters::kCount; ++index) {
        result.counters[index] = counters_.snapshot(index);
    }
    return result;
}

SocStatus SystemOnChip::validate_snapshot(
    const SocSnapshot& state) const noexcept {
    SocStatus status = interrupts_.validate_snapshot(state.interrupts);
    if (status != SocStatus::ok) {
        return status;
    }
    status = dma_.validate_snapshot(state.dma);
    if (status != SocStatus::ok) {
        return status;
    }
    for (std::size_t index = 0U; index < RootCounters::kCount; ++index) {
        status = counters_.validate_snapshot(index, state.counters[index]);
        if (status != SocStatus::ok) {
            return status;
        }
    }
    return SocStatus::ok;
}

SocStatus SystemOnChip::restore(const SocSnapshot& state) noexcept {
    const SocStatus validation = validate_snapshot(state);
    if (validation != SocStatus::ok) {
        return validation;
    }

    // DMA is the only nested state with dynamically allocated data.  Its
    // restore allocates and validates before mutation, so applying it first
    // keeps the aggregate operation atomic on allocation failure.  The
    // already-validated interrupt and counter restores cannot then fail.
    SocStatus status = dma_.restore(state.dma);
    if (status != SocStatus::ok) {
        return status;
    }
    static_cast<void>(interrupts_.restore(state.interrupts));
    for (std::size_t index = 0U; index < RootCounters::kCount; ++index) {
        static_cast<void>(counters_.restore(index, state.counters[index]));
    }
    return SocStatus::ok;
}

} // namespace clean_aot::soc
