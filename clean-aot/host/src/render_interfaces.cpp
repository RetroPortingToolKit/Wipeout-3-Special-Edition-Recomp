#include "clean_aot/host/render_interfaces.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <variant>

namespace clean_aot::host {
namespace {

[[nodiscard]] std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (unsigned shift = 0u; shift < 64u; shift += 8u) {
        hash ^= (value >> shift) & 0xffu;
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] std::uint32_t pack_color(const psx::render::Rgba8& color) noexcept {
    return static_cast<std::uint32_t>(color.r) |
           (static_cast<std::uint32_t>(color.g) << 8u) |
           (static_cast<std::uint32_t>(color.b) << 16u) |
           (static_cast<std::uint32_t>(color.a) << 24u);
}

} // namespace

GeometryBridgeResult build_render_geometry(
    const psx::render::AssembledPolygon& polygon,
    const psx::render::ProjectionPlan& projection,
    std::span<RenderVertex> vertices,
    std::span<std::uint16_t> indices) noexcept {
    GeometryBridgeResult result{};
    if (polygon.error.has_value() || !std::isfinite(projection.horizontal_clip_scale) ||
        projection.horizontal_clip_scale <= 0.0 ||
        polygon.triangles.size() > std::numeric_limits<std::uint16_t>::max() / 3u) {
        return result;
    }
    const std::size_t count = polygon.triangles.size() * 3u;
    if (vertices.size() < count || indices.size() < count) {
        result.status = HostStatus{HostCode::queue_full};
        return result;
    }

    std::uint64_t digest = 14695981039346656037ULL;
    std::size_t output = 0u;
    for (const psx::render::EmittedTriangle& triangle : polygon.triangles) {
        if (!projection.exact_native_4x3 && !triangle.uses_precise_geometry()) {
            return GeometryBridgeResult{HostStatus{HostCode::unsupported}};
        }
        for (const psx::render::EmittedVertex& source : triangle.vertices) {
            RenderVertex vertex{};
            vertex.x = static_cast<float>(source.native.x);
            vertex.y = static_cast<float>(source.native.y);
            if (source.precise.has_value() && triangle.uses_precise_geometry()) {
                vertex.x = static_cast<float>(source.precise->x *
                                              projection.horizontal_clip_scale);
                vertex.y = static_cast<float>(source.precise->y);
                vertex.z = static_cast<float>(source.precise->z);
                vertex.w = static_cast<float>(source.precise->w);
                const auto* identity = std::get_if<psx::render::PreciseVertexIdentity>(
                    &source.identity);
                if (identity == nullptr) {
                    return GeometryBridgeResult{};
                }
                vertex.storage_generation = identity->storage.generation;
                vertex.origin_generation = identity->origin.generation;
            }
            if (source.uv.has_value()) {
                vertex.u = static_cast<float>(source.uv->u) / 255.0F;
                vertex.v = static_cast<float>(source.uv->v) / 255.0F;
            }
            if (source.color.has_value()) {
                vertex.color_rgba = pack_color(*source.color);
            }
            vertices[output] = vertex;
            indices[output] = static_cast<std::uint16_t>(output);
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.x));
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.y));
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.z));
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.w));
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.u));
            digest = mix(digest, std::bit_cast<std::uint32_t>(vertex.v));
            digest = mix(digest, vertex.color_rgba);
            digest = mix(digest, vertex.storage_generation);
            digest = mix(digest, vertex.origin_generation);
            ++output;
        }
    }
    result.status = HostStatus{};
    result.geometry = RenderGeometry{vertices.data(), static_cast<std::uint32_t>(count),
                                     indices.data(), static_cast<std::uint32_t>(count)};
    result.culling = projection.culling;
    result.visual_digest = digest;
    return result;
}

GeometryBridgeResult build_gpu_polygon_geometry(
    const gpu::PolygonEvent& event,
    std::span<const psx::render::VertexCandidate> candidates,
    const psx::render::ProvenanceState* provenance,
    const psx::render::ProjectionPlan& projection,
    std::span<RenderVertex> vertices,
    std::span<std::uint16_t> indices) noexcept {
    if (event.raw_word_count == 0u || event.raw_word_count > event.raw_words.size()) {
        return GeometryBridgeResult{};
    }
    const auto parsed = psx::render::GpuPacketParser::parse_polygon(
        std::span<const std::uint32_t>(event.raw_words.data(), event.raw_word_count));
    if (!parsed.packet.has_value() || parsed.words_consumed != event.raw_word_count) {
        return GeometryBridgeResult{};
    }
    const psx::render::AssembledPolygon assembled =
        psx::render::PolygonAssembler::assemble(*parsed.packet, candidates, provenance);
    return build_render_geometry(assembled, projection, vertices, indices);
}

HostStatus bind_scanout_frame(
    const raster::ScanoutPlan& plan,
    std::span<const std::uint8_t> rgb888,
    std::uint64_t sequence,
    std::uint64_t update_index,
    FrameSubmission& submission) noexcept {
    const std::uint64_t pitch = static_cast<std::uint64_t>(plan.logical_width) * 3u;
    if (plan.logical_width == 0u || plan.logical_height == 0u ||
        plan.required_rgb_bytes != pitch * plan.logical_height ||
        rgb888.size() != plan.required_rgb_bytes ||
        pitch > std::numeric_limits<std::uint32_t>::max() ||
        rgb888.size() > std::numeric_limits<std::uint32_t>::max()) {
        return HostStatus{HostCode::invalid_argument};
    }
    submission = FrameSubmission{
        sequence, update_index,
        FrameView{rgb888.data(), static_cast<std::uint32_t>(rgb888.size()),
                  plan.logical_width, plan.logical_height,
                  static_cast<std::uint32_t>(pitch), PixelFormat::rgb8_unorm}};
    return HostStatus{};
}

} // namespace clean_aot::host
