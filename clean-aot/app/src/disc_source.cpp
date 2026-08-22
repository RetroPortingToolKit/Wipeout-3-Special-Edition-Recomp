#include "clean_aot/app/disc_source.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace clean_aot::app {

namespace {

constexpr cdrom::Lba LicenseSectorLba = 4U;
constexpr std::uint8_t LicensedDataDiscType = 0x20U;

bool bind_authenticated_license(media::CueImage& image,
                                cdrom::DiscLayout& layout,
                                std::string& diagnostic) {
    auto sector = image.read_raw_sector(LicenseSectorLba);
    if (!sector) {
        diagnostic = "license-sector-read-failed:" + sector.error().detail;
        return false;
    }

    // The PSX license screen stores its banner as display text with variable
    // spacing.  Compare only ASCII letters, but require one exact known Sony
    // region phrase so arbitrary disc data cannot become a GetID identity.
    std::string letters;
    letters.reserve(sector.value().bytes.size());
    for (const std::uint8_t byte : sector.value().bytes) {
        if ((byte >= static_cast<std::uint8_t>('A') &&
             byte <= static_cast<std::uint8_t>('Z')) ||
            (byte >= static_cast<std::uint8_t>('a') &&
             byte <= static_cast<std::uint8_t>('z'))) {
            letters.push_back(static_cast<char>(byte));
        }
    }

    struct License final {
        std::string_view banner;
        std::array<std::uint8_t, 4U> get_id;
    };
    constexpr std::array licenses{
        License{"LicensedbySonyComputerEntertainmentEurope", {'S', 'C', 'E', 'E'}},
        License{"LicensedbySonyComputerEntertainmentAmerica", {'S', 'C', 'E', 'A'}},
        License{"LicensedbySonyComputerEntertainmentInc", {'S', 'C', 'E', 'I'}},
    };
    const License* matched = nullptr;
    for (const License& license : licenses) {
        if (letters.find(license.banner) != std::string::npos) {
            if (matched != nullptr) {
                diagnostic = "license-sector-ambiguous";
                return false;
            }
            matched = &license;
        }
    }
    if (matched == nullptr) {
        // Audio and development media may legitimately have no console
        // license banner.  Keep their identity neutral so GetID denies them;
        // mounting the authenticated bytes itself remains valid.
        return true;
    }
    layout.disc_type = LicensedDataDiscType;
    layout.license_region = matched->get_id;
    return true;
}

} // namespace

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
        if (layout.tracks[0U].kind == cdrom::TrackKind::Data &&
            layout.lead_out_lba > LicenseSectorLba &&
            !bind_authenticated_license(image, layout, diagnostic)) {
            return nullptr;
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
