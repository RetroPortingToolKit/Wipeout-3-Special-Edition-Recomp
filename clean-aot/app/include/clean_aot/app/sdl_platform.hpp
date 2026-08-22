#pragma once

#include "clean_aot/host/host.hpp"

#include <cstdint>

namespace clean_aot::app {

struct PlatformConfiguration final {
    std::uint32_t width = 3840U;
    std::uint32_t height = 2160U;
    std::uint32_t audio_buffer_frames = 882U;
    std::uint32_t max_queued_audio_frames = 3528U;
    std::int16_t controller_deadzone = 3932;
    bool borderless = true;
};

struct AudioHealth final {
    std::uint64_t underruns = 0U;
    std::uint64_t overflows = 0U;
    std::uint32_t queued_frames = 0U;
};

class SdlPlatform final {
public:
    SdlPlatform() noexcept = default;
    ~SdlPlatform();
    [[nodiscard]] bool initialize(const char* title,
                                  std::uint32_t width,
                                  std::uint32_t height) noexcept;
    [[nodiscard]] bool initialize(const char* title,
                                  PlatformConfiguration configuration) noexcept;
    [[nodiscard]] bool pump(host::ControllerState& controller,
                            bool& quit) noexcept;
    [[nodiscard]] bool queue_audio(
        const host::StereoAudioBatch& audio) noexcept;
    [[nodiscard]] AudioHealth audio_health() const noexcept;
    void shutdown() noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace clean_aot::app
