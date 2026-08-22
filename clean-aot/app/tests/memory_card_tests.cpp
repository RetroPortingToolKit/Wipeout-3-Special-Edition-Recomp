#include "clean_aot/app/memory_card.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool valid_frame_checksum(std::span<const std::uint8_t> frame) {
    std::uint8_t checksum = 0U;
    for (const auto byte : frame) checksum ^= byte;
    return checksum == 0U;
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "clean-aot-card-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    clean_aot::app::MemoryCard card;
    const auto path = root / "card1.mcd";
    if (!card.load_or_create(path) || card.bytes().size() != clean_aot::app::kMemoryCardBytes)
        return 1;
    if (card.bytes()[0] != 0x4dU || card.bytes()[1] != 0x43U ||
        !valid_frame_checksum(card.bytes().subspan(0U, 128U))) return 2;
    for (std::size_t frame = 1U; frame <= 35U; ++frame) {
        if (!valid_frame_checksum(card.bytes().subspan(frame * 128U, 128U))) return 3;
    }
    if (!std::equal(card.bytes().begin(), card.bytes().begin() + 128,
                    card.bytes().begin() + (63 * 128))) return 4;
    const std::array<std::uint8_t, 4> marker{0x4dU, 0x43U, 0x44U, 0x01U};
    if (!card.write(127U * 1024U, marker) || !card.dirty()) return 5;

    const auto malformed_path = root / "malformed.mcd";
    const std::array<std::uint8_t, 3> malformed{0xdeU, 0xadU, 0xbeU};
    {
        std::ofstream output(malformed_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(malformed.data()),
                     static_cast<std::streamsize>(malformed.size()));
    }
    if (card.load_or_create(malformed_path)) return 6;
    if (!card.dirty() || !card.flush()) return 7;
    if (std::filesystem::file_size(malformed_path) != malformed.size()) return 8;
    std::array<std::uint8_t, 3> malformed_after{};
    {
        std::ifstream input(malformed_path, std::ios::binary);
        input.read(reinterpret_cast<char*>(malformed_after.data()),
                   static_cast<std::streamsize>(malformed_after.size()));
        if (!input) return 9;
    }
    if (malformed_after != malformed) return 10;

    clean_aot::app::MemoryCard reloaded;
    if (!reloaded.load_or_create(path) ||
        !std::equal(marker.begin(), marker.end(), reloaded.bytes().begin() + 127U * 1024U)) return 11;
    const std::array<std::uint8_t, 2> overflow{};
    if (reloaded.write(clean_aot::app::kMemoryCardBytes - 1U, overflow)) return 12;
    std::filesystem::remove_all(root, ignored);
    std::cout << "clean_aot_memory_card_tests: all checks passed\n";
    return 0;
}
