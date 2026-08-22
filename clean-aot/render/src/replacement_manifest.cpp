#include "psx_render/replacement_manifest.h"

#include <algorithm>
#include <charconv>
#include <limits>

namespace psx::render {
namespace {

[[nodiscard]] bool valid_sha256(std::string_view value) noexcept {
    return value.size() == 64u && std::all_of(value.begin(), value.end(), [](char digit) {
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    });
}

[[nodiscard]] bool valid_path(std::string_view value) noexcept {
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0u;
    while (start <= value.size()) {
        const std::size_t end = value.find('/', start);
        const std::string_view part = value.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1u;
    }
    return true;
}

[[nodiscard]] bool entry_less(const ReplacementManifestEntry& left,
                              const ReplacementManifestEntry& right) noexcept {
    if (left.kind != right.kind) {
        return left.kind < right.kind;
    }
    return left.key < right.key;
}

} // namespace

std::optional<ReplacementKey> parse_replacement_key(std::string_view value) noexcept {
    if (value.size() != 17u || value[8] != '-') {
        return std::nullopt;
    }
    const auto lowercase_hex = [](char digit) noexcept {
        return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
    };
    for (std::size_t index = 0u; index < value.size(); ++index) {
        if (index != 8u && !lowercase_hex(value[index])) {
            return std::nullopt;
        }
    }
    ReplacementKey key{};
    const auto first = std::from_chars(value.data(), value.data() + 8, key.primary, 16);
    const auto second = std::from_chars(value.data() + 9, value.data() + 17,
                                        key.secondary, 16);
    if (first.ec != std::errc{} || first.ptr != value.data() + 8 ||
        second.ec != std::errc{} || second.ptr != value.data() + 17) {
        return std::nullopt;
    }
    return key;
}

std::optional<ReplacementManifest> ReplacementManifest::create(
    std::span<const ReplacementManifestEntry> entries,
    std::uint64_t manifest_generation) {
    if (entries.empty() || manifest_generation == 0u ||
        entries.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    ReplacementManifest manifest;
    manifest.entries_.assign(entries.begin(), entries.end());
    for (const ReplacementManifestEntry& entry : manifest.entries_) {
        if ((entry.kind != ReplacementAssetKind::Texture &&
             entry.kind != ReplacementAssetKind::Font) ||
            !valid_path(entry.logical_path) || !valid_sha256(entry.sha256) ||
            entry.width == 0u || entry.height == 0u ||
            entry.width > 16384u || entry.height > 16384u) {
            return std::nullopt;
        }
    }
    std::sort(manifest.entries_.begin(), manifest.entries_.end(), entry_less);
    if (std::adjacent_find(manifest.entries_.begin(), manifest.entries_.end(),
                           [](const auto& left, const auto& right) {
                               return left.kind == right.kind && left.key == right.key;
                           }) != manifest.entries_.end()) {
        return std::nullopt;
    }
    manifest.generation_ = manifest_generation;
    return manifest;
}

std::optional<ReplacementIdentity> ReplacementManifest::resolve(
    ReplacementAssetKind kind, ReplacementKey key,
    std::uint64_t source_generation) const {
    if (source_generation == 0u) {
        return std::nullopt;
    }
    const ReplacementManifestEntry needle{kind, key, {}, {}, 0u, 0u};
    const auto found = std::lower_bound(entries_.begin(), entries_.end(), needle, entry_less);
    if (found == entries_.end() || found->kind != kind || found->key != key) {
        return std::nullopt;
    }
    return ReplacementIdentity{kind, key, generation_, source_generation,
                               static_cast<std::uint32_t>(found - entries_.begin()),
                               found->sha256};
}

bool ReplacementManifest::is_current(const ReplacementIdentity& identity,
                                     std::uint64_t source_generation) const noexcept {
    if (identity.manifest_generation != generation_ || source_generation == 0u ||
        identity.source_generation != source_generation ||
        identity.entry_index >= entries_.size()) {
        return false;
    }
    const ReplacementManifestEntry& entry = entries_[identity.entry_index];
    return entry.kind == identity.kind && entry.key == identity.key &&
           entry.sha256 == identity.sha256;
}

std::uint64_t ReplacementManifest::generation() const noexcept { return generation_; }
std::size_t ReplacementManifest::size() const noexcept { return entries_.size(); }

} // namespace psx::render
