#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "clean_aot/runtime/native_dispatch.hpp"

namespace clean_aot::runtime {

struct RuntimeTraceImage final {
    std::string_view id;
    std::string_view image_kind;
    std::string_view sha256;
    std::string_view variant_sha256;
    std::uint32_t load_start = 0;
    std::uint64_t load_end_exclusive = 0;
};

class RuntimeTargetTrace final {
public:
    static constexpr std::size_t kMaximumImages = 8U;
    static constexpr std::size_t kMaximumEvents = 4096U;

    RuntimeTargetTrace(std::span<const RuntimeTraceImage> images,
                       std::size_t event_capacity = kMaximumEvents,
                       std::uint32_t first_sequence = 0U) noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    void record_epoch(std::string_view active_identity, DispatchStatus status) noexcept;
    void record_dispatch(std::string_view active_identity, std::uint32_t source_pc,
                         std::uint32_t destination_pc, DispatchStatus status) noexcept;
    void record_target_attempt(std::string_view active_identity,
                               std::string_view source_identity,
                               std::uint32_t source_pc,
                               std::string_view destination_identity,
                               std::uint32_t destination_pc,
                               RuntimeTraceTransfer transfer,
                               DispatchStatus status) noexcept;
    void finish() noexcept { finalized_ = true; }
    [[nodiscard]] std::string canonical_receipt() const;
    [[nodiscard]] std::uint64_t dropped_events() const noexcept { return dropped_events_; }
    [[nodiscard]] bool complete() const noexcept {
        return valid_ && finalized_ && dropped_events_ == 0U;
    }

private:
    enum class Kind : std::uint8_t { Epoch, Dispatch, TargetAttempt };
    struct Event final {
        Kind kind = Kind::Epoch;
        std::uint32_t sequence = 0;
        std::string_view active_identity;
        std::string_view source_identity;
        std::string_view destination_identity;
        std::uint32_t source_pc = 0;
        std::uint32_t destination_pc = 0;
        RuntimeTraceTransfer transfer = RuntimeTraceTransfer::None;
        DispatchStatus status = DispatchStatus::InvalidBinding;
    };

    void append(Event event) noexcept;
    [[nodiscard]] const RuntimeTraceImage* image_for(std::string_view identity) const noexcept;

    std::array<RuntimeTraceImage, kMaximumImages> images_{};
    std::array<Event, kMaximumEvents> events_{};
    std::size_t image_count_ = 0;
    std::size_t event_count_ = 0;
    std::size_t event_capacity_ = 0;
    std::uint32_t first_sequence_ = 0;
    std::uint64_t dropped_events_ = 0;
    bool valid_ = false;
    bool finalized_ = false;
};

} // namespace clean_aot::runtime
