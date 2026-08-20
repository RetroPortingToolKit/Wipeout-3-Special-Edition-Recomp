#include "psx_render/interpolation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace psx::render {
namespace {

[[nodiscard]] bool finite(double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(const BarycentricWeights& weights) noexcept {
    return finite(weights.a) && finite(weights.b) && finite(weights.c);
}

[[nodiscard]] bool finite(const Vec2& value) noexcept {
    return finite(value.x) && finite(value.y);
}

[[nodiscard]] bool finite(const Vec4& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

[[nodiscard]] bool valid_w(double value) noexcept {
    return finite(value) && value != 0.0;
}

[[nodiscard]] std::optional<double> weighted_component(
    BarycentricWeights weights,
    std::array<double, 3> values) {
    if (!finite(values[0]) || !finite(values[1]) || !finite(values[2])) {
        return std::nullopt;
    }

    const double magnitude = std::max({std::abs(values[0]), std::abs(values[1]), std::abs(values[2])});
    if (magnitude == 0.0) {
        return 0.0;
    }

    const double scaled =
        weights.a * (values[0] / magnitude) +
        weights.b * (values[1] / magnitude) +
        weights.c * (values[2] / magnitude);
    const double result = scaled * magnitude;
    if (!finite(scaled) || !finite(result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<Vec2> interpolate(
    BarycentricWeights weights,
    const std::array<Vec2, 3>& values) {
    if (!finite(values[0]) || !finite(values[1]) || !finite(values[2])) {
        return std::nullopt;
    }
    const auto x = weighted_component(weights, {values[0].x, values[1].x, values[2].x});
    const auto y = weighted_component(weights, {values[0].y, values[1].y, values[2].y});
    if (!x.has_value() || !y.has_value()) {
        return std::nullopt;
    }
    return Vec2{*x, *y};
}

[[nodiscard]] std::optional<Vec4> interpolate(
    BarycentricWeights weights,
    const std::array<Vec4, 3>& values) {
    if (!finite(values[0]) || !finite(values[1]) || !finite(values[2])) {
        return std::nullopt;
    }
    const auto x = weighted_component(weights, {values[0].x, values[1].x, values[2].x});
    const auto y = weighted_component(weights, {values[0].y, values[1].y, values[2].y});
    const auto z = weighted_component(weights, {values[0].z, values[1].z, values[2].z});
    const auto w = weighted_component(weights, {values[0].w, values[1].w, values[2].w});
    if (!x.has_value() || !y.has_value() || !z.has_value() || !w.has_value()) {
        return std::nullopt;
    }
    return Vec4{*x, *y, *z, *w};
}

} // namespace

std::optional<BarycentricWeights> perspective_correct_weights(
    BarycentricWeights screen_weights,
    std::array<double, 3> clip_w) {
    if (!finite(screen_weights) || !valid_w(clip_w[0]) || !valid_w(clip_w[1]) || !valid_w(clip_w[2])) {
        return std::nullopt;
    }

    const bool sign = std::signbit(clip_w[0]);
    if (std::signbit(clip_w[1]) != sign || std::signbit(clip_w[2]) != sign) {
        return std::nullopt;
    }

    const double minimum_w = std::min({std::abs(clip_w[0]), std::abs(clip_w[1]), std::abs(clip_w[2])});
    std::array<double, 3> terms{
        screen_weights.a * (minimum_w / std::abs(clip_w[0])),
        screen_weights.b * (minimum_w / std::abs(clip_w[1])),
        screen_weights.c * (minimum_w / std::abs(clip_w[2])),
    };
    if (!finite(terms[0]) || !finite(terms[1]) || !finite(terms[2])) {
        return std::nullopt;
    }

    const double term_scale = std::max({std::abs(terms[0]), std::abs(terms[1]), std::abs(terms[2])});
    if (term_scale == 0.0) {
        return std::nullopt;
    }
    for (double& term : terms) {
        term /= term_scale;
    }

    const double denominator = terms[0] + terms[1] + terms[2];
    const double denominator_scale = std::abs(terms[0]) + std::abs(terms[1]) + std::abs(terms[2]);
    constexpr double cancellation_limit = 64.0 * std::numeric_limits<double>::epsilon();
    if (!finite(denominator) ||
        std::abs(denominator) <= cancellation_limit * denominator_scale) {
        return std::nullopt;
    }

    const BarycentricWeights corrected{
        terms[0] / denominator,
        terms[1] / denominator,
        terms[2] / denominator,
    };
    if (!finite(corrected)) {
        return std::nullopt;
    }
    return corrected;
}

std::optional<Vec2> perspective_interpolate(
    BarycentricWeights screen_weights,
    std::array<Vec2, 3> values,
    std::array<double, 3> clip_w) {
    const auto weights = perspective_correct_weights(screen_weights, clip_w);
    if (!weights.has_value()) {
        return std::nullopt;
    }
    return interpolate(*weights, values);
}

std::optional<Vec4> perspective_interpolate(
    BarycentricWeights screen_weights,
    std::array<Vec4, 3> values,
    std::array<double, 3> clip_w) {
    const auto weights = perspective_correct_weights(screen_weights, clip_w);
    if (!weights.has_value()) {
        return std::nullopt;
    }
    return interpolate(*weights, values);
}

std::optional<PerspectiveAttributes> perspective_interpolate_attributes(
    BarycentricWeights screen_weights,
    std::array<PerspectiveAttributes, 3> values,
    std::array<double, 3> clip_w) {
    const bool any_uv = values[0].uv.has_value() || values[1].uv.has_value() || values[2].uv.has_value();
    const bool all_uv = values[0].uv.has_value() && values[1].uv.has_value() && values[2].uv.has_value();
    const bool any_color = values[0].color.has_value() || values[1].color.has_value() || values[2].color.has_value();
    const bool all_color = values[0].color.has_value() && values[1].color.has_value() && values[2].color.has_value();
    if (any_uv != all_uv || any_color != all_color) {
        return std::nullopt;
    }

    const auto weights = perspective_correct_weights(screen_weights, clip_w);
    if (!weights.has_value()) {
        return std::nullopt;
    }

    PerspectiveAttributes output{};
    if (all_uv) {
        const std::array<Vec2, 3> uv_values{
            values[0].uv.value_or(Vec2{}),
            values[1].uv.value_or(Vec2{}),
            values[2].uv.value_or(Vec2{}),
        };
        output.uv = interpolate(*weights, uv_values);
        if (!output.uv.has_value()) {
            return std::nullopt;
        }
    }
    if (all_color) {
        const std::array<Vec4, 3> color_values{
            values[0].color.value_or(Vec4{}),
            values[1].color.value_or(Vec4{}),
            values[2].color.value_or(Vec4{}),
        };
        output.color = interpolate(*weights, color_values);
        if (!output.color.has_value()) {
            return std::nullopt;
        }
    }
    return output;
}

} // namespace psx::render
