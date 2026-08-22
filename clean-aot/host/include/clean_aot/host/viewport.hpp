#pragma once

#include <cstdint>

#include "clean_aot/host/host.hpp"

namespace clean_aot::host {

struct AspectRatio final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

inline constexpr AspectRatio kAspect4x3{4U, 3U};
inline constexpr AspectRatio kAspect16x9{16U, 9U};
inline constexpr AspectRatio kAspect21x9{21U, 9U};
inline constexpr AspectRatio kAspect32x9{32U, 9U};

enum class AspectPolicy : std::uint8_t {
    preserve_4x3,
    preserve_16x9,
    preserve_21x9,
    preserve_32x9,
    custom,
};

enum class ScaleMode : std::uint8_t {
    fractional,
    integer,
};

struct ViewportRequest final {
    std::uint32_t window_width = 0U;
    std::uint32_t window_height = 0U;
    std::uint32_t source_width = 0U;
    std::uint32_t source_height = 0U;
    AspectPolicy aspect = AspectPolicy::preserve_4x3;
    AspectRatio custom_aspect{};
    ScaleMode scale = ScaleMode::fractional;
};

struct Viewport final {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t logical_width = 0U;
    std::uint32_t logical_height = 0U;
    std::uint64_t scale_numerator = 0U;
    std::uint64_t scale_denominator = 1U;
};

[[nodiscard]] HostStatus calculate_viewport(
    const ViewportRequest& request,
    Viewport& result) noexcept;

} // namespace clean_aot::host
