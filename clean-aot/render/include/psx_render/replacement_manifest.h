#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace psx::render {

enum class ReplacementAssetKind : std::uint8_t {
    Texture,
    Font,
};

struct ReplacementKey {
    std::uint32_t primary{};
    std::uint32_t secondary{};

    friend constexpr bool operator==(const ReplacementKey&, const ReplacementKey&) = default;
    friend constexpr auto operator<=>(const ReplacementKey&, const ReplacementKey&) = default;
};

struct ReplacementManifestEntry {
    ReplacementAssetKind kind = ReplacementAssetKind::Texture;
    ReplacementKey key{};
    std::string logical_path;
    std::string sha256;
    std::uint32_t width{};
    std::uint32_t height{};
};

struct ReplacementIdentity {
    ReplacementAssetKind kind = ReplacementAssetKind::Texture;
    ReplacementKey key{};
    std::uint64_t manifest_generation{};
    std::uint64_t source_generation{};
    std::uint32_t entry_index{};
    std::string sha256;
};

class ReplacementManifest final {
public:
    [[nodiscard]] static std::optional<ReplacementManifest> create(
        std::span<const ReplacementManifestEntry> entries,
        std::uint64_t manifest_generation);

    [[nodiscard]] std::optional<ReplacementIdentity> resolve(
        ReplacementAssetKind kind, ReplacementKey key,
        std::uint64_t source_generation) const;
    [[nodiscard]] bool is_current(const ReplacementIdentity& identity,
                                  std::uint64_t source_generation) const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<ReplacementManifestEntry> entries_;
    std::uint64_t generation_{};
};

[[nodiscard]] std::optional<ReplacementKey> parse_replacement_key(
    std::string_view value) noexcept;

} // namespace psx::render
