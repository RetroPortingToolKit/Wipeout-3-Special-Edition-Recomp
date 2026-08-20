#pragma once

#include "psx_render/geometry_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace psx::render {

enum class PolygonKind : std::uint8_t {
    Triangle,
    Quadrilateral,
};

enum class ShadingMode : std::uint8_t {
    Flat,
    Gouraud,
};

enum class TextureMode : std::uint8_t {
    None,
    DirectUv,
    DirectUvRaw,
};

enum class TransparencyMode : std::uint8_t {
    Opaque,
    SemiTransparent,
};

struct PacketVertex {
    NativePosition position{};
    std::optional<NativeUv> uv;
    std::optional<Rgba8> color;
};

struct PolygonPacket {
    std::uint32_t header{};
    std::uint8_t opcode{};
    PolygonKind polygon_kind{};
    ShadingMode shading{};
    TextureMode texture{};
    TransparencyMode transparency{};
    std::size_t vertex_count{};
    std::array<PacketVertex, 4> vertices{};
};

enum class PacketParseErrorCode : std::uint8_t {
    EmptyPacket,
    UnsupportedOpcode,
    TruncatedPacket,
};

struct PacketParseError {
    PacketParseErrorCode code{};
    std::size_t word_index{};
    std::string_view message{};
};

struct PacketParseResult {
    std::optional<PolygonPacket> packet;
    std::optional<PacketParseError> error;
    std::size_t words_consumed{};

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return packet.has_value();
    }
};

class GpuPacketParser final {
public:
    [[nodiscard]] static PacketParseResult parse_polygon(std::span<const std::uint32_t> words);
};

} // namespace psx::render
