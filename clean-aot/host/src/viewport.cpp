#include "clean_aot/host/viewport.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace clean_aot::host {
namespace {

[[nodiscard]] AspectRatio requested_aspect(
    const ViewportRequest& request) noexcept {
    switch (request.aspect) {
    case AspectPolicy::preserve_4x3:
        return kAspect4x3;
    case AspectPolicy::preserve_16x9:
        return kAspect16x9;
    case AspectPolicy::preserve_21x9:
        return kAspect21x9;
    case AspectPolicy::preserve_32x9:
        return kAspect32x9;
    case AspectPolicy::custom:
        return request.custom_aspect;
    }
    return AspectRatio{};
}

[[nodiscard]] bool multiply_checked(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0U &&
        right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

} // namespace

HostStatus calculate_viewport(
    const ViewportRequest& request,
    Viewport& result) noexcept {
    const AspectRatio aspect = requested_aspect(request);
    if (request.window_width == 0U || request.window_height == 0U ||
        request.source_width == 0U || request.source_height == 0U ||
        aspect.width == 0U || aspect.height == 0U) {
        return HostStatus{HostCode::invalid_argument};
    }

    const std::uint32_t divisor = std::gcd(aspect.width, aspect.height);
    const std::uint64_t ratio_width = aspect.width / divisor;
    const std::uint64_t ratio_height = aspect.height / divisor;
    const std::uint64_t logical_units =
        (static_cast<std::uint64_t>(request.source_height) +
         ratio_height - 1U) / ratio_height;
    std::uint64_t logical_width = 0U;
    std::uint64_t logical_height = 0U;
    if (!multiply_checked(ratio_width, logical_units, logical_width) ||
        !multiply_checked(ratio_height, logical_units, logical_height) ||
        logical_width > std::numeric_limits<std::uint32_t>::max() ||
        logical_height > std::numeric_limits<std::uint32_t>::max()) {
        return HostStatus{HostCode::invalid_argument};
    }

    result = Viewport{};
    result.logical_width = static_cast<std::uint32_t>(logical_width);
    result.logical_height = static_cast<std::uint32_t>(logical_height);

    if (request.scale == ScaleMode::integer) {
        const std::uint32_t width_scale =
            request.window_width / result.logical_width;
        const std::uint32_t height_scale =
            request.window_height / result.logical_height;
        const std::uint32_t integer_scale =
            width_scale < height_scale ? width_scale : height_scale;
        if (integer_scale > 0U) {
            result.width = result.logical_width * integer_scale;
            result.height = result.logical_height * integer_scale;
            result.scale_numerator = integer_scale;
            result.scale_denominator = 1U;
        }
    }

    if (result.width == 0U || result.height == 0U) {
        const std::uint64_t ratio_scale = std::min(
            static_cast<std::uint64_t>(request.window_width) / ratio_width,
            static_cast<std::uint64_t>(request.window_height) / ratio_height);
        std::uint64_t fitted_width = 0U;
        std::uint64_t fitted_height = 0U;
        if (!multiply_checked(ratio_width, ratio_scale, fitted_width) ||
            !multiply_checked(ratio_height, ratio_scale, fitted_height) ||
            fitted_width > std::numeric_limits<std::uint32_t>::max() ||
            fitted_height > std::numeric_limits<std::uint32_t>::max()) {
            return HostStatus{HostCode::invalid_argument};
        }
        result.width = static_cast<std::uint32_t>(fitted_width);
        result.height = static_cast<std::uint32_t>(fitted_height);
        if (result.width == 0U || result.height == 0U) {
            return HostStatus{HostCode::invalid_argument};
        }
        result.scale_numerator = result.width;
        result.scale_denominator = result.logical_width;
    }

    const std::uint64_t x =
        (static_cast<std::uint64_t>(request.window_width) - result.width) / 2U;
    const std::uint64_t y =
        (static_cast<std::uint64_t>(request.window_height) - result.height) / 2U;
    if (x > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) ||
        y > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        return HostStatus{HostCode::invalid_argument};
    }
    result.x = static_cast<std::int32_t>(x);
    result.y = static_cast<std::int32_t>(y);
    return HostStatus{};
}

} // namespace clean_aot::host
