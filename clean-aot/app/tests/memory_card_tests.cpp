#include "clean_aot/app/memory_card.hpp"

#include <array>
#include <filesystem>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "clean-aot-card-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    clean_aot::app::MemoryCard card;
    const auto path = root / "card1.mcd";
    if (!card.load_or_create(path) || card.bytes().size() != clean_aot::app::kMemoryCardBytes)
        return 1;
    const std::array<std::uint8_t, 4> marker{0x4dU, 0x43U, 0x44U, 0x01U};
    if (!card.write(127U * 1024U, marker) || !card.dirty() || !card.flush()) return 2;
    clean_aot::app::MemoryCard reloaded;
    if (!reloaded.load_or_create(path) ||
        !std::equal(marker.begin(), marker.end(), reloaded.bytes().begin() + 127U * 1024U)) return 3;
    const std::array<std::uint8_t, 2> overflow{};
    if (reloaded.write(clean_aot::app::kMemoryCardBytes - 1U, overflow)) return 4;
    std::filesystem::remove_all(root, ignored);
    std::cout << "clean_aot_memory_card_tests: all checks passed\n";
    return 0;
}
