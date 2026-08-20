#include "clean_aot/compiler/psx_exe.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

namespace clean_aot::compiler {
namespace {

constexpr std::size_t kInitialPcOffset = 0x10U;
constexpr std::size_t kLoadAddressOffset = 0x18U;
constexpr std::size_t kTextSizeOffset = 0x1cU;
constexpr std::size_t kMagicSize = 8U;

[[nodiscard]] std::uint32_t read_le32(std::span<const std::uint8_t> bytes,
                                      std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
        throw ExeFormatError("PS-X EXE header field is outside the file");
    }

    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

} // namespace

PsxExeImage PsxExeImage::parse(std::span<const std::uint8_t> file_bytes) {
    if (file_bytes.size() < kHeaderSize) {
        throw ExeFormatError("PS-X EXE is shorter than its fixed header");
    }

    constexpr std::array<char, kMagicSize> expected_magic = {
        'P', 'S', '-', 'X', ' ', 'E', 'X', 'E'};
    if (std::memcmp(file_bytes.data(), expected_magic.data(), expected_magic.size()) != 0) {
        throw ExeFormatError("PS-X EXE magic is not present");
    }

    const GuestAddress entry_pc = read_le32(file_bytes, kInitialPcOffset);
    const GuestAddress load_address = read_le32(file_bytes, kLoadAddressOffset);
    const std::uint32_t text_size = read_le32(file_bytes, kTextSizeOffset);

    if (text_size == 0U || (text_size % sizeof(std::uint32_t)) != 0U) {
        throw ExeFormatError("PS-X EXE text size must be a non-zero multiple of four");
    }
    if ((load_address % sizeof(std::uint32_t)) != 0U ||
        (entry_pc % sizeof(std::uint32_t)) != 0U) {
        throw ExeFormatError("PS-X EXE load address and entry PC must be aligned");
    }

    const std::uint64_t text_end_exclusive =
        static_cast<std::uint64_t>(load_address) + static_cast<std::uint64_t>(text_size);
    constexpr std::uint64_t kGuestAddressSpaceSize =
        static_cast<std::uint64_t>(std::numeric_limits<GuestAddress>::max()) + 1ULL;
    if (text_end_exclusive > kGuestAddressSpaceSize) {
        throw ExeFormatError("PS-X EXE text range wraps the guest address space");
    }

    const std::size_t payload_size = file_bytes.size() - kHeaderSize;
    if (static_cast<std::uint64_t>(text_size) > static_cast<std::uint64_t>(payload_size)) {
        throw ExeFormatError("PS-X EXE text extends beyond the available file bytes");
    }

    const std::uint64_t entry_end = static_cast<std::uint64_t>(entry_pc) + sizeof(std::uint32_t);
    if (static_cast<std::uint64_t>(entry_pc) < static_cast<std::uint64_t>(load_address) ||
        entry_end > text_end_exclusive) {
        throw ExeFormatError("PS-X EXE entry PC is outside the text image");
    }

    const auto text_begin = file_bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize);
    const auto text_end = text_begin + static_cast<std::ptrdiff_t>(text_size);
    std::vector<std::uint8_t> text(text_begin, text_end);
    return PsxExeImage(entry_pc, load_address, text_end_exclusive, std::move(text));
}

PsxExeImage PsxExeImage::read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw ExeFormatError("unable to open PS-X EXE file: " + path.string());
    }

    const std::streampos end = input.tellg();
    if (end < 0) {
        throw ExeFormatError("unable to determine PS-X EXE file size");
    }
    const auto size = static_cast<std::uintmax_t>(end);
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw ExeFormatError("PS-X EXE file is too large for this host");
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        throw ExeFormatError("PS-X EXE file is too large for one host stream read");
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw ExeFormatError("unable to read the complete PS-X EXE file");
        }
    }
    return parse(bytes);
}

bool PsxExeImage::contains(GuestAddress address, std::size_t width) const noexcept {
    const std::uint64_t begin = static_cast<std::uint64_t>(address);
    const std::uint64_t requested_width = static_cast<std::uint64_t>(width);
    if (requested_width > std::numeric_limits<std::uint64_t>::max() - begin) {
        return false;
    }
    const std::uint64_t end = begin + requested_width;
    return begin >= static_cast<std::uint64_t>(load_address_) &&
           end <= text_end_exclusive_ &&
           end >= begin;
}

std::uint32_t PsxExeImage::read_u32(GuestAddress address) const {
    if (!contains(address, sizeof(std::uint32_t))) {
        throw ExeFormatError("guest word read is outside the PS-X EXE text image");
    }

    const std::size_t offset = static_cast<std::size_t>(
        static_cast<std::uint64_t>(address) - static_cast<std::uint64_t>(load_address_));
    return static_cast<std::uint32_t>(text_[offset]) |
           (static_cast<std::uint32_t>(text_[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(text_[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(text_[offset + 3U]) << 24U);
}

} // namespace clean_aot::compiler
