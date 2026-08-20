#include "psx_render/assembly.h"
#include "psx_render/gpu_packet.h"
#include "psx_render/interpolation.h"
#include "psx_render/provenance.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace psx::render;

int failures = 0;

void expect(bool condition, std::string_view expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(...) expect(static_cast<bool>(__VA_ARGS__), #__VA_ARGS__, __LINE__)

void expect_near(double actual, double expected, double tolerance, std::string_view expression, int line) {
    expect(std::abs(actual - expected) <= tolerance, expression, line);
}

#define EXPECT_NEAR(actual, expected, tolerance) expect_near((actual), (expected), (tolerance), #actual " ~= " #expected, __LINE__)

[[nodiscard]] constexpr std::uint32_t xy(std::int16_t x, std::int16_t y) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(x)) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16u);
}

[[nodiscard]] constexpr std::uint32_t raw_xy(std::uint16_t x, std::uint16_t y) noexcept {
    return static_cast<std::uint32_t>(x) | (static_cast<std::uint32_t>(y) << 16u);
}

[[nodiscard]] PreciseCoordinate precise(double x, double y, double z = 0.0, double w = 1.0) {
    return PreciseCoordinate{x, y, z, w};
}

[[nodiscard]] VertexCandidate candidate(const GteRegisterValue& value) {
    return value.candidate();
}

[[nodiscard]] std::vector<std::uint32_t> polygon_words(std::uint8_t opcode) {
    const bool gouraud = (opcode & 0x10u) != 0u;
    const bool quad = (opcode & 0x08u) != 0u;
    const bool textured = (opcode & 0x04u) != 0u;
    const std::size_t vertex_count = quad ? 4u : 3u;

    std::vector<std::uint32_t> words;
    words.reserve(12u);
    words.push_back((static_cast<std::uint32_t>(opcode) << 24u) | 0x00332211u);
    for (std::size_t vertex = 0u; vertex < vertex_count; ++vertex) {
        if (vertex != 0u && gouraud) {
            words.push_back(0x00030201u + static_cast<std::uint32_t>(vertex) * 0x00010101u);
        }
        const auto coordinate = static_cast<std::int16_t>(static_cast<int>(vertex) * 7 - 9);
        words.push_back(xy(coordinate, static_cast<std::int16_t>(coordinate + 3)));
        if (textured) {
            const std::uint32_t attribute = 0x8120u + static_cast<std::uint32_t>(vertex);
            const std::uint32_t uv =
                static_cast<std::uint32_t>(0x10u + vertex) |
                (static_cast<std::uint32_t>(0x20u + vertex) << 8u) |
                (attribute << 16u);
            words.push_back(uv);
        }
    }
    return words;
}

void parser_decodes_flat_triangle() {
    const std::array<std::uint32_t, 5> words{
        0x20112233u,
        xy(10, -20),
        xy(30, 40),
        xy(-50, 60),
        0xdeadbeefu,
    };

    const PacketParseResult result = GpuPacketParser::parse_polygon(words);
    EXPECT(result);
    EXPECT(result.words_consumed == 4u);
    EXPECT(result.packet->opcode == 0x20u);
    EXPECT(result.packet->polygon_kind == PolygonKind::Triangle);
    EXPECT(result.packet->shading == ShadingMode::Flat);
    EXPECT(result.packet->texture == TextureMode::None);
    EXPECT(result.packet->transparency == TransparencyMode::Opaque);
    EXPECT(result.packet->vertex_count == 3u);
    EXPECT(result.packet->vertices[0].position == NativePosition{10, -20});
    EXPECT(result.packet->vertices[2].position == NativePosition{-50, 60});
    EXPECT(result.packet->vertices[1].color == Rgba8{0x33u, 0x22u, 0x11u, 255u});
}

void parser_decodes_textured_gouraud_quad() {
    const std::array<std::uint32_t, 12> words{
        0x3e040506u,
        xy(1, 2), 0x00030201u,
        0x000000aau, xy(3, 4), 0x00060504u,
        0x000000bbu, xy(5, 6), 0x00090807u,
        0x000000ccu, xy(7, 8), 0x000c0b0au,
    };

    const PacketParseResult result = GpuPacketParser::parse_polygon(words);
    EXPECT(result);
    EXPECT(result.words_consumed == words.size());
    EXPECT(result.packet->polygon_kind == PolygonKind::Quadrilateral);
    EXPECT(result.packet->shading == ShadingMode::Gouraud);
    EXPECT(result.packet->texture == TextureMode::DirectUv);
    EXPECT(result.packet->transparency == TransparencyMode::SemiTransparent);
    EXPECT(result.packet->vertices[0].uv == NativeUv{1u, 2u, 0x00030201u});
    EXPECT(result.packet->vertices[3].uv == NativeUv{10u, 11u, 0x000c0b0au});
    EXPECT(result.packet->vertices[2].color == Rgba8{0xbbu, 0u, 0u, 255u});
}

void parser_rejects_invalid_or_truncated_packets() {
    const std::array<std::uint32_t, 1> unsupported{0x10000000u};
    const PacketParseResult unsupported_result = GpuPacketParser::parse_polygon(unsupported);
    EXPECT(!unsupported_result);
    EXPECT(unsupported_result.error->code == PacketParseErrorCode::UnsupportedOpcode);

    const std::array<std::uint32_t, 2> truncated{0x24000000u, xy(1, 2)};
    const PacketParseResult truncated_result = GpuPacketParser::parse_polygon(truncated);
    EXPECT(!truncated_result);
    EXPECT(truncated_result.error->code == PacketParseErrorCode::TruncatedPacket);
}

void parser_preserves_raw_texture_opcode() {
    const std::array<std::uint32_t, 7> words{
        0x25000000u,
        xy(1, 2), 0x12340302u,
        xy(3, 4), 0x56780605u,
        xy(5, 6), 0x9ab00807u,
    };

    const PacketParseResult result = GpuPacketParser::parse_polygon(words);
    EXPECT(result);
    EXPECT(result.packet->texture == TextureMode::DirectUvRaw);
    EXPECT(result.packet->transparency == TransparencyMode::Opaque);
    EXPECT(!result.packet->vertices[0].color.has_value());
    EXPECT(result.packet->vertices[1].uv == NativeUv{5u, 6u, 0x56780605u});
}

void parser_covers_every_polygon_opcode_and_prefix() {
    const PacketParseResult empty = GpuPacketParser::parse_polygon({});
    EXPECT(!empty);
    EXPECT(empty.error->code == PacketParseErrorCode::EmptyPacket);
    EXPECT(empty.words_consumed == 0u);

    for (std::uint32_t command = 0u; command <= 0xffu; ++command) {
        if (command >= 0x20u && command <= 0x3fu) {
            continue;
        }
        const std::array<std::uint32_t, 1> words{command << 24u};
        const PacketParseResult rejected = GpuPacketParser::parse_polygon(words);
        EXPECT(!rejected);
        EXPECT(rejected.error->code == PacketParseErrorCode::UnsupportedOpcode);
        EXPECT(rejected.words_consumed == 0u);
    }

    for (std::uint32_t command = 0x20u; command <= 0x3fu; ++command) {
        const auto opcode = static_cast<std::uint8_t>(command);
        const std::vector<std::uint32_t> words = polygon_words(opcode);
        const PacketParseResult result = GpuPacketParser::parse_polygon(words);
        EXPECT(result);
        if (!result) {
            continue;
        }

        const bool quad = (opcode & 0x08u) != 0u;
        const bool gouraud = (opcode & 0x10u) != 0u;
        const bool textured = (opcode & 0x04u) != 0u;
        const bool raw = textured && (opcode & 0x01u) != 0u;
        const bool semitransparent = (opcode & 0x02u) != 0u;
        const std::size_t vertex_count = quad ? 4u : 3u;

        EXPECT(result.words_consumed == words.size());
        EXPECT(result.packet->opcode == opcode);
        EXPECT(result.packet->polygon_kind == (quad ? PolygonKind::Quadrilateral : PolygonKind::Triangle));
        EXPECT(result.packet->shading == (gouraud ? ShadingMode::Gouraud : ShadingMode::Flat));
        EXPECT(result.packet->texture == (!textured
            ? TextureMode::None
            : (raw ? TextureMode::DirectUvRaw : TextureMode::DirectUv)));
        EXPECT(result.packet->transparency == (semitransparent
            ? TransparencyMode::SemiTransparent
            : TransparencyMode::Opaque));
        EXPECT(result.packet->vertex_count == vertex_count);

        for (std::size_t vertex = 0u; vertex < vertex_count; ++vertex) {
            EXPECT(result.packet->vertices[vertex].color.has_value() == !raw);
            EXPECT(result.packet->vertices[vertex].uv.has_value() == textured);
            if (textured) {
                const NativeUv& uv = *result.packet->vertices[vertex].uv;
                EXPECT(uv.raw == words[vertex == 0u
                    ? 2u
                    : 2u + vertex * (gouraud ? 3u : 2u)]);
                EXPECT(uv.attribute() == static_cast<std::uint16_t>(0x8120u + vertex));
            }
        }

        for (std::size_t prefix = 1u; prefix < words.size(); ++prefix) {
            const PacketParseResult truncated = GpuPacketParser::parse_polygon(
                std::span<const std::uint32_t>{words.data(), prefix});
            EXPECT(!truncated);
            EXPECT(truncated.error->code == PacketParseErrorCode::TruncatedPacket);
            EXPECT(truncated.error->word_index == prefix);
            EXPECT(truncated.words_consumed == prefix);
        }

        std::vector<std::uint32_t> with_sentinel = words;
        with_sentinel.push_back(0xdeadbeefu);
        const PacketParseResult bounded = GpuPacketParser::parse_polygon(with_sentinel);
        EXPECT(bounded);
        EXPECT(bounded.words_consumed == words.size());
    }
}

void parser_decodes_signed_11_bit_coordinates() {
    const std::array<std::uint32_t, 4> words{
        0x20000000u,
        raw_xy(0xf801u, 0xfc00u),
        raw_xy(0xfbffu, 0xffffu),
        raw_xy(0x0400u, 0x03ffu),
    };
    const PacketParseResult result = GpuPacketParser::parse_polygon(words);
    EXPECT(result);
    EXPECT(result.packet->vertices[0].position == NativePosition{1, -1024});
    EXPECT(result.packet->vertices[1].position == NativePosition{1023, -1});
    EXPECT(result.packet->vertices[2].position == NativePosition{-1024, 1023});
}

void gte_value_survives_memory_store_and_load() {
    ProvenanceState state;
    const GteRegisterValue gte = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{12, 24},
        precise(12.25, 24.5, 3.0, 2.0));
    const MemoryStoreResult stored = state.store_memory(
        0x100u,
        xy(12, 24),
        gte.candidate().enhancement);

    EXPECT(stored.carried_precise());
    EXPECT(stored.storage.has_value());
    EXPECT(state.is_current(gte.storage));
    const auto loaded = state.load_memory(0x100u);
    EXPECT(loaded.has_value());
    EXPECT(loaded->precise == gte.candidate().enhancement->precise);
    EXPECT(loaded->storage.has_value());
    EXPECT(state.is_current(*loaded->storage));
    EXPECT(loaded->candidate().enhancement->origin == gte.origin);
}

void same_address_memory_copy_keeps_live_provenance() {
    ProvenanceState state;
    const GteRegisterValue gte = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{8, 9},
        precise(8.5, 9.5));
    const MemoryStoreResult first = state.store_memory(
        0x140u,
        xy(8, 9),
        gte.candidate().enhancement);
    const auto loaded = state.load_memory(0x140u);
    EXPECT(first.carried_precise());
    EXPECT(loaded.has_value());
    if (!loaded.has_value() || !loaded->storage.has_value()) {
        return;
    }
    EXPECT(loaded->candidate().enhancement.has_value());
    if (!loaded->candidate().enhancement.has_value()) {
        return;
    }

    const MemoryStoreResult copied = state.store_memory(
        0x140u,
        loaded->raw,
        loaded->candidate().enhancement);
    EXPECT(copied.carried_precise());
    EXPECT(copied.storage.has_value());
    if (copied.storage.has_value()) {
        EXPECT(state.is_current(*copied.storage));
    }
    EXPECT(!state.is_current(*loaded->storage));
}

void provenance_invalidation_is_overlap_safe_and_deterministic() {
    ProvenanceState first_state;
    ProvenanceState second_state;
    const GteRegisterValue first_origin = first_state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{1, -1},
        precise(1.25, -1.5, 2.0, 3.0));
    const GteRegisterValue second_origin = second_state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{1, -1},
        precise(1.25, -1.5, 2.0, 3.0));
    EXPECT(first_origin.storage == second_origin.storage);
    EXPECT(first_origin.origin == second_origin.origin);
    EXPECT(first_state.is_known_origin(first_origin.origin));

    const MemoryStoreResult first_word = first_state.store_memory(
        0x300u,
        xy(1, -1),
        first_origin.candidate().enhancement);
    const GteRegisterValue adjacent_origin = first_state.write_gte(
        GteRegister::ScreenVertex1,
        NativePosition{2, 3},
        precise(2.25, 3.5));
    const MemoryStoreResult adjacent_word = first_state.store_memory(
        0x304u,
        xy(2, 3),
        adjacent_origin.candidate().enhancement);
    EXPECT(first_word.carried_precise());
    EXPECT(adjacent_word.carried_precise());
    EXPECT(first_state.invalidate_memory(0x302u, 1u) == 1u);
    EXPECT(!first_state.load_memory(0x300u).has_value());
    EXPECT(first_state.load_memory(0x304u).has_value());
    EXPECT(!first_state.is_current(*first_word.storage));
    EXPECT(first_state.is_current(*adjacent_word.storage));

    const MemoryStoreResult replacement = first_state.store_memory(
        0x300u,
        xy(1, -1),
        first_origin.candidate().enhancement);
    EXPECT(replacement.carried_precise());
    EXPECT(replacement.storage->generation == first_word.storage->generation + 1u);
    EXPECT(first_state.invalidate_memory(0x301u, 7u) == 2u);
    EXPECT(!first_state.load_memory(0x300u).has_value());
    EXPECT(!first_state.load_memory(0x304u).has_value());

    const MemoryStoreResult highest_word = first_state.store_memory(
        0xfffffffcu,
        xy(1, -1),
        first_origin.candidate().enhancement);
    EXPECT(highest_word.carried_precise());
    EXPECT(first_state.invalidate_memory(0xffffffffu, 2u) == 1u);
    EXPECT(!first_state.load_memory(0xfffffffcu).has_value());

    const MemoryStoreResult native_only = first_state.store_memory(0x308u, xy(4, 5));
    EXPECT(native_only.enhancement == MemoryStoreEnhancement::NativeOnly);
    const ProvenanceStamp fabricated_native_storage{
        ProvenanceSource{ProvenanceSourceKind::MemoryWord, 0x308u},
        1u,
    };
    EXPECT(!first_state.is_current(fabricated_native_storage));

    const MemoryStoreResult unaligned = first_state.store_memory(
        0x309u,
        xy(1, -1),
        first_origin.candidate().enhancement);
    EXPECT(unaligned.enhancement == MemoryStoreEnhancement::RejectedUnalignedAddress);
    EXPECT(!first_state.load_memory(0x309u).has_value());
    EXPECT(first_state.invalidate_memory(0x300u, 0u) == 0u);
}

void provenance_rejects_unknown_origins_and_invalid_values() {
    ProvenanceState state;
    const GteRegisterValue first = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 0},
        precise(0.25, 0.5));
    const VertexEnhancement forged{
        first.native,
        first.precise,
        first.storage,
        ProvenanceStamp{ProvenanceSource{ProvenanceSourceKind::External, 7u}, 1u},
    };
    EXPECT(!state.is_known_origin(forged.origin));
    const MemoryStoreResult unknown = state.store_memory(0x340u, xy(0, 0), forged);
    EXPECT(unknown.enhancement == MemoryStoreEnhancement::RejectedUnknownOrigin);

    VertexEnhancement forged_precise = *first.candidate().enhancement;
    forged_precise.precise.x += 100.0;
    EXPECT(!state.matches_current(forged_precise));
    const MemoryStoreResult mismatched_value = state.store_memory(0x348u, xy(0, 0), forged_precise);
    EXPECT(mismatched_value.enhancement == MemoryStoreEnhancement::RejectedValueMismatch);

    const GteRegisterValue invalid = state.write_gte(
        GteRegister::ScreenVertex1,
        NativePosition{10, 0},
        precise(std::numeric_limits<double>::quiet_NaN(), 0.0));
    const MemoryStoreResult non_finite = state.store_memory(
        0x344u,
        xy(10, 0),
        invalid.candidate().enhancement);
    EXPECT(non_finite.enhancement == MemoryStoreEnhancement::RejectedInvalidCoordinate);

    const GteRegisterValue third = state.write_gte(
        GteRegister::ScreenVertex2,
        NativePosition{0, 10},
        precise(0.0, 10.0));
    const std::array<std::uint32_t, 4> words{0x20000000u, xy(0, 0), xy(10, 0), xy(0, 10)};
    const PolygonPacket packet = GpuPacketParser::parse_polygon(words).packet.value();
    const std::array<VertexCandidate, 3> candidates{
        VertexCandidate{forged},
        candidate(invalid),
        candidate(third),
    };
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(assembled.triangles[0].fallback_reason == FallbackReason::UnknownOrigin);

    const std::array<VertexCandidate, 3> forged_value_candidates{
        VertexCandidate{forged_precise},
        candidate(invalid),
        candidate(third),
    };
    const AssembledPolygon forged_value = PolygonAssembler::assemble(
        packet,
        forged_value_candidates,
        &state);
    EXPECT(forged_value.triangles[0].fallback_reason == FallbackReason::ProvenanceValueMismatch);
}

void gte_invalidation_and_copy_identity_fail_closed() {
    ProvenanceState state;
    const GteRegisterValue original = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{7, 8},
        precise(7.25, 8.5));
    const GteRegisterValue copied = state.write_gte(
        GteRegister::ScreenVertex1,
        original.native,
        original.precise,
        original.candidate().enhancement);
    EXPECT(copied.origin == original.origin);

    const GteRegisterValue changed = state.write_gte(
        GteRegister::ScreenVertex2,
        NativePosition{8, 8},
        precise(8.25, 8.5),
        original.candidate().enhancement);
    EXPECT(changed.origin == changed.storage);

    state.invalidate_gte(GteRegister::ScreenVertex0);
    EXPECT(!state.read_gte(GteRegister::ScreenVertex0).has_value());
    EXPECT(!state.is_current(original.storage));
    EXPECT(state.is_known_origin(original.origin));
    const GteRegisterValue replacement = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{9, 9},
        precise(9.0, 9.0));
    EXPECT(replacement.storage.generation == original.storage.generation + 1u);

    bool threw = false;
    try {
        (void)state.read_gte(static_cast<GteRegister>(0xffu));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    EXPECT(threw);
}

void native_position_mismatch_rejects_precise_carry() {
    ProvenanceState state;
    const GteRegisterValue gte = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{4, 5},
        precise(4.25, 5.25));
    const MemoryStoreResult stored = state.store_memory(
        0x1c0u,
        xy(4, 6),
        gte.candidate().enhancement);
    EXPECT(stored.enhancement == MemoryStoreEnhancement::RejectedNativeMismatch);
    const auto loaded = state.load_memory(0x1c0u);
    EXPECT(loaded.has_value());
    if (loaded.has_value()) {
        EXPECT(!loaded->candidate().enhancement.has_value());
    }

    const std::array<std::uint32_t, 4> words{0x20000000u, xy(4, 5), xy(10, 0), xy(0, 10)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const GteRegisterValue v1 = state.write_gte(GteRegister::ScreenVertex1, NativePosition{10, 0}, precise(10, 0));
    const GteRegisterValue v2 = state.write_gte(GteRegister::ScreenVertex2, NativePosition{0, 10}, precise(0, 10));
    const VertexCandidate mismatched{
        VertexEnhancement{NativePosition{4, 6}, gte.precise, gte.storage, gte.origin},
    };
    const std::array<VertexCandidate, 3> candidates{mismatched, candidate(v1), candidate(v2)};
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(assembled.triangles[0].fallback_reason == FallbackReason::NativeValueMismatch);
}

void stale_generation_forces_whole_triangle_fallback() {
    ProvenanceState state;
    const GteRegisterValue stale = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 0},
        precise(0.0, 0.0));
    const GteRegisterValue second = state.write_gte(
        GteRegister::ScreenVertex1,
        NativePosition{100, 0},
        precise(100.0, 0.0));
    const GteRegisterValue third = state.write_gte(
        GteRegister::ScreenVertex2,
        NativePosition{0, 100},
        precise(0.0, 100.0));
    const GteRegisterValue replacement = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{1, 1},
        precise(1.0, 1.0));
    EXPECT(state.is_current(replacement.storage));
    const MemoryStoreResult rejected = state.store_memory(
        0x180u,
        xy(0, 0),
        stale.candidate().enhancement);
    EXPECT(rejected.enhancement == MemoryStoreEnhancement::RejectedStaleSource);
    const auto rejected_value = state.load_memory(0x180u);
    EXPECT(rejected_value.has_value());
    if (rejected_value.has_value()) {
        EXPECT(!rejected_value->precise.has_value());
    }

    const std::array<std::uint32_t, 4> words{0x20000000u, xy(0, 0), xy(100, 0), xy(0, 100)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const std::array<VertexCandidate, 3> candidates{candidate(stale), candidate(second), candidate(third)};
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);

    EXPECT(assembled.triangles.size() == 1u);
    EXPECT(!assembled.triangles[0].uses_precise_geometry());
    EXPECT(assembled.triangles[0].fallback_reason == FallbackReason::StaleGeneration);
    for (const EmittedVertex& vertex : assembled.triangles[0].vertices) {
        EXPECT(std::holds_alternative<NativeFallbackIdentity>(vertex.identity));
        EXPECT(!vertex.precise.has_value());
    }
}

void mixed_provenance_never_emits_partial_triangle() {
    ProvenanceState state;
    const GteRegisterValue first = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 0},
        precise(0.0, 0.0));
    const GteRegisterValue third = state.write_gte(
        GteRegister::ScreenVertex2,
        NativePosition{0, 100},
        precise(0.0, 100.0));
    const std::array<std::uint32_t, 4> words{0x20000000u, xy(0, 0), xy(100, 0), xy(0, 100)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const std::array<VertexCandidate, 3> candidates{
        candidate(first),
        VertexCandidate{std::nullopt},
        candidate(third),
    };

    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(!assembled.triangles[0].uses_precise_geometry());
    EXPECT(assembled.triangles[0].fallback_reason == FallbackReason::MissingCandidate);
    for (const EmittedVertex& vertex : assembled.triangles[0].vertices) {
        EXPECT(std::holds_alternative<NativeFallbackIdentity>(vertex.identity));
    }
}

void quad_is_split_into_two_provenance_checked_triangles() {
    ProvenanceState state;
    const std::array<std::uint32_t, 5> words{0x28000000u, xy(0, 0), xy(10, 0), xy(10, 10), xy(0, 10)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const GteRegisterValue v0 = state.write_gte(GteRegister::ScreenVertex0, NativePosition{0, 0}, precise(0, 0));
    const MemoryStoreResult v0_stored = state.store_memory(
        0x200u,
        xy(0, 0),
        v0.candidate().enhancement);
    const GteRegisterValue v1 = state.write_gte(GteRegister::ScreenVertex1, NativePosition{10, 0}, precise(10, 0));
    const GteRegisterValue v2 = state.write_gte(GteRegister::ScreenVertex2, NativePosition{10, 10}, precise(10, 10));
    const GteRegisterValue v3 = state.write_gte(GteRegister::ScreenVertex0, NativePosition{0, 10}, precise(0, 10));
    const auto v0_loaded = state.load_memory(0x200u);
    EXPECT(v0_stored.carried_precise());
    EXPECT(v0_loaded.has_value());
    const std::array<VertexCandidate, 4> candidates{
        v0_loaded->candidate(), candidate(v1), candidate(v2), candidate(v3),
    };

    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(assembled.triangles.size() == 2u);
    EXPECT(assembled.triangles[0].vertices[0].native == NativePosition{0, 0});
    EXPECT(assembled.triangles[0].vertices[1].native == NativePosition{10, 0});
    EXPECT(assembled.triangles[0].vertices[2].native == NativePosition{10, 10});
    EXPECT(assembled.triangles[1].vertices[0].native == NativePosition{10, 0});
    EXPECT(assembled.triangles[1].vertices[1].native == NativePosition{10, 10});
    EXPECT(assembled.triangles[1].vertices[2].native == NativePosition{0, 10});
    EXPECT(assembled.triangles[0].uses_precise_geometry());
    EXPECT(assembled.triangles[1].uses_precise_geometry());
}

void quad_provenance_fallback_is_triangle_local() {
    ProvenanceState state;
    const std::array<std::uint32_t, 5> words{0x28000000u, xy(0, 0), xy(10, 0), xy(10, 10), xy(0, 10)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const GteRegisterValue v0_source = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 0},
        precise(0, 0));
    const MemoryStoreResult v0_stored = state.store_memory(
        0x280u,
        xy(0, 0),
        v0_source.candidate().enhancement);
    const GteRegisterValue v1 = state.write_gte(GteRegister::ScreenVertex1, NativePosition{10, 0}, precise(10, 0));
    const GteRegisterValue v2 = state.write_gte(GteRegister::ScreenVertex2, NativePosition{10, 10}, precise(10, 10));
    const GteRegisterValue stale_v3 = state.write_gte(GteRegister::ScreenVertex0, NativePosition{0, 10}, precise(0, 10));
    const GteRegisterValue current_v3 = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 10},
        precise(0, 10));
    EXPECT(state.is_current(current_v3.storage));
    const auto v0 = state.load_memory(0x280u);
    EXPECT(v0_stored.carried_precise());
    EXPECT(v0.has_value());
    if (!v0.has_value()) {
        return;
    }

    const std::array<VertexCandidate, 4> candidates{
        v0->candidate(), candidate(v1), candidate(v2), candidate(stale_v3),
    };
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(assembled.triangles.size() == 2u);
    EXPECT(assembled.triangles[0].uses_precise_geometry());
    EXPECT(!assembled.triangles[1].uses_precise_geometry());
    EXPECT(assembled.triangles[1].fallback_reason == FallbackReason::StaleGeneration);
}

void quad_missing_candidate_fallback_is_triangle_local() {
    ProvenanceState state;
    const std::array<std::uint32_t, 5> words{0x28000000u, xy(0, 0), xy(10, 0), xy(10, 10), xy(0, 10)};
    const PolygonPacket packet = GpuPacketParser::parse_polygon(words).packet.value();
    const GteRegisterValue v0 = state.write_gte(
        GteRegister::ScreenVertex0,
        NativePosition{0, 0},
        precise(0, 0));
    const GteRegisterValue v1 = state.write_gte(
        GteRegister::ScreenVertex1,
        NativePosition{10, 0},
        precise(10, 0));
    const GteRegisterValue v2 = state.write_gte(
        GteRegister::ScreenVertex2,
        NativePosition{10, 10},
        precise(10, 10));
    const std::array<VertexCandidate, 3> candidates{candidate(v0), candidate(v1), candidate(v2)};

    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, candidates, &state);
    EXPECT(!assembled.error.has_value());
    EXPECT(assembled.triangles.size() == 2u);
    EXPECT(assembled.triangles[0].uses_precise_geometry());
    EXPECT(assembled.triangles[1].fallback_reason == FallbackReason::MissingCandidate);
    for (const EmittedVertex& vertex : assembled.triangles[1].vertices) {
        EXPECT(!vertex.precise.has_value());
        EXPECT(std::holds_alternative<NativeFallbackIdentity>(vertex.identity));
    }
}

void assembler_rejects_inconsistent_packet_metadata() {
    const std::array<std::uint32_t, 4> words{0x20000000u, xy(0, 0), xy(1, 0), xy(0, 1)};
    PolygonPacket packet = GpuPacketParser::parse_polygon(words).packet.value();
    packet.vertex_count = 4u;
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, {}, nullptr);
    EXPECT(assembled.error == PolygonAssemblyError::InvalidVertexCount);
    EXPECT(assembled.triangles.empty());

    packet.polygon_kind = PolygonKind::Quadrilateral;
    packet.vertex_count = 3u;
    const AssembledPolygon malformed_quad = PolygonAssembler::assemble(packet, {}, nullptr);
    EXPECT(malformed_quad.error == PolygonAssemblyError::InvalidVertexCount);
    EXPECT(malformed_quad.triangles.empty());

    packet.polygon_kind = static_cast<PolygonKind>(0xffu);
    packet.vertex_count = 4u;
    const AssembledPolygon invalid_kind = PolygonAssembler::assemble(packet, {}, nullptr);
    EXPECT(invalid_kind.error == PolygonAssemblyError::InvalidPolygonKind);
    EXPECT(invalid_kind.triangles.empty());
}

void fallback_has_explicit_native_identity() {
    const std::array<std::uint32_t, 4> words{0x20000000u, xy(-4, 7), xy(12, 7), xy(0, -9)};
    const auto packet = GpuPacketParser::parse_polygon(words).packet.value();
    const std::array<VertexCandidate, 3> no_candidates{};
    const AssembledPolygon assembled = PolygonAssembler::assemble(packet, no_candidates, nullptr);

    EXPECT(assembled.triangles.size() == 1u);
    for (const EmittedVertex& vertex : assembled.triangles[0].vertices) {
        const auto* identity = std::get_if<NativeFallbackIdentity>(&vertex.identity);
        EXPECT(identity != nullptr);
        EXPECT(identity->position == vertex.native);
    }
}

void perspective_interpolation_uses_clip_w() {
    const auto weights = perspective_correct_weights(
        BarycentricWeights{0.25, 0.25, 0.5},
        std::array<double, 3>{1.0, 2.0, 4.0});
    EXPECT(weights.has_value());
    EXPECT_NEAR(weights->a, 0.5, 1.0e-12);
    EXPECT_NEAR(weights->b, 0.25, 1.0e-12);
    EXPECT_NEAR(weights->c, 0.25, 1.0e-12);

    const auto uv = perspective_interpolate(
        BarycentricWeights{0.25, 0.25, 0.5},
        std::array<Vec2, 3>{Vec2{0.0, 0.0}, Vec2{10.0, 20.0}, Vec2{20.0, 0.0}},
        std::array<double, 3>{1.0, 2.0, 4.0});
    EXPECT(uv.has_value());
    EXPECT_NEAR(uv->x, 7.5, 1.0e-12);
    EXPECT_NEAR(uv->y, 5.0, 1.0e-12);

    const auto invalid = perspective_correct_weights(
        BarycentricWeights{0.5, 0.5, 0.0},
        std::array<double, 3>{1.0, 0.0, 1.0});
    EXPECT(!invalid.has_value());

    const auto non_finite_attribute = perspective_interpolate(
        BarycentricWeights{0.5, 0.5, 0.0},
        std::array<Vec2, 3>{Vec2{0.0, 0.0}, Vec2{NAN, 1.0}, Vec2{2.0, 2.0}},
        std::array<double, 3>{1.0, 1.0, 1.0});
    EXPECT(!non_finite_attribute.has_value());
}

void perspective_interpolation_handles_extreme_and_signed_w() {
    const BarycentricWeights screen{0.2, 0.3, 0.5};
    const std::array<double, 3> scales{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(),
    };
    for (const double scale : scales) {
        const auto weights = perspective_correct_weights(screen, {scale, scale, scale});
        EXPECT(weights.has_value());
        if (weights.has_value()) {
            EXPECT_NEAR(weights->a, screen.a, 1.0e-12);
            EXPECT_NEAR(weights->b, screen.b, 1.0e-12);
            EXPECT_NEAR(weights->c, screen.c, 1.0e-12);
        }
    }

    const auto negative = perspective_correct_weights(screen, {-2.0, -4.0, -8.0});
    EXPECT(negative.has_value());
    EXPECT_NEAR(negative->a, 8.0 / 19.0, 1.0e-12);
    EXPECT_NEAR(negative->b, 6.0 / 19.0, 1.0e-12);
    EXPECT_NEAR(negative->c, 5.0 / 19.0, 1.0e-12);

    EXPECT(!perspective_correct_weights(screen, {1.0, -1.0, 1.0}).has_value());
    EXPECT(!perspective_correct_weights(screen, {1.0, 0.0, 1.0}).has_value());
    EXPECT(!perspective_correct_weights(screen, {1.0, -0.0, 1.0}).has_value());
    EXPECT(!perspective_correct_weights(BarycentricWeights{1.0, -1.0, 0.0}, {1.0, 1.0, 1.0}).has_value());
    EXPECT(!perspective_correct_weights(
        BarycentricWeights{1.0, -1.0 + std::numeric_limits<double>::epsilon(), 0.0},
        {1.0, 1.0, 1.0}).has_value());

    const double maximum = std::numeric_limits<double>::max();
    const auto finite_extreme = perspective_interpolate(
        BarycentricWeights{0.25, 0.25, 0.5},
        std::array<Vec2, 3>{Vec2{maximum, maximum}, Vec2{maximum, maximum}, Vec2{maximum, maximum}},
        std::array<double, 3>{1.0, 1.0, 1.0});
    EXPECT(finite_extreme.has_value());
    EXPECT(std::isfinite(finite_extreme->x));
    EXPECT(std::isfinite(finite_extreme->y));

    const auto overflowing = perspective_interpolate(
        BarycentricWeights{2.0, -1.0, 0.0},
        std::array<Vec2, 3>{Vec2{maximum, 0.0}, Vec2{-maximum, 0.0}, Vec2{0.0, 0.0}},
        std::array<double, 3>{1.0, 1.0, 1.0});
    EXPECT(!overflowing.has_value());
}

void perspective_attributes_are_all_or_nothing() {
    const BarycentricWeights screen{0.25, 0.25, 0.5};
    const std::array<double, 3> clip_w{1.0, 2.0, 4.0};
    const std::array<PerspectiveAttributes, 3> complete{
        PerspectiveAttributes{Vec2{0.0, 0.0}, Vec4{1.0, 0.0, 0.0, 1.0}},
        PerspectiveAttributes{Vec2{10.0, 20.0}, Vec4{0.0, 1.0, 0.0, 1.0}},
        PerspectiveAttributes{Vec2{20.0, 0.0}, Vec4{0.0, 0.0, 1.0, 1.0}},
    };
    const auto sample = perspective_interpolate_attributes(screen, complete, clip_w);
    EXPECT(sample.has_value());
    EXPECT(sample->uv.has_value());
    EXPECT(sample->color.has_value());
    EXPECT_NEAR(sample->uv->x, 7.5, 1.0e-12);
    EXPECT_NEAR(sample->color->x, 0.5, 1.0e-12);
    EXPECT_NEAR(sample->color->y, 0.25, 1.0e-12);
    EXPECT_NEAR(sample->color->z, 0.25, 1.0e-12);

    auto missing_color = complete;
    missing_color[1].color.reset();
    EXPECT(!perspective_interpolate_attributes(screen, missing_color, clip_w).has_value());

    auto invalid_color = complete;
    invalid_color[1].color->z = std::numeric_limits<double>::infinity();
    const auto rejected = perspective_interpolate_attributes(screen, invalid_color, clip_w);
    EXPECT(!rejected.has_value());

    auto missing_uv = complete;
    missing_uv[2].uv.reset();
    EXPECT(!perspective_interpolate_attributes(screen, missing_uv, clip_w).has_value());
}

} // namespace

int main() {
    parser_decodes_flat_triangle();
    parser_decodes_textured_gouraud_quad();
    parser_rejects_invalid_or_truncated_packets();
    parser_preserves_raw_texture_opcode();
    parser_covers_every_polygon_opcode_and_prefix();
    parser_decodes_signed_11_bit_coordinates();
    gte_value_survives_memory_store_and_load();
    same_address_memory_copy_keeps_live_provenance();
    provenance_invalidation_is_overlap_safe_and_deterministic();
    provenance_rejects_unknown_origins_and_invalid_values();
    gte_invalidation_and_copy_identity_fail_closed();
    native_position_mismatch_rejects_precise_carry();
    stale_generation_forces_whole_triangle_fallback();
    mixed_provenance_never_emits_partial_triangle();
    quad_is_split_into_two_provenance_checked_triangles();
    quad_provenance_fallback_is_triangle_local();
    quad_missing_candidate_fallback_is_triangle_local();
    assembler_rejects_inconsistent_packet_metadata();
    fallback_has_explicit_native_identity();
    perspective_interpolation_uses_clip_w();
    perspective_interpolation_handles_extreme_and_signed_w();
    perspective_attributes_are_all_or_nothing();

    if (failures != 0) {
        std::cerr << failures << " geometry test(s) failed\n";
        return 1;
    }
    std::cout << "all geometry tests passed\n";
    return 0;
}
