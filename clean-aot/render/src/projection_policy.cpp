#include "psx_render/projection_policy.h"

#include <cmath>

namespace psx::render {

ProjectionResult ProjectionPolicyPlanner::plan(const ProjectionRequest& request) noexcept {
    ProjectionResult result{};
    if (request.target_aspect_width == 0u || request.target_aspect_height == 0u ||
        (request.culling != CullingPolicy::NativeHardware &&
         request.culling != CullingPolicy::MatchExpandedProjection)) {
        return result;
    }

    switch (request.projection) {
    case ProjectionPolicy::PreserveNative4x3:
        if (request.culling != CullingPolicy::NativeHardware) {
            return result;
        }
        result.accepted = true;
        return result;
    case ProjectionPolicy::ExpandHorizontal: {
        const double target = static_cast<double>(request.target_aspect_width) /
                              static_cast<double>(request.target_aspect_height);
        constexpr double native = 4.0 / 3.0;
        if (!std::isfinite(target) || target < native) {
            return result;
        }
        result.accepted = true;
        result.plan.horizontal_clip_scale = native / target;
        result.plan.culling = request.culling;
        result.plan.exact_native_4x3 = target == native;
        return result;
    }
    default:
        return result;
    }
}

} // namespace psx::render
