#include "clean_aot/app/sdl_platform.hpp"

#include <array>
#include <iostream>

int main() {
    clean_aot::app::GraphicsConfiguration graphics{};
    auto native = clean_aot::app::make_live_graphics_config("live", graphics);
    if (native.width != 3840U || native.height != 2160U ||
        native.aspect != clean_aot::host::AspectPolicy::preserve_4x3 ||
        native.fullscreen != clean_aot::host::FullscreenMode::borderless ||
        native.presentation_filter != clean_aot::host::PresentationFilter::Nearest) {
        std::cerr << "exact 4K 4:3 live graphics default changed\n";
        return 1;
    }
    graphics.output_width = 5120U;
    graphics.output_height = 2160U;
    graphics.presentation = clean_aot::raster::PresentationMode::Generic21x9;
    graphics.presentation_resolve =
        clean_aot::raster::PresentationResolveMode::EdgeAware;
    native = clean_aot::app::make_live_graphics_config("live", graphics);
    if (native.width != 5120U || native.height != 2160U ||
        native.aspect != clean_aot::host::AspectPolicy::preserve_21x9 ||
        native.presentation_filter != clean_aot::host::PresentationFilter::Linear) {
        std::cerr << "5K ultrawide live graphics policy did not propagate\n";
        return 1;
    }
    clean_aot::app::SdlPlatform platform;
    clean_aot::app::PlatformConfiguration configuration{};
    configuration.width = 320U;
    configuration.height = 240U;
    if (!platform.initialize("clean AOT shell test", configuration)) {
        std::cerr << "SDL dummy window/audio/input initialization failed\n";
        return 1;
    }
    clean_aot::host::ControllerState controller{};
    bool quit = false;
    if (!platform.pump(controller, quit) || quit) {
        std::cerr << "SDL bounded input/event pump failed\n";
        return 1;
    }
    if (controller.sample_index != 0U) return 1;
    for (std::uint64_t sample = 1U; sample < 3600U; ++sample) {
        if (!platform.pump(controller, quit) || quit ||
            controller.sample_index != sample) {
            std::cerr << "100 Hz input sample sequence drifted\n";
            return 1;
        }
    }
    clean_aot::sio::PadState pad{};
    pad.rumble_small = 1U;
    pad.rumble_large = 0xffU;
    if (!platform.apply_rumble(pad, 0U) || platform.apply_rumble(pad, 0U)) {
        std::cerr << "SDL rumble bridge sample sequencing failed\n";
        return 1;
    }
    std::array<std::int16_t,
               clean_aot::host::kAudioFramesPerUpdate *
                   clean_aot::host::kAudioChannels> silence{};
    const clean_aot::host::StereoAudioBatch audio{
        0U, silence.data(), clean_aot::host::kAudioFramesPerUpdate};
    if (!platform.queue_audio(audio)) {
        std::cerr << "SDL exact audio batch queue failed\n";
        return 1;
    }
    if (platform.audio_health().overflows != 0U) return 1;
    platform.shutdown();
    std::cout << "clean_aot_app_sdl_tests: all checks passed\n";
    return 0;
}
