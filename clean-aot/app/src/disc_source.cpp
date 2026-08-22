#include "clean_aot/app/disc_source.hpp"

#include <algorithm>
#include <limits>

namespace clean_aot::app {

std::unique_ptr<DiscSource> DiscSource::open(
    const std::filesystem::path& cue_path,
    std::string& diagnostic) noexcept {
    try {
        auto opened = media::CueImage::open(cue_path);
        if (!opened) {
            diagnostic = "cue-open-failed:" + opened.error().detail;
            return nullptr;
        }
        media::CueImage image = std::move(opened).value();
        const media::DiscInfo& disc = image.disc();
        if (disc.tracks.empty() || disc.tracks.size() > 99U ||
            disc.lead_out_lba > cdrom::MaximumAddressableLba) {
            diagnostic = "cue-layout-out-of-range";
            return nullptr;
        }
        cdrom::DiscLayout layout{};
        layout.track_count = static_cast<std::uint8_t>(disc.tracks.size());
        layout.lead_out_lba = static_cast<cdrom::Lba>(disc.lead_out_lba);
        for (std::size_t index = 0U; index < disc.tracks.size(); ++index) {
            const media::TrackInfo& source = disc.tracks[index];
            if (source.index01_lba > cdrom::MaximumAddressableLba) {
                diagnostic = "cue-track-out-of-range";
                return nullptr;
            }
            layout.tracks[index] = cdrom::Track{
                source.number,
                static_cast<cdrom::Lba>(source.index01_lba),
                source.type == media::TrackType::Audio
                    ? cdrom::TrackKind::Audio
                    : cdrom::TrackKind::Data};
        }
        if (!layout.valid()) {
            diagnostic = "cdrom-layout-rejected";
            return nullptr;
        }
        diagnostic = "mounted-authenticated-bin-cue";
        return std::unique_ptr<DiscSource>(
            new DiscSource(std::move(image), layout));
    } catch (...) {
        diagnostic = "disc-source-exception";
        return nullptr;
    }
}

bool DiscSource::read_raw_sector(
    cdrom::Lba lba, cdrom::RawSector& destination) noexcept {
    try {
        auto sector = image_.read_raw_sector(lba);
        if (!sector) {
            return false;
        }
        std::copy(sector.value().bytes.begin(), sector.value().bytes.end(),
                  destination.bytes.begin());
        ++sectors_read_;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace clean_aot::app
