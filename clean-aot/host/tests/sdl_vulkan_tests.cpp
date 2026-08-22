#include "clean_aot/host/sdl_vulkan.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    using namespace clean_aot::host;
    SdlVulkanConfig config{};
    config.title = "clean-aot native backend test";
    config.width = 640U;
    config.height = 360U;
    config.fullscreen = FullscreenMode::windowed;
    config.aspect = AspectPolicy::preserve_21x9;
    config.hidden = false;
    SdlVulkanPresentation backend(config);
    if (!backend.initialize().success()) {
        std::cerr << "native SDL/Vulkan initialization failed\n";
        return 1;
    }
    const std::array<std::uint8_t, 12U> rgb{
        255U,0U,0U, 0U,255U,0U, 0U,0U,255U, 255U,255U,255U};
    const FrameSubmission frame{0U,0U,{rgb.data(),static_cast<std::uint32_t>(rgb.size()),
        2U,2U,6U,PixelFormat::rgb8_unorm}};
    if (!backend.submit_frame(frame).success()) return 2;
    const std::array<std::int16_t, 882U> audio{};
    constexpr std::uint64_t kSoakUpdates = 200U;
    for (std::uint64_t update = 0U; update < kSoakUpdates; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 3;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const SdlAudioMetrics metrics = backend.audio_metrics();
    if (metrics.submitted_updates != kSoakUpdates ||
        metrics.submitted_frames != kSoakUpdates * 441U ||
        metrics.dropped_updates != 0U || metrics.overruns != 0U ||
        !metrics.playback_started || !metrics.exact_44100_stereo ||
        metrics.time_stretch_used || metrics.peak_queued_frames > 3528U) return 8;
    for (std::uint64_t update = kSoakUpdates;
         update < kSoakUpdates + 32U; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 9;
    }
    const SdlAudioMetrics saturated = backend.audio_metrics();
    if (saturated.overruns == 0U || saturated.dropped_updates == 0U ||
        saturated.queued_frames > 3528U || saturated.time_stretch_used) return 10;
    if (!backend.submit_controller({}).success()) return 4;
    if (!backend.service().success()) return 5;
    const std::array<std::uint8_t, 4U> replacement{1U,2U,3U,255U};
    ReplacementImage image{}; image.width=1U; image.height=1U; image.rgba8=replacement;
    std::uint64_t handle=0U;
    if (!backend.upload_replacement(image,handle).success() || handle==0U) return 6;
    if (!backend.release_replacement(handle).success()) return 7;
    backend.shutdown();
    std::cout << "clean_aot_host_sdl_vulkan_tests: all checks passed\n";
    return 0;
}
