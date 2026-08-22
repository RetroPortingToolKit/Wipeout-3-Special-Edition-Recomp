#pragma once

#include <cstdint>

#include "clean_aot/host/fixed_queue.hpp"
#include "clean_aot/host/host.hpp"
#include "clean_aot/host/render_interfaces.hpp"

namespace clean_aot::host {

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
    void shutdown() noexcept override;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool shutting_down() const noexcept override;
    [[nodiscard]] WindowState window_state() const noexcept;

private:
    struct Impl;
    SdlVulkanConfig config_{};
    Impl* impl_ = nullptr;
    IFrameEncoder* encoder_ = nullptr;
};

} // namespace clean_aot::host
