#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace clean_aot::app {

inline constexpr std::size_t kMemoryCardBytes = 128U * 1024U;

class MemoryCard final {
public:
    [[nodiscard]] bool load_or_create(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool write(std::size_t offset, std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool flush() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return data_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }

private:
    std::filesystem::path path_{};
    std::array<std::uint8_t, kMemoryCardBytes> data_{};
    bool loaded_ = false;
    bool dirty_ = false;
};

} // namespace clean_aot::app
