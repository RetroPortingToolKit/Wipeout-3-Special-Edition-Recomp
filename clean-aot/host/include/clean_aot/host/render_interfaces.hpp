#pragma once

#include <cstdint>
#include <span>

#include "clean_aot/host/host.hpp"
#include "clean_aot/host/viewport.hpp"
#include "clean_aot/gpu/gpu.hpp"
#include "clean_aot/raster/raster.hpp"
#include "psx_render/assembly.h"
#include "psx_render/projection_policy.h"
#include "psx_render/replacement_manifest.h"

namespace clean_aot::host {

struct RenderVertex final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::uint32_t color_rgba = 0U;
    std::uint64_t storage_generation = 0U;
    std::uint64_t origin_generation = 0U;
};

struct RenderGeometry final {
    const RenderVertex* vertices = nullptr;
    std::uint32_t vertex_count = 0U;
    const std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0U;
};

struct ReplacementImage final {
    psx::render::ReplacementIdentity identity{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::span<const std::uint8_t> rgba8{};
};

class IReplacementUploader {
public:
    virtual ~IReplacementUploader() = default;
    [[nodiscard]] virtual HostStatus upload_replacement(
        const ReplacementImage& replacement,
        std::uint64_t& handle) noexcept = 0;
};

struct GeometryBridgeResult final {
    HostStatus status{HostCode::invalid_argument};
    RenderGeometry geometry{};
    psx::render::CullingPolicy culling = psx::render::CullingPolicy::NativeHardware;
    std::uint64_t visual_digest = 0U;
};

[[nodiscard]] GeometryBridgeResult build_render_geometry(
    const psx::render::AssembledPolygon& polygon,
    const psx::render::ProjectionPlan& projection,
    std::span<RenderVertex> vertices,
    std::span<std::uint16_t> indices) noexcept;

[[nodiscard]] GeometryBridgeResult build_gpu_polygon_geometry(
    const gpu::PolygonEvent& event,
    std::span<const psx::render::VertexCandidate> candidates,
    const psx::render::ProvenanceState* provenance,
    const psx::render::ProjectionPlan& projection,
    std::span<RenderVertex> vertices,
    std::span<std::uint16_t> indices) noexcept;

[[nodiscard]] HostStatus bind_scanout_frame(
    const raster::ScanoutPlan& plan,
    std::span<const std::uint8_t> rgb888,
    std::uint64_t sequence,
    std::uint64_t update_index,
    FrameSubmission& submission) noexcept;

// These are intentionally narrow seams. They describe where the clean GPU,
// PGXP, and replacement-texture implementations will attach without making
// the host know anything about guest VRAM or game policy.
class IPgxpTransformProvider {
public:
    virtual ~IPgxpTransformProvider() = default;
    [[nodiscard]] virtual HostStatus transform(
        RenderGeometry geometry,
        RenderGeometry& transformed) noexcept = 0;
};

class ITextureReplacementProvider {
public:
    virtual ~ITextureReplacementProvider() = default;
    [[nodiscard]] virtual HostStatus resolve(
        psx::render::ReplacementAssetKind kind,
        psx::render::ReplacementKey source_key,
        std::uint64_t source_generation,
        std::uint64_t& texture_handle) noexcept = 0;
};

class IGpuEmulationProvider {
public:
    virtual ~IGpuEmulationProvider() = default;
    [[nodiscard]] virtual HostStatus build_frame(
        std::uint64_t update_index,
        RenderGeometry& geometry) noexcept = 0;
};

struct PresentationContext final {
    void* command_buffer = nullptr;
    void* color_target = nullptr;
    std::uint32_t target_width = 0U;
    std::uint32_t target_height = 0U;
    Viewport viewport{};
};

class IFrameEncoder {
public:
    virtual ~IFrameEncoder() = default;
    [[nodiscard]] virtual HostStatus encode(
        const PresentationContext& context,
        const FrameView& frame) noexcept = 0;
};

} // namespace clean_aot::host
