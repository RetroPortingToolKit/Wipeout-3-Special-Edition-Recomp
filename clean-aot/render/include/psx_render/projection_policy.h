#pragma once

#include <cstdint>

namespace psx::render {

enum class ProjectionPolicy : std::uint8_t {
    PreserveNative4x3,
    ExpandHorizontal,
};

enum class CullingPolicy : std::uint8_t {
    NativeHardware,
    MatchExpandedProjection,
};

struct ProjectionRequest {
    ProjectionPolicy projection = ProjectionPolicy::PreserveNative4x3;
    CullingPolicy culling = CullingPolicy::NativeHardware;
    std::uint32_t target_aspect_width = 4;
    std::uint32_t target_aspect_height = 3;
};

struct ProjectionPlan {
    double horizontal_clip_scale = 1.0;
    CullingPolicy culling = CullingPolicy::NativeHardware;
    bool exact_native_4x3 = true;
};

struct ProjectionResult {
    bool accepted = false;
    ProjectionPlan plan{};
};

class ProjectionPolicyPlanner final {
public:
    [[nodiscard]] static ProjectionResult plan(const ProjectionRequest& request) noexcept;
};

} // namespace psx::render
