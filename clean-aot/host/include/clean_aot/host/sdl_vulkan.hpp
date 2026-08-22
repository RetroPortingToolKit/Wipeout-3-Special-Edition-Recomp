#pragma once

#include <cstdint>

#include "clean_aot/host/fixed_queue.hpp"
#include "clean_aot/host/host.hpp"
#include "clean_aot/host/render_interfaces.hpp"

namespace clean_aot::host {

enum class PresentationFilter : std::uint8_t {
    Nearest,
    Linear,
};

enum class SdlPresentPolicy : std::uint8_t { vsync, low_latency_100hz, immediate, mailbox };
enum class SdlActivePresentMode : std::uint8_t { unknown, vsync, immediate, mailbox };
enum class PresentCompletionSource : std::uint8_t {
    unavailable, present_wait, display_timing, fault
};

struct PresentCompletion final {
    std::uint64_t generation = 0U;
    std::uint64_t present_id = 0U;
    std::uint64_t completion_ns = 0U;
};

class IPresentCompletionProbe {
public:
    virtual ~IPresentCompletionProbe() = default;
    [[nodiscard]] virtual PresentCompletionSource source() const noexcept = 0;
    virtual void reset(std::uint64_t generation) noexcept = 0;
    virtual void submitted(std::uint64_t generation,
                           std::uint64_t present_id) noexcept = 0;
    [[nodiscard]] virtual bool poll(PresentCompletion& completion) noexcept = 0;
};

// SDL confirms GPU queue submission, not compositor or scanout completion.
struct SdlPresentationMetrics final {
    std::uint64_t frames_accepted = 0U;
    std::uint64_t swapchain_images_acquired = 0U;
    std::uint64_t gpu_present_submissions = 0U;
    std::uint64_t frame_upload_bytes = 0U;
    std::uint64_t gpu_effect_frames = 0U;
    std::uint64_t gpu_effect_output_pixels = 0U;
    std::uint64_t gpu_ui_draws = 0U;
    std::uint64_t gpu_ui_pixels = 0U;
    std::uint64_t gpu_ui_stale_fallbacks = 0U;
    std::uint64_t display_presentations_confirmed = 0U;
    std::uint64_t acquire_not_ready = 0U;
    std::uint64_t sequence_errors = 0U;
    std::uint64_t swapchain_recreations = 0U;
    std::uint64_t device_losses = 0U;
    std::uint64_t queue_latency_ns_total = 0U;
    std::uint64_t queue_latency_ns_max = 0U;
    std::uint64_t next_sequence = 0U;
    std::uint64_t next_update_index = 0U;
    std::uint64_t present_generation = 0U;
    std::uint64_t last_completed_present_id = 0U;
    std::uint64_t last_completion_ns = 0U;
    std::uint64_t completion_interval_ns_total = 0U;
    std::uint64_t completion_interval_ns_max = 0U;
    std::uint64_t completion_intervals_at_target = 0U;
    std::uint64_t completion_interval_violations = 0U;
    std::uint32_t display_refresh_numerator = 0U;
    std::uint32_t display_refresh_denominator = 0U;
    std::uint32_t target_refresh_hz = 100U;
    std::uint8_t allowed_frames_in_flight = 1U;
    SdlActivePresentMode active_present_mode = SdlActivePresentMode::unknown;
    PresentCompletionSource completion_source = PresentCompletionSource::unavailable;
    bool exact_target_refresh = false;
    bool vrr_status_known = false;
    bool present_wait_extensions_enabled = false;
    bool display_timing_extension_enabled = false;
    bool completion_evidence_fault = false;
};

struct SdlVulkanConfig final {
    const char* title = "Clean AOT host";
    std::uint32_t width = 3840U;
    std::uint32_t height = 2160U;
    FullscreenMode fullscreen = FullscreenMode::borderless;
    AspectPolicy aspect = AspectPolicy::preserve_4x3;
    AspectRatio custom_aspect = kAspect4x3;
    ScaleMode scale = ScaleMode::fractional;
    bool debug_gpu = false;
    bool hidden = false;
    PresentationFilter presentation_filter = PresentationFilter::Nearest;
    SdlPresentPolicy present_policy = SdlPresentPolicy::low_latency_100hz;
    std::uint32_t target_refresh_hz = 100U;
    std::uint8_t allowed_frames_in_flight = 1U;
    std::uint64_t completion_cadence_tolerance_ns = 1'000'000U;
    IPresentCompletionProbe* completion_probe = nullptr;
    std::uint32_t audio_target_buffer_frames = 882U;
    std::uint32_t audio_max_buffer_frames = 3528U;
};

struct SdlAudioMetrics final {
    std::uint64_t submitted_updates = 0U;
    std::uint64_t submitted_frames = 0U;
    std::uint64_t dropped_updates = 0U;
    std::uint64_t underruns = 0U;
    std::uint64_t overruns = 0U;
    std::uint64_t device_reopens = 0U;
    std::uint64_t focus_lost_events = 0U;
    std::uint64_t focus_gained_events = 0U;
    std::uint32_t queued_frames = 0U;
    std::uint32_t peak_queued_frames = 0U;
    bool playback_started = false;
    bool exact_44100_stereo = true;
    bool time_stretch_used = false;
};

// This class is only linked when CLEAN_AOT_HOST_ENABLE_SDL_VULKAN is enabled
// on Windows. SDL owns window-thread affinity; callers must initialize,
// service, resize, and shut down it from the same thread.
class SdlVulkanPresentation final : public IHostBackend,
                                    public IReplacementUploader {
public:
    explicit SdlVulkanPresentation(const SdlVulkanConfig& config) noexcept;
    ~SdlVulkanPresentation();

    SdlVulkanPresentation(const SdlVulkanPresentation&) = delete;
    SdlVulkanPresentation& operator=(const SdlVulkanPresentation&) = delete;

    [[nodiscard]] HostStatus initialize() noexcept;
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
    [[nodiscard]] HostStatus recreate_swapchain() noexcept;
    void mark_device_lost() noexcept;
    void set_frame_encoder(IFrameEncoder* encoder) noexcept;
    [[nodiscard]] HostStatus upload_replacement(
        const ReplacementImage& replacement,
        std::uint64_t& handle) noexcept override;
    [[nodiscard]] HostStatus release_replacement(
        std::uint64_t handle) noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool shutting_down() const noexcept override;
    [[nodiscard]] WindowState window_state() const noexcept;
    [[nodiscard]] SdlPresentationMetrics presentation_metrics() const noexcept;
    [[nodiscard]] SdlAudioMetrics audio_metrics() const noexcept;
    [[nodiscard]] PresentationCapabilities presentation_capabilities()
        const noexcept override;

private:
    struct Impl;
    SdlVulkanConfig config_{};
    Impl* impl_ = nullptr;
    IFrameEncoder* encoder_ = nullptr;
};

} // namespace clean_aot::host
