#pragma once

#include <cstdint>

#include "clean_aot/host/fixed_queue.hpp"
#include "clean_aot/host/host.hpp"

namespace clean_aot::host {

inline constexpr std::size_t kHeadlessFrameQueueDepth = 4U;
inline constexpr std::size_t kHeadlessAudioQueueDepth = 8U;
inline constexpr std::size_t kHeadlessControllerQueueDepth = 4U;
inline constexpr std::size_t kHeadlessEventQueueDepth = 8U;

struct HeadlessStats final {
    std::uint64_t frames_submitted = 0U;
    std::uint64_t frames_presented = 0U;
    std::uint64_t audio_batches_submitted = 0U;
    std::uint64_t audio_batches_consumed = 0U;
    std::uint64_t audio_sample_frames = 0U;
    std::uint64_t sequence_errors = 0U;
    std::uint64_t queue_full_errors = 0U;
    std::uint64_t frame_digest = 1469598103934665603ULL;
    std::uint64_t audio_digest = 1469598103934665603ULL;
    ControllerState controller{};
    WindowState window{640U, 480U, FullscreenMode::windowed};
};

class HeadlessBackend final : public IHostBackend {
public:
    HeadlessBackend() noexcept = default;
    ~HeadlessBackend() override { shutdown(); }

    [[nodiscard]] HostStatus submit_frame(
        const FrameSubmission& frame) noexcept override;
    [[nodiscard]] HostStatus submit_audio(
        const StereoAudioBatch& audio) noexcept override;
    [[nodiscard]] HostStatus submit_controller(
        const ControllerState& state) noexcept override;
    [[nodiscard]] HostStatus request_window(
        const WindowRequest& request) noexcept override;
    [[nodiscard]] HostStatus service() noexcept override;
    [[nodiscard]] bool poll_event(HostEvent& event) noexcept override;
    void shutdown() noexcept override;
    [[nodiscard]] bool shutting_down() const noexcept override {
        return shutting_down_;
    }

    [[nodiscard]] const HeadlessStats& stats() const noexcept { return stats_; }

private:
    [[nodiscard]] static bool valid_frame(const FrameSubmission& frame) noexcept;
    [[nodiscard]] static bool valid_audio(const StereoAudioBatch& audio) noexcept;
    [[nodiscard]] HostStatus service_frame() noexcept;
    [[nodiscard]] HostStatus service_audio() noexcept;
    [[nodiscard]] HostStatus service_controller() noexcept;

    FixedSpscQueue<FrameSubmission, kHeadlessFrameQueueDepth> frames_{};
    FixedSpscQueue<StereoAudioBatch, kHeadlessAudioQueueDepth> audio_{};
    FixedSpscQueue<ControllerState, kHeadlessControllerQueueDepth> controllers_{};
    FixedSpscQueue<HostEvent, kHeadlessEventQueueDepth> events_{};
    HeadlessStats stats_{};
    std::uint64_t next_frame_sequence_ = 0U;
    std::uint64_t next_audio_update_ = 0U;
    bool shutting_down_ = false;
};

} // namespace clean_aot::host
