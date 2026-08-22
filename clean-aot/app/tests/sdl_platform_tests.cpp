#include "clean_aot/app/sdl_platform.hpp"

#include <array>
#include <iostream>

int main() {
    clean_aot::app::SdlPlatform platform;
    if (!platform.initialize("clean AOT shell test", 320U, 240U)) {
        std::cerr << "SDL dummy window/audio/input initialization failed\n";
        return 1;
    }
    clean_aot::host::ControllerState controller{};
    bool quit = false;
    if (!platform.pump(controller, quit) || quit) {
        std::cerr << "SDL bounded input/event pump failed\n";
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
    platform.shutdown();
    std::cout << "clean_aot_app_sdl_tests: all checks passed\n";
    return 0;
}
