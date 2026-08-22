#pragma once

#include "clean_aot/host/host.hpp"

#include <cstdint>

namespace clean_aot::app {

class SdlPlatform final {
public:
    SdlPlatform() noexcept = default;
    ~SdlPlatform();
    [[nodiscard]] bool initialize(const char* title,
                                  std::uint32_t width,
                                  std::uint32_t height) noexcept;
    [[nodiscard]] bool pump(host::ControllerState& controller,
                            bool& quit) noexcept;
    [[nodiscard]] bool queue_audio(
        const host::StereoAudioBatch& audio) noexcept;
    void shutdown() noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace clean_aot::app
