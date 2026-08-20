#pragma once

#include "clean_aot/compiler/types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace clean_aot::compiler {

class ExeFormatError final : public std::runtime_error {
public:
    explicit ExeFormatError(const std::string& message)
        : std::runtime_error(message) {}
};

class PsxExeImage final {
public:
    static constexpr std::size_t kHeaderSize = 0x800U;

    static PsxExeImage parse(std::span<const std::uint8_t> file_bytes);
    static PsxExeImage read_file(const std::filesystem::path& path);

    [[nodiscard]] GuestAddress entry_pc() const noexcept { return entry_pc_; }
    [[nodiscard]] GuestAddress load_address() const noexcept { return load_address_; }
    [[nodiscard]] std::uint32_t text_size() const noexcept {
        return static_cast<std::uint32_t>(text_.size());
    }
    [[nodiscard]] std::uint64_t text_end_exclusive() const noexcept {
        return text_end_exclusive_;
    }
    [[nodiscard]] std::span<const std::uint8_t> text() const noexcept {
        return text_;
    }

    [[nodiscard]] bool contains(GuestAddress address, std::size_t width = 1U) const noexcept;
    [[nodiscard]] std::uint32_t read_u32(GuestAddress address) const;

private:
    PsxExeImage(GuestAddress entry_pc,
                GuestAddress load_address,
                std::uint64_t text_end_exclusive,
                std::vector<std::uint8_t> text)
        : entry_pc_(entry_pc),
          load_address_(load_address),
          text_end_exclusive_(text_end_exclusive),
          text_(std::move(text)) {}

    GuestAddress entry_pc_;
    GuestAddress load_address_;
    std::uint64_t text_end_exclusive_;
    std::vector<std::uint8_t> text_;
};

} // namespace clean_aot::compiler
