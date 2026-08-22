#include "clean_aot/host/sdl_vulkan.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {
using namespace clean_aot::host;
[[nodiscard]] bool service_until_submitted(SdlVulkanPresentation& backend,
                                            std::uint64_t count) {
    for (std::uint32_t attempt = 0U; attempt < 512U; ++attempt) {
        const HostStatus status = backend.service();
        if (!status.success() && status.code != HostCode::not_ready) return false;
        if (backend.presentation_metrics().gpu_present_submissions == count) return true;
    }
    return false;
}
} // namespace

int main() {
    using namespace clean_aot::host;
    SdlVulkanConfig config{};
    config.title = "clean-aot hidden 100 Hz Vulkan acceptance";
    config.width = 3840U; config.height = 2160U;
    config.fullscreen = FullscreenMode::windowed;
    config.aspect = AspectPolicy::preserve_4x3;
    config.hidden = true;
    config.present_policy = SdlPresentPolicy::low_latency_100hz;
    config.target_refresh_hz = kUpdateRateHz;
    config.allowed_frames_in_flight = 1U;
    SdlVulkanPresentation hidden_backend(config);
    if (!hidden_backend.initialize().success()) return 1;
    const SdlPresentationMetrics initial = hidden_backend.presentation_metrics();
    if (initial.target_refresh_hz != 100U || initial.allowed_frames_in_flight != 1U ||
        initial.active_present_mode == SdlActivePresentMode::unknown ||
        initial.display_presentations_confirmed != 0U || initial.vrr_status_known) return 2;

    const std::array<std::uint8_t, 12U> rgb{
        255U,0U,0U, 0U,255U,0U, 0U,0U,255U, 255U,255U,255U};
    const FrameView surface{rgb.data(),static_cast<std::uint32_t>(rgb.size()),
        2U,2U,6U,PixelFormat::rgb8_unorm};
    if (!hidden_backend.submit_frame({0U,0U,surface}).success() ||
        hidden_backend.service().code != HostCode::not_ready) return 3;
    const SdlPresentationMetrics hidden_metrics = hidden_backend.presentation_metrics();
    if (hidden_metrics.frames_accepted != 1U ||
        hidden_metrics.swapchain_images_acquired != 0U ||
        hidden_metrics.gpu_present_submissions != 0U ||
        hidden_metrics.acquire_not_ready != 1U) return 4;
    hidden_backend.shutdown();

    // A hidden SDL window intentionally has no presentable swapchain image.
    // Use a real shown swapchain for the GPU submission acceptance tranche.
    config.hidden = false;
    SdlVulkanPresentation backend(config);
    if (!backend.initialize().success()) return 5;
    if (!backend.submit_frame({7U,0U,surface}).success()) return 3;
    if (backend.service().code != HostCode::sequence_error) return 4;
    for (std::uint64_t index = 0U; index < 100U; ++index) {
        if (!backend.submit_frame({index,index,surface}).success()) return 5;
        if (!service_until_submitted(backend,index+1U)) return 6;
    }
    SdlPresentationMetrics metrics = backend.presentation_metrics();
    if (metrics.frames_accepted != 101U || metrics.swapchain_images_acquired != 100U ||
        metrics.gpu_present_submissions != 100U ||
        metrics.display_presentations_confirmed != 0U ||
        metrics.next_sequence != 100U || metrics.next_update_index != 100U ||
        metrics.sequence_errors != 1U || metrics.queue_latency_ns_max == 0U) return 7;
    if (!backend.request_window({{5120U,2160U,FullscreenMode::windowed}}).success() ||
        !backend.recreate_swapchain().success()) return 8;
    if (!backend.submit_frame({100U,100U,surface}).success() ||
        !service_until_submitted(backend,101U)) return 9;
    metrics = backend.presentation_metrics();
    if (metrics.swapchain_recreations < 1U || metrics.gpu_present_submissions != 101U) return 10;

    const std::array<std::int16_t, 882U> audio{};
    constexpr std::uint64_t kSoakUpdates = 200U;
    for (std::uint64_t update = 0U; update < kSoakUpdates; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 11;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const SdlAudioMetrics audio_metrics = backend.audio_metrics();
    if (audio_metrics.submitted_updates != kSoakUpdates ||
        audio_metrics.submitted_frames != kSoakUpdates * 441U ||
        audio_metrics.dropped_updates != 0U || audio_metrics.overruns != 0U ||
        !audio_metrics.playback_started || !audio_metrics.exact_44100_stereo ||
        audio_metrics.time_stretch_used || audio_metrics.peak_queued_frames > 3528U) return 12;
    for (std::uint64_t update = kSoakUpdates; update < kSoakUpdates + 32U; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 13;
    }
    const SdlAudioMetrics saturated = backend.audio_metrics();
    if (saturated.overruns == 0U || saturated.dropped_updates == 0U ||
        saturated.queued_frames > 3528U || saturated.time_stretch_used) return 14;
    const std::array<std::uint8_t, 4U> replacement{1U,2U,3U,255U};
    ReplacementImage image{}; image.width=1U; image.height=1U; image.rgba8=replacement;
    std::uint64_t handle=0U;
    if (!backend.upload_replacement(image,handle).success() || handle==0U ||
        !backend.release_replacement(handle).success()) return 15;
    backend.mark_device_lost();
    if (backend.service().code != HostCode::device_lost ||
        backend.presentation_metrics().device_losses != 1U) return 16;
    backend.shutdown();
    std::cout << "clean_aot_host_sdl_vulkan_tests: 101 unique GPU submissions; "
                 "physical presentation intentionally unclaimed\n";
    return 0;
}
