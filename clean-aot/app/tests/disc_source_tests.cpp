#include "clean_aot/app/disc_source.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void write_image(const std::filesystem::path& root, std::string banner) {
    std::vector<std::uint8_t> bytes(5U * 2352U, 0U);
    const std::size_t offset = 4U * 2352U + 24U;
    std::copy(banner.begin(), banner.end(), bytes.begin() + offset);
    std::ofstream bin(root / "track.bin", std::ios::binary);
    bin.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    std::ofstream cue(root / "disc.cue", std::ios::binary);
    cue << "FILE \"track.bin\" BINARY\n"
           "  TRACK 01 MODE2/2352\n"
           "    INDEX 01 00:00:00\n";
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      "clean-aot-disc-source-license-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);

    write_image(root,
                "Licensed  by          Sony Computer Entertainment Euro pe");
    std::string diagnostic;
    auto disc = clean_aot::app::DiscSource::open(root / "disc.cue", diagnostic);
    bool ok = expect(disc != nullptr, "authenticated European disc mounts");
    if (disc) {
        ok &= expect(disc->layout().disc_type == 0x20U,
                     "GetID reports a licensed data disc");
        ok &= expect(disc->layout().license_region ==
                         std::array<std::uint8_t, 4U>{'S', 'C', 'E', 'E'},
                     "license banner binds SCEE identity");
    }

    write_image(root, "unlicensed data");
    diagnostic.clear();
    disc = clean_aot::app::DiscSource::open(root / "disc.cue", diagnostic);
    ok &= expect(disc != nullptr, "neutral authenticated media still mounts");
    if (disc) {
        ok &= expect(disc->layout().license_region ==
                         std::array<std::uint8_t, 4U>{},
                     "unknown media cannot acquire a GetID identity");
    }
    std::filesystem::remove_all(root, error);
    return ok ? 0 : 1;
}
