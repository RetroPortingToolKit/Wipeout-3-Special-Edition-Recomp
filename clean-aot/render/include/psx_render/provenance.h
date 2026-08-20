#pragma once

#include "psx_render/geometry_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace psx::render {

enum class GteRegister : std::uint8_t {
    ScreenVertex0,
    ScreenVertex1,
    ScreenVertex2,
};

struct VertexEnhancement {
    NativePosition native{};
    PreciseCoordinate precise{};
    ProvenanceStamp storage{};
    ProvenanceStamp origin{};

    friend constexpr bool operator==(const VertexEnhancement&, const VertexEnhancement&) = default;
};

struct VertexCandidate {
    std::optional<VertexEnhancement> enhancement;
};

struct GteRegisterValue {
    NativePosition native{};
    PreciseCoordinate precise{};
    ProvenanceStamp storage{};
    ProvenanceStamp origin{};

    [[nodiscard]] VertexCandidate candidate() const;
};

struct MemoryWordValue {
    std::uint32_t raw{};
    NativePosition native{};
    std::optional<PreciseCoordinate> precise;
    std::optional<ProvenanceStamp> storage;
    std::optional<ProvenanceStamp> origin;

    [[nodiscard]] VertexCandidate candidate() const;
};

enum class MemoryStoreEnhancement : std::uint8_t {
    NativeOnly,
    PreciseCarried,
    RejectedUnalignedAddress,
    RejectedStaleSource,
    RejectedUnknownOrigin,
    RejectedValueMismatch,
    RejectedNativeMismatch,
    RejectedInvalidCoordinate,
};

struct MemoryStoreResult {
    MemoryStoreEnhancement enhancement{MemoryStoreEnhancement::NativeOnly};
    std::optional<ProvenanceStamp> storage;

    [[nodiscard]] bool carried_precise() const noexcept {
        return enhancement == MemoryStoreEnhancement::PreciseCarried;
    }
};

class ProvenanceState final {
public:
    [[nodiscard]] GteRegisterValue write_gte(
        GteRegister reg,
        NativePosition native,
        PreciseCoordinate precise,
        std::optional<VertexEnhancement> inherited = std::nullopt);

    [[nodiscard]] std::optional<GteRegisterValue> read_gte(GteRegister reg) const;
    void invalidate_gte(GteRegister reg);

    [[nodiscard]] MemoryStoreResult store_memory(
        std::uint32_t byte_address,
        std::uint32_t raw,
        std::optional<VertexEnhancement> source = std::nullopt);

    [[nodiscard]] std::optional<MemoryWordValue> load_memory(std::uint32_t byte_address) const;
    [[nodiscard]] std::size_t invalidate_memory(
        std::uint32_t byte_address,
        std::uint32_t byte_count);

    [[nodiscard]] bool is_current(ProvenanceStamp stamp) const noexcept;
    [[nodiscard]] bool is_known_origin(ProvenanceStamp stamp) const noexcept;
    [[nodiscard]] bool matches_current(const VertexEnhancement& enhancement) const noexcept;

private:
    struct GteSlot {
        std::uint64_t generation{};
        std::optional<GteRegisterValue> value;
    };

    struct MemorySlot {
        std::uint64_t generation{};
        std::optional<MemoryWordValue> value;
    };

    [[nodiscard]] static ProvenanceSource gte_source(GteRegister reg) noexcept;
    [[nodiscard]] static ProvenanceSource memory_source(std::uint32_t byte_address) noexcept;
    [[nodiscard]] std::uint64_t next_gte_generation(GteRegister reg);
    [[nodiscard]] std::uint64_t next_memory_generation(std::uint32_t byte_address);

    std::array<GteSlot, 3> gte_{};
    std::map<std::uint32_t, MemorySlot> memory_{};
};

} // namespace psx::render
