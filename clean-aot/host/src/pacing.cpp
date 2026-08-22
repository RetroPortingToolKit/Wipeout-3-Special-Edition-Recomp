#include "clean_aot/host/pacing.hpp"

#include <chrono>
#include <limits>
#include <thread>

namespace clean_aot::host {
namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;

[[nodiscard]] std::uint64_t add_saturated(
    std::uint64_t left,
    std::uint64_t right,
    bool& overflow) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        overflow = true;
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

} // namespace

std::uint64_t SystemMonotonicClock::now_ns() const noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void SystemMonotonicClock::sleep_until_ns(
    std::uint64_t deadline_ns) noexcept {
    if (deadline_ns >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::time_point{
        std::chrono::nanoseconds{static_cast<std::int64_t>(deadline_ns)}};
    std::this_thread::sleep_until(deadline);
}

MonotonicPacer::MonotonicPacer(FrameSchedule schedule) noexcept
    : schedule_(schedule) {}

HostStatus MonotonicPacer::start(std::uint64_t origin_ns) noexcept {
    if (!schedule_.valid() || started_) {
        return HostStatus{HostCode::invalid_argument};
    }
    deadline_ns_ = origin_ns;
    frame_index_ = 0U;
    last_observed_ns_ = origin_ns;
    remainder_ = 0U;
    started_ = true;
    return HostStatus{};
}

PaceResult MonotonicPacer::wait(IMonotonicClock& clock) noexcept {
    if (!started_) {
        return PaceResult{PaceDecision::not_started, 0U, clock.now_ns()};
    }

    std::uint64_t observed = clock.now_ns();
    if (observed < last_observed_ns_) {
        return PaceResult{
            PaceDecision::clock_stalled, deadline_ns_, observed};
    }
    if (observed < deadline_ns_) {
        clock.sleep_until_ns(deadline_ns_);
        const std::uint64_t after_sleep = clock.now_ns();
        if (after_sleep < observed || after_sleep < deadline_ns_) {
            return PaceResult{
                PaceDecision::clock_stalled, deadline_ns_, after_sleep};
        }
        observed = after_sleep;
    }

    last_observed_ns_ = observed;

    return PaceResult{
        observed == deadline_ns_ ? PaceDecision::ready : PaceDecision::late,
        deadline_ns_,
        observed};
}

HostStatus MonotonicPacer::commit() noexcept {
    if (!started_ || !schedule_.valid()) {
        return HostStatus{HostCode::invalid_argument};
    }

    const std::uint64_t whole_nanoseconds =
        kNanosecondsPerSecond / schedule_.rate_hz;
    const std::uint32_t fractional_nanoseconds = static_cast<std::uint32_t>(
        kNanosecondsPerSecond % schedule_.rate_hz);
    bool overflow = false;
    deadline_ns_ = add_saturated(deadline_ns_, whole_nanoseconds, overflow);
    const std::uint64_t new_remainder =
        static_cast<std::uint64_t>(remainder_) + fractional_nanoseconds;
    if (new_remainder >= schedule_.rate_hz) {
        deadline_ns_ = add_saturated(deadline_ns_, 1U, overflow);
        remainder_ = static_cast<std::uint32_t>(
            new_remainder - static_cast<std::uint64_t>(schedule_.rate_hz));
    } else {
        remainder_ = static_cast<std::uint32_t>(new_remainder);
    }
    if (overflow || frame_index_ == std::numeric_limits<std::uint64_t>::max()) {
        return HostStatus{HostCode::internal_error};
    }
    ++frame_index_;
    return HostStatus{};
}

} // namespace clean_aot::host
