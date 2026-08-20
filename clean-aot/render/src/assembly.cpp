#include "psx_render/assembly.h"

#include <cmath>

namespace psx::render {
namespace {

[[nodiscard]] bool finite(const PreciseCoordinate& coordinate) noexcept {
    return std::isfinite(coordinate.x) && std::isfinite(coordinate.y) &&
        std::isfinite(coordinate.z) && std::isfinite(coordinate.w);
}

[[nodiscard]] EmittedVertex native_vertex(const PacketVertex& source) {
    return EmittedVertex{
        source.position,
        source.uv,
        source.color,
        std::nullopt,
        NativeFallbackIdentity{source.position},
    };
}

[[nodiscard]] EmittedVertex precise_vertex(
    const PacketVertex& source,
    const VertexEnhancement& enhancement) {
    return EmittedVertex{
        source.position,
        source.uv,
        source.color,
        enhancement.precise,
        PreciseVertexIdentity{enhancement.storage, enhancement.origin},
    };
}

[[nodiscard]] std::optional<FallbackReason> validate_candidate(
    const VertexEnhancement& enhancement,
    NativePosition packet_position,
    const ProvenanceState* state) {
    if (state == nullptr) {
        return FallbackReason::MissingValidationState;
    }

    if (enhancement.native != packet_position) {
        return FallbackReason::NativeValueMismatch;
    }
    if (!finite(enhancement.precise)) {
        return FallbackReason::InvalidCoordinate;
    }
    if (!state->is_current(enhancement.storage)) {
        return FallbackReason::StaleGeneration;
    }
    if (enhancement.storage.generation == 0u || enhancement.origin.generation == 0u) {
        return FallbackReason::MissingProvenance;
    }
    if (!state->is_known_origin(enhancement.origin)) {
        return FallbackReason::UnknownOrigin;
    }
    if (!state->matches_current(enhancement)) {
        return FallbackReason::ProvenanceValueMismatch;
    }
    return std::nullopt;
}

} // namespace

AssembledPolygon PolygonAssembler::assemble(
    const PolygonPacket& packet,
    std::span<const VertexCandidate> candidates,
    const ProvenanceState* state) {
    AssembledPolygon assembled{};
    const std::array<std::array<std::size_t, 3>, 2> triangle_indices{
        std::array<std::size_t, 3>{0u, 1u, 2u},
        std::array<std::size_t, 3>{1u, 2u, 3u},
    };
    std::size_t triangle_count = 0u;
    switch (packet.polygon_kind) {
    case PolygonKind::Triangle:
        triangle_count = 1u;
        break;
    case PolygonKind::Quadrilateral:
        triangle_count = 2u;
        break;
    default:
        assembled.error = PolygonAssemblyError::InvalidPolygonKind;
        return assembled;
    }
    const std::size_t expected_vertex_count = triangle_count == 1u ? 3u : 4u;
    if (packet.vertex_count != expected_vertex_count) {
        assembled.error = PolygonAssemblyError::InvalidVertexCount;
        return assembled;
    }
    assembled.triangles.reserve(triangle_count);

    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const auto indices = triangle_indices[triangle];
        std::array<VertexEnhancement, 3> validated{};
        std::size_t validated_count = 0u;
        std::optional<FallbackReason> fallback_reason;
        for (std::size_t corner = 0u; corner < indices.size(); ++corner) {
            const std::size_t index = indices[corner];
            if (index >= candidates.size()) {
                fallback_reason = FallbackReason::MissingCandidate;
                break;
            }
            if (!candidates[index].enhancement.has_value()) {
                fallback_reason = FallbackReason::MissingCandidate;
                break;
            }
            const VertexEnhancement enhancement = candidates[index].enhancement.value_or(VertexEnhancement{});
            const std::optional<FallbackReason> reason = validate_candidate(
                enhancement,
                packet.vertices[index].position,
                state);
            if (reason.has_value()) {
                fallback_reason = reason;
                break;
            }
            validated[corner] = enhancement;
            ++validated_count;
        }
        if (!fallback_reason.has_value() && validated_count != indices.size()) {
            fallback_reason = FallbackReason::MissingCandidate;
        }

        EmittedTriangle output{};
        output.fallback_reason = fallback_reason;
        for (std::size_t corner = 0; corner < indices.size(); ++corner) {
            const std::size_t vertex_index = indices[corner];
            if (fallback_reason.has_value()) {
                output.vertices[corner] = native_vertex(packet.vertices[vertex_index]);
            } else {
                output.vertices[corner] = precise_vertex(
                    packet.vertices[vertex_index],
                    validated[corner]);
            }
        }
        assembled.triangles.push_back(output);
    }

    return assembled;
}

} // namespace psx::render
