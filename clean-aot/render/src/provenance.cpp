#include "psx_render/provenance.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace psx::render {
namespace {

[[nodiscard]] std::size_t gte_index(GteRegister reg) {
    const std::size_t index = static_cast<std::size_t>(reg);
    if (index >= 3u) {
        throw std::out_of_range("invalid GTE screen-vertex register");
    }
    return index;
}

[[nodiscard]] ProvenanceStamp stamp_for(ProvenanceSource source, std::uint64_t generation) noexcept {
    return ProvenanceStamp{source, generation};
}

[[nodiscard]] bool finite(const PreciseCoordinate& coordinate) noexcept {
    return std::isfinite(coordinate.x) && std::isfinite(coordinate.y) &&
        std::isfinite(coordinate.z) && std::isfinite(coordinate.w);
}

} // namespace

VertexCandidate GteRegisterValue::candidate() const {
    return VertexCandidate{VertexEnhancement{native, precise, storage, origin}};
}

VertexCandidate MemoryWordValue::candidate() const {
    if (!precise.has_value() || !storage.has_value() || !origin.has_value()) {
        return VertexCandidate{std::nullopt};
    }
    return VertexCandidate{VertexEnhancement{
        native,
        precise.value_or(PreciseCoordinate{}),
        storage.value_or(ProvenanceStamp{}),
        origin.value_or(ProvenanceStamp{}),
    }};
}

GteRegisterValue ProvenanceState::write_gte(
    GteRegister reg,
    NativePosition native,
    PreciseCoordinate precise,
    std::optional<VertexEnhancement> inherited) {
    const std::size_t index = gte_index(reg);
    GteSlot& slot = gte_[index];
    const VertexEnhancement inherited_value = inherited.value_or(VertexEnhancement{});
    const bool inherited_current = inherited.has_value() &&
        is_current(inherited_value.storage) &&
        is_known_origin(inherited_value.origin) &&
        matches_current(inherited_value) &&
        inherited_value.native == native &&
        inherited_value.precise == precise &&
        finite(precise);
    slot.generation = next_gte_generation(reg);
    const ProvenanceStamp storage = stamp_for(gte_source(reg), slot.generation);

    ProvenanceStamp origin = storage;
    if (inherited_current) {
        origin = inherited_value.origin;
    }

    const GteRegisterValue value{native, precise, storage, origin};
    slot.value = value;
    return value;
}

std::optional<GteRegisterValue> ProvenanceState::read_gte(GteRegister reg) const {
    const GteSlot& slot = gte_[gte_index(reg)];
    if (!slot.value.has_value()) {
        return std::nullopt;
    }
    return slot.value;
}

void ProvenanceState::invalidate_gte(GteRegister reg) {
    gte_[gte_index(reg)].value.reset();
}

MemoryStoreResult ProvenanceState::store_memory(
    std::uint32_t byte_address,
    std::uint32_t raw,
    std::optional<VertexEnhancement> source) {
    if ((byte_address & 0x3u) != 0u) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedUnalignedAddress, std::nullopt};
    }

    const NativePosition native = decode_native_position(raw);
    const VertexEnhancement source_value = source.value_or(VertexEnhancement{});
    const bool source_current = source.has_value() && is_current(source_value.storage);
    const bool origin_known = source.has_value() && is_known_origin(source_value.origin);
    const bool source_matches = source.has_value() && matches_current(source_value);
    MemorySlot& slot = memory_[byte_address];
    slot.generation = next_memory_generation(byte_address);
    MemoryWordValue& stored_value = slot.value.emplace(
        MemoryWordValue{raw, native, std::nullopt, std::nullopt, std::nullopt});

    if (!source.has_value()) {
        return MemoryStoreResult{MemoryStoreEnhancement::NativeOnly, std::nullopt};
    }
    if (!source_current) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedStaleSource, std::nullopt};
    }
    if (!origin_known) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedUnknownOrigin, std::nullopt};
    }
    if (source_value.native != native) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedNativeMismatch, std::nullopt};
    }
    if (!finite(source_value.precise)) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedInvalidCoordinate, std::nullopt};
    }
    if (!source_matches) {
        return MemoryStoreResult{MemoryStoreEnhancement::RejectedValueMismatch, std::nullopt};
    }

    const ProvenanceStamp storage = stamp_for(memory_source(byte_address), slot.generation);
    stored_value.precise = source_value.precise;
    stored_value.storage = storage;
    stored_value.origin = source_value.origin;
    return MemoryStoreResult{MemoryStoreEnhancement::PreciseCarried, storage};
}

std::optional<MemoryWordValue> ProvenanceState::load_memory(std::uint32_t byte_address) const {
    const auto found = memory_.find(byte_address);
    if (found == memory_.end() || !found->second.value.has_value()) {
        return std::nullopt;
    }
    return found->second.value;
}

std::size_t ProvenanceState::invalidate_memory(
    std::uint32_t byte_address,
    std::uint32_t byte_count) {
    if (byte_count == 0u) {
        return 0u;
    }

    const std::uint64_t write_begin = byte_address;
    const std::uint64_t write_end = write_begin + byte_count;
    const std::uint32_t first_word = byte_address & ~0x3u;
    std::size_t invalidated = 0u;
    for (auto found = memory_.lower_bound(first_word);
         found != memory_.end() && static_cast<std::uint64_t>(found->first) < write_end;
         ++found) {
        MemorySlot& slot = found->second;
        if (!slot.value.has_value()) {
            continue;
        }
        const std::uint64_t word_end = static_cast<std::uint64_t>(found->first) + 4u;
        if (write_begin < word_end) {
            slot.value.reset();
            ++invalidated;
        }
    }
    return invalidated;
}

bool ProvenanceState::is_current(ProvenanceStamp stamp) const noexcept {
    if (stamp.generation == 0u) {
        return false;
    }

    switch (stamp.source.kind) {
    case ProvenanceSourceKind::GteRegister: {
        if (stamp.source.location >= gte_.size()) {
            return false;
        }
        const GteSlot& slot = gte_[stamp.source.location];
        const GteRegisterValue value = slot.value.value_or(GteRegisterValue{});
        return slot.value.has_value() &&
            slot.generation == stamp.generation &&
            value.storage == stamp;
    }
    case ProvenanceSourceKind::MemoryWord: {
        const auto found = memory_.find(stamp.source.location);
        if (found == memory_.end() || !found->second.value.has_value()) {
            return false;
        }
        const MemoryWordValue value = found->second.value.value_or(MemoryWordValue{});
        return found->second.generation == stamp.generation &&
            value.storage.has_value() &&
            value.storage.value_or(ProvenanceStamp{}) == stamp;
    }
    case ProvenanceSourceKind::External:
        return false;
    }

    return false;
}

bool ProvenanceState::is_known_origin(ProvenanceStamp stamp) const noexcept {
    if (stamp.source.kind != ProvenanceSourceKind::GteRegister ||
        stamp.source.location >= gte_.size() ||
        stamp.generation == 0u) {
        return false;
    }
    return stamp.generation <= gte_[stamp.source.location].generation;
}

bool ProvenanceState::matches_current(const VertexEnhancement& enhancement) const noexcept {
    if (!is_current(enhancement.storage)) {
        return false;
    }

    switch (enhancement.storage.source.kind) {
    case ProvenanceSourceKind::GteRegister: {
        const GteSlot& slot = gte_[enhancement.storage.source.location];
        const GteRegisterValue value = slot.value.value_or(GteRegisterValue{});
        return VertexEnhancement{value.native, value.precise, value.storage, value.origin} == enhancement;
    }
    case ProvenanceSourceKind::MemoryWord: {
        const auto found = memory_.find(enhancement.storage.source.location);
        if (found == memory_.end() || !found->second.value.has_value()) {
            return false;
        }
        const MemoryWordValue value = found->second.value.value_or(MemoryWordValue{});
        if (!value.precise.has_value() || !value.storage.has_value() || !value.origin.has_value()) {
            return false;
        }
        return VertexEnhancement{
            value.native,
            value.precise.value_or(PreciseCoordinate{}),
            value.storage.value_or(ProvenanceStamp{}),
            value.origin.value_or(ProvenanceStamp{}),
        } == enhancement;
    }
    case ProvenanceSourceKind::External:
        return false;
    }

    return false;
}

ProvenanceSource ProvenanceState::gte_source(GteRegister reg) noexcept {
    return ProvenanceSource{ProvenanceSourceKind::GteRegister, static_cast<std::uint32_t>(reg)};
}

ProvenanceSource ProvenanceState::memory_source(std::uint32_t byte_address) noexcept {
    return ProvenanceSource{ProvenanceSourceKind::MemoryWord, byte_address};
}

std::uint64_t ProvenanceState::next_gte_generation(GteRegister reg) {
    GteSlot& slot = gte_[gte_index(reg)];
    if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("GTE provenance generation exhausted");
    }
    return slot.generation + 1u;
}

std::uint64_t ProvenanceState::next_memory_generation(std::uint32_t byte_address) {
    MemorySlot& slot = memory_[byte_address];
    if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("memory provenance generation exhausted");
    }
    return slot.generation + 1u;
}

} // namespace psx::render
