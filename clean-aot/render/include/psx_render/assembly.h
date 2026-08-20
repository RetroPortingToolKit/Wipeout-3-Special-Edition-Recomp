#pragma once

#include "psx_render/gpu_packet.h"
#include "psx_render/provenance.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace psx::render {

enum class FallbackReason : std::uint8_t {
    MissingCandidate,
    MissingProvenance,
    StaleGeneration,
    NativeValueMismatch,
    InvalidCoordinate,
    MissingValidationState,
    UnknownOrigin,
    ProvenanceValueMismatch,
};

enum class PolygonAssemblyError : std::uint8_t {
    InvalidPolygonKind,
    InvalidVertexCount,
};

struct PreciseVertexIdentity {
    ProvenanceStamp storage{};
    ProvenanceStamp origin{};
};

struct NativeFallbackIdentity {
    NativePosition position{};
};

using EmittedVertexIdentity = std::variant<PreciseVertexIdentity, NativeFallbackIdentity>;

struct EmittedVertex {
    NativePosition native{};
    std::optional<NativeUv> uv;
    std::optional<Rgba8> color;
    std::optional<PreciseCoordinate> precise;
    EmittedVertexIdentity identity{NativeFallbackIdentity{}};
};

struct EmittedTriangle {
    std::array<EmittedVertex, 3> vertices{};
    std::optional<FallbackReason> fallback_reason;

    [[nodiscard]] bool uses_precise_geometry() const noexcept {
        return !fallback_reason.has_value();
    }
};

struct AssembledPolygon {
    std::vector<EmittedTriangle> triangles;
    std::optional<PolygonAssemblyError> error;
};

class PolygonAssembler final {
public:
    [[nodiscard]] static AssembledPolygon assemble(
        const PolygonPacket& packet,
        std::span<const VertexCandidate> candidates,
        const ProvenanceState* state);
};

} // namespace psx::render
