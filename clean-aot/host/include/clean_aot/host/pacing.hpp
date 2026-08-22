#pragma once

#include <cstdint>

#include "clean_aot/host/host.hpp"

namespace clean_aot::host {

struct FrameSchedule final {
    std::uint32_t rate_hz = 0U;

    [[nodiscard]] static constexpr FrameSchedule from_rate(
        std::uint32_t rate) noexcept {
        return FrameSchedule{rate};
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return rate_hz > 0U && rate_hz <= 1'000'000U;
    }
};

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual std::uint64_t now_ns() const noexcept = 0;
    virtual void sleep_until_ns(std::uint64_t deadline_ns) noexcept = 0;
};

class SystemMonotonicClock final : public IMonotonicClock {
public:
    [[nodiscard]] std::uint64_t now_ns() const noexcept override;
    void sleep_until_ns(std::uint64_t deadline_ns) noexcept override;
};

enum class PaceDecision : std::uint8_t {
    ready,
    late,
    not_started,
    clock_stalled,
};

struct PaceResult final {
    PaceDecision decision = PaceDecision::not_started;
    std::uint64_t deadline_ns = 0U;
    std::uint64_t observed_ns = 0U;
};

class MonotonicPacer final {
public:
    explicit MonotonicPacer(FrameSchedule schedule) noexcept;

    [[nodiscard]] HostStatus start(std::uint64_t origin_ns) noexcept;
    [[nodiscard]] PaceResult wait(IMonotonicClock& clock) noexcept;
    [[nodiscard]] HostStatus commit() noexcept;

    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] std::uint64_t frame_index() const noexcept {
        return frame_index_;
    }
    [[nodiscard]] std::uint64_t deadline_ns() const noexcept {
        return deadline_ns_;
    }
    [[nodiscard]] FrameSchedule schedule() const noexcept { return schedule_; }

private:
    FrameSchedule schedule_{};
    std::uint64_t deadline_ns_ = 0U;
    std::uint64_t frame_index_ = 0U;
    std::uint64_t last_observed_ns_ = 0U;
    std::uint32_t remainder_ = 0U;
    bool started_ = false;
};

} // namespace clean_aot::host
