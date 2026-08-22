#pragma once

#include "cdrom.hpp"
#include "clean_aot/media/media.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace clean_aot::app {

class DiscSource final : public cdrom::RawSectorSource {
public:
    [[nodiscard]] static std::unique_ptr<DiscSource> open(
        const std::filesystem::path& cue_path,
        std::string& diagnostic) noexcept;

    [[nodiscard]] bool read_raw_sector(
        cdrom::Lba lba, cdrom::RawSector& destination) noexcept override;
    [[nodiscard]] const cdrom::DiscLayout& layout() const noexcept {
        return layout_;
    }
    [[nodiscard]] std::uint64_t sectors_read() const noexcept {
        return sectors_read_;
    }

private:
    DiscSource(media::CueImage image, cdrom::DiscLayout layout)
        : image_(std::move(image)), layout_(layout) {}

    media::CueImage image_;
    cdrom::DiscLayout layout_{};
    std::uint64_t sectors_read_ = 0U;
};

} // namespace clean_aot::app
