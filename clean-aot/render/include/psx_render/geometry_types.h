#pragma once

#include <cstdint>

namespace psx::render {

struct NativePosition {
    std::int16_t x{};
    std::int16_t y{};

    friend constexpr bool operator==(const NativePosition&, const NativePosition&) = default;
};

[[nodiscard]] constexpr std::int16_t decode_signed_11(std::uint32_t value) noexcept {
    const std::uint32_t encoded = value & 0x7ffu;
    const std::int32_t decoded = (encoded & 0x400u) != 0u
        ? static_cast<std::int32_t>(encoded) - 0x800
        : static_cast<std::int32_t>(encoded);
    return static_cast<std::int16_t>(decoded);
}

[[nodiscard]] constexpr NativePosition decode_native_position(std::uint32_t word) noexcept {
    return NativePosition{
        decode_signed_11(word),
        decode_signed_11(word >> 16u),
    };
}

struct NativeUv {
    std::uint8_t u{};
    std::uint8_t v{};
    std::uint32_t raw{};

    [[nodiscard]] constexpr std::uint16_t attribute() const noexcept {
        return static_cast<std::uint16_t>(raw >> 16u);
    }

    friend constexpr bool operator==(const NativeUv&, const NativeUv&) = default;
};

struct Rgba8 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

struct Vec2 {
    double x{};
    double y{};

    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec4 {
    double x{};
    double y{};
    double z{};
    double w{1.0};

    friend constexpr bool operator==(const Vec4&, const Vec4&) = default;
};

using PreciseCoordinate = Vec4;

enum class ProvenanceSourceKind : std::uint8_t {
    GteRegister,
    MemoryWord,
    External,
};

struct ProvenanceSource {
    ProvenanceSourceKind kind{};
    std::uint32_t location{};

    friend constexpr bool operator==(const ProvenanceSource&, const ProvenanceSource&) = default;
};

struct ProvenanceStamp {
    ProvenanceSource source{};
    std::uint64_t generation{};

    friend constexpr bool operator==(const ProvenanceStamp&, const ProvenanceStamp&) = default;
};

} // namespace psx::render
