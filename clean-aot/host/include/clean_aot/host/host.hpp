#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace clean_aot::host {

inline constexpr std::uint32_t kUpdateRateHz = 100U;
inline constexpr std::uint32_t kAudioSampleRateHz = 44'100U;
inline constexpr std::uint32_t kAudioChannels = 2U;
inline constexpr std::uint32_t kAudioFramesPerUpdate =
    kAudioSampleRateHz / kUpdateRateHz;
static_assert(kAudioFramesPerUpdate == 441U);

enum class HostCode : std::uint8_t {
    ok,
    not_ready,
    invalid_argument,
    queue_full,
    sequence_error,
    shutting_down,
    device_lost,
    unsupported,
    internal_error,
};

struct HostStatus final {
    HostCode code = HostCode::ok;

    [[nodiscard]] constexpr bool success() const noexcept {
        return code == HostCode::ok;
    }
};

enum class PixelFormat : std::uint8_t {
    rgba8_unorm,
    bgra8_unorm,
    rgb8_unorm,
    rgb565,
    indexed8,
};

[[nodiscard]] constexpr std::uint32_t bytes_per_pixel(
    PixelFormat format) noexcept {
    switch (format) {
    case PixelFormat::rgba8_unorm:
    case PixelFormat::bgra8_unorm:
        return 4U;
    case PixelFormat::rgb8_unorm:
        return 3U;
    case PixelFormat::rgb565:
        return 2U;
    case PixelFormat::indexed8:
        return 1U;
    }
    return 0U;
}

struct FrameView final {
    const std::uint8_t* pixels = nullptr;
    std::uint32_t byte_size = 0U;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t pitch = 0U;
    PixelFormat format = PixelFormat::rgba8_unorm;
};

struct FrameSubmission final {
    std::uint64_t sequence = 0U;
    std::uint64_t update_index = 0U;
    FrameView surface{};
};

struct StereoAudioBatch final {
    std::uint64_t update_index = 0U;
    const std::int16_t* interleaved_stereo = nullptr;
    std::uint32_t sample_frames = 0U;
};

inline constexpr std::size_t kControllerAxisCount = 8U;

struct ControllerState final {
    std::uint64_t sample_index = 0U;
    std::uint32_t connected_mask = 0U;
    std::uint32_t buttons = 0U;
    std::array<std::int16_t, kControllerAxisCount> axes{};
};

enum class FullscreenMode : std::uint8_t {
    windowed,
    borderless,
    exclusive,
};

struct WindowState final {
    std::uint32_t pixel_width = 0U;
    std::uint32_t pixel_height = 0U;
    FullscreenMode fullscreen = FullscreenMode::windowed;
};

struct WindowRequest final {
    WindowState state{};
};

enum class HostEventKind : std::uint8_t {
    quit_requested,
    window_resized,
    fullscreen_changed,
    device_lost,
};

struct HostEvent final {
    HostEventKind kind = HostEventKind::window_resized;
    WindowState window{};
    HostCode reason = HostCode::ok;
};

class IHostBackend {
public:
    virtual ~IHostBackend() = default;

    // The pointed-to frame and audio memory must remain valid until the
    // corresponding service call consumes it. Backends never take ownership.
    [[nodiscard]] virtual HostStatus submit_frame(
        const FrameSubmission& frame) noexcept = 0;
    [[nodiscard]] virtual HostStatus submit_audio(
        const StereoAudioBatch& audio) noexcept = 0;
    [[nodiscard]] virtual HostStatus submit_controller(
        const ControllerState& state) noexcept = 0;
    [[nodiscard]] virtual HostStatus request_window(
        const WindowRequest& request) noexcept = 0;

    // Service is bounded: it consumes at most one item from each queue and a
    // bounded number of OS events. It never allocates.
    [[nodiscard]] virtual HostStatus service() noexcept = 0;
    [[nodiscard]] virtual bool poll_event(HostEvent& event) noexcept = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual bool shutting_down() const noexcept = 0;
};

} // namespace clean_aot::host
