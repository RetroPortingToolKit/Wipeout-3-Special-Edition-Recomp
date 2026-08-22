#include "clean_aot/host/sdl_vulkan.hpp"

#include <array>
#include <cstdint>
#include <iostream>

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
    if (!backend.submit_audio({0U,audio.data(),441U}).success()) return 3;
    if (!backend.submit_controller({}).success()) return 4;
    if (!backend.service().success()) return 5;
    const std::array<std::uint8_t, 4U> replacement{1U,2U,3U,255U};
    ReplacementImage image{}; image.width=1U; image.height=1U; image.rgba8=replacement;
    std::uint64_t handle=0U;
    if (!backend.upload_replacement(image,handle).success() || handle==0U) return 6;
    backend.shutdown();
    std::cout << "clean_aot_host_sdl_vulkan_tests: all checks passed\n";
    return 0;
}
