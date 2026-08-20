#include "psx_render/gpu_packet.h"

#include <array>

namespace psx::render {
namespace {

struct PolygonLayout {
    PolygonKind kind;
    ShadingMode shading;
    TextureMode texture;
    TransparencyMode transparency;
};

[[nodiscard]] std::optional<PolygonLayout> layout_for(std::uint8_t opcode) {
    if (opcode < 0x20u || opcode > 0x3fu) {
        return std::nullopt;
    }

    const bool raw_texture = (opcode & 0x01u) != 0u;
    const bool textured = (opcode & 0x04u) != 0u;

    const PolygonKind kind = (opcode & 0x08u) != 0u ? PolygonKind::Quadrilateral : PolygonKind::Triangle;
    const ShadingMode shading = (opcode & 0x10u) != 0u ? ShadingMode::Gouraud : ShadingMode::Flat;
    const TextureMode texture = !textured
        ? TextureMode::None
        : (raw_texture ? TextureMode::DirectUvRaw : TextureMode::DirectUv);
    const TransparencyMode transparency = (opcode & 0x02u) != 0u
        ? TransparencyMode::SemiTransparent
        : TransparencyMode::Opaque;
    return PolygonLayout{kind, shading, texture, transparency};
}

[[nodiscard]] NativeUv unpack_uv(std::uint32_t word) noexcept {
    return NativeUv{
        static_cast<std::uint8_t>(word & 0xffu),
        static_cast<std::uint8_t>((word >> 8u) & 0xffu),
        word,
    };
}

[[nodiscard]] Rgba8 unpack_color(std::uint32_t word) noexcept {
    return Rgba8{
        static_cast<std::uint8_t>(word & 0xffu),
        static_cast<std::uint8_t>((word >> 8u) & 0xffu),
        static_cast<std::uint8_t>((word >> 16u) & 0xffu),
        255,
    };
}

[[nodiscard]] PacketParseResult error(
    PacketParseErrorCode code,
    std::size_t index,
    std::string_view message) {
    return PacketParseResult{
        std::nullopt,
        PacketParseError{code, index, message},
        index,
    };
}

} // namespace

PacketParseResult GpuPacketParser::parse_polygon(std::span<const std::uint32_t> words) {
    if (words.empty()) {
        return error(PacketParseErrorCode::EmptyPacket, 0, "polygon packet has no header");
    }

    const std::uint32_t header = words.front();
    const std::uint8_t opcode = static_cast<std::uint8_t>(header >> 24u);
    const auto layout = layout_for(opcode);
    if (!layout.has_value()) {
        return error(PacketParseErrorCode::UnsupportedOpcode, 0, "opcode is not a PS1 polygon command");
    }

    const std::size_t vertex_count = layout->kind == PolygonKind::Triangle ? 3u : 4u;
    const std::size_t words_per_vertex = layout->texture == TextureMode::None ? 1u : 2u;
    const std::size_t color_words = layout->shading == ShadingMode::Gouraud ? vertex_count - 1u : 0u;
    const std::size_t required_words = 1u + vertex_count * words_per_vertex + color_words;
    if (words.size() < required_words) {
        return error(PacketParseErrorCode::TruncatedPacket, words.size(), "polygon packet is shorter than its command layout");
    }

    PolygonPacket packet{};
    packet.header = header;
    packet.opcode = opcode;
    packet.polygon_kind = layout->kind;
    packet.shading = layout->shading;
    packet.texture = layout->texture;
    packet.transparency = layout->transparency;
    packet.vertex_count = vertex_count;

    std::size_t cursor = 1u;
    if (layout->texture != TextureMode::DirectUvRaw) {
        packet.vertices[0].color = unpack_color(header);
    }
    packet.vertices[0].position = decode_native_position(words[cursor++]);
    if (layout->texture != TextureMode::None) {
        packet.vertices[0].uv = unpack_uv(words[cursor++]);
    }

    for (std::size_t vertex = 1u; vertex < vertex_count; ++vertex) {
        // Gouraud packet words are color, position, [UV] for each vertex after
        // the first one. Flat packets contain only position, [UV].
        if (layout->shading == ShadingMode::Gouraud) {
            const Rgba8 color = unpack_color(words[cursor++]);
            if (layout->texture != TextureMode::DirectUvRaw) {
                packet.vertices[vertex].color = color;
            }
        } else if (layout->texture != TextureMode::DirectUvRaw) {
            packet.vertices[vertex].color = packet.vertices[0].color;
        }
        packet.vertices[vertex].position = decode_native_position(words[cursor++]);
        if (layout->texture != TextureMode::None) {
            packet.vertices[vertex].uv = unpack_uv(words[cursor++]);
        }
    }

    return PacketParseResult{packet, std::nullopt, required_words};
}

} // namespace psx::render
