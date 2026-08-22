#ifndef CLEAN_AOT_SPU_CD_AUDIO_HPP
#define CLEAN_AOT_SPU_CD_AUDIO_HPP

#include "clean_aot/spu/spu.hpp"
#include "cdrom.hpp"
#include "clean_aot/media/media.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace clean_aot::spu {

struct CdAudioStatus final {
    std::uint64_t cdda_sectors = 0U;
    std::uint64_t xa_sectors = 0U;
    std::uint64_t rejected_xa_sectors = 0U;
    std::uint64_t dropped_frames = 0U;
};

/*
 * Synchronous native-time handoff. Cdrom invokes this sink only while handling
 * its native-cycle sector event; the runtime then consumes the queued 44.1-kHz
 * frames at SPU sample boundaries. No host clock participates in this path.
 */
class CdAudioSink final : public cdrom::CddaSink, public cdrom::XaSectorSink {
public:
    explicit CdAudioSink(Device& device) noexcept;

    void reset() noexcept;
    [[nodiscard]] CdAudioStatus status() const noexcept { return status_; }

    void receive_cdda_samples(
        cdrom::Lba lba,
        std::span<const cdrom::StereoSample> samples) noexcept override;
    void receive_xa_sector(cdrom::Lba lba,
                           const cdrom::XaMetadata& metadata,
                           std::span<const std::uint8_t> adpcm_bytes,
                           cdrom::AudioMatrix matrix,
                           bool muted) noexcept override;

private:
    Device* device_ = nullptr;
    media::XaAdpcmDecoder decoder_{};
    std::optional<media::XaRateConverter> converter_{};
    std::optional<media::XaFormat> format_{};
    std::uint8_t file_ = 0U;
    std::uint8_t channel_ = 0U;
    cdrom::Lba next_lba_ = 0U;
    bool stream_active_ = false;
    CdAudioStatus status_{};
    std::array<media::StereoSample, 4032U> decoded_{};
    std::array<media::StereoSample, 9408U> converted_{};
    std::array<StereoSample, 9408U> handoff_{};

    void reject_xa() noexcept;
};

} // namespace clean_aot::spu

#endif
