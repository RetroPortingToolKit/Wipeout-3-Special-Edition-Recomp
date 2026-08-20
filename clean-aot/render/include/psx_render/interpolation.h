#pragma once

#include "psx_render/geometry_types.h"

#include <array>
#include <optional>

namespace psx::render {

struct BarycentricWeights {
    double a{};
    double b{};
    double c{};

    friend constexpr bool operator==(const BarycentricWeights&, const BarycentricWeights&) = default;
};

struct PerspectiveAttributes {
    std::optional<Vec2> uv;
    std::optional<Vec4> color;

    friend constexpr bool operator==(const PerspectiveAttributes&, const PerspectiveAttributes&) = default;
};

[[nodiscard]] std::optional<BarycentricWeights> perspective_correct_weights(
    BarycentricWeights screen_weights,
    std::array<double, 3> clip_w);

[[nodiscard]] std::optional<Vec2> perspective_interpolate(
    BarycentricWeights screen_weights,
    std::array<Vec2, 3> values,
    std::array<double, 3> clip_w);

[[nodiscard]] std::optional<Vec4> perspective_interpolate(
    BarycentricWeights screen_weights,
    std::array<Vec4, 3> values,
    std::array<double, 3> clip_w);

[[nodiscard]] std::optional<PerspectiveAttributes> perspective_interpolate_attributes(
    BarycentricWeights screen_weights,
    std::array<PerspectiveAttributes, 3> values,
    std::array<double, 3> clip_w);

} // namespace psx::render
