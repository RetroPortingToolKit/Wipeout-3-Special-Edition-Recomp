#pragma once

#include <cstdint>

#include "clean_aot/host/host.hpp"
#include "clean_aot/host/viewport.hpp"

namespace clean_aot::host {

struct RenderVertex final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    std::uint32_t color_rgba = 0U;
};

struct RenderGeometry final {
    const RenderVertex* vertices = nullptr;
    std::uint32_t vertex_count = 0U;
    const std::uint16_t* indices = nullptr;
    std::uint32_t index_count = 0U;
};

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
        std::uint32_t source_key,
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
