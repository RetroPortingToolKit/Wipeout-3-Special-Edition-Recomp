#include "clean_aot/spu/cd_audio.hpp"

#include <algorithm>
#include <limits>

namespace clean_aot::spu {

CdAudioSink::CdAudioSink(Device& device) noexcept : device_(&device) {}

void CdAudioSink::reset() noexcept {
    decoder_.reset();
    converter_.reset();
    format_.reset();
    stream_active_ = false;
    next_lba_ = 0U;
    cdda_stream_active_ = false;
    next_cdda_lba_ = 0U;
    device_->clear_audio_input(AudioSource::Cdda);
    device_->clear_audio_input(AudioSource::XaResampled);
    status_ = {};
}

void CdAudioSink::receive_cdda_samples(
    cdrom::Lba lba,
    std::span<const cdrom::StereoSample> samples) noexcept {
    if (!cdda_stream_active_ || lba != next_cdda_lba_) {
        device_->clear_audio_input(AudioSource::Cdda);
        if (cdda_stream_active_) {
            ++status_.stream_discontinuities;
        }
    }
    ++status_.cdda_sectors;
    const std::size_t count = std::min(samples.size(), handoff_.size());
    for (std::size_t index = 0U; index < count; ++index) {
        handoff_[index] = StereoSample{samples[index].left, samples[index].right};
    }
    device_->set_audio_source(AudioSource::Cdda);
    const PushResult pushed = device_->push_cdda(
        std::span<const StereoSample>(handoff_.data(), count));
    status_.dropped_frames += pushed.dropped + (samples.size() - count);
    cdda_stream_active_ = true;
    next_cdda_lba_ = lba + 1U;
}

void CdAudioSink::reject_xa() noexcept {
    ++status_.rejected_xa_sectors;
    decoder_.reset();
    converter_.reset();
    format_.reset();
    stream_active_ = false;
    device_->clear_audio_input(AudioSource::XaResampled);
}

void CdAudioSink::receive_xa_sector(
    cdrom::Lba lba,
    const cdrom::XaMetadata& metadata,
    std::span<const std::uint8_t> adpcm_bytes,
    cdrom::AudioMatrix matrix,
    bool muted) noexcept {
    if (adpcm_bytes.size() != media::XaAdpcmBytes || !metadata.audio ||
        !metadata.form_two || (metadata.coding & 0xaaU) != 0U) {
        reject_xa();
        return;
    }
    media::XaSector sector{};
    sector.lba = lba;
    sector.metadata.file_number = metadata.file;
    sector.metadata.channel_number = metadata.channel;
    sector.metadata.audio = true;
    sector.metadata.form_two = true;
    sector.format.stereo = (metadata.coding & 0x01U) != 0U;
    sector.format.sample_rate_hz = (metadata.coding & 0x04U) != 0U
                                       ? media::XaRateHalfHz
                                       : media::XaRateFullHz;
    sector.format.bits_per_sample = (metadata.coding & 0x10U) != 0U
                                        ? media::XaBitsPerSample::Eight
                                        : media::XaBitsPerSample::Four;
    sector.format.emphasis = (metadata.coding & 0x40U) != 0U;
    std::copy(adpcm_bytes.begin(), adpcm_bytes.end(), sector.adpcm.begin());

    /* Filtered XA channels are normally interleaved, so forward LBA gaps are
       continuous. Repeated/backward sectors are stream discontinuities. */
    const bool discontinuity = !stream_active_ || lba < next_lba_ ||
                               metadata.file != file_ || metadata.channel != channel_ ||
                               !format_.has_value() || *format_ != sector.format;
    if (discontinuity) {
        device_->clear_audio_input(AudioSource::XaResampled);
        if (stream_active_) {
            ++status_.stream_discontinuities;
        }
        decoder_.reset();
        auto converter = media::XaRateConverter::create(sector.format.sample_rate_hz);
        if (!converter) {
            reject_xa();
            return;
        }
        converter_ = std::move(converter).value();
        format_ = sector.format;
        file_ = metadata.file;
        channel_ = metadata.channel;
    }

    auto decoded = decoder_.decode(sector, decoded_);
    if (!decoded || !converter_.has_value()) {
        reject_xa();
        return;
    }
    auto converted = converter_->push(
        std::span<const media::StereoSample>(decoded_.data(), decoded.value()), converted_);
    if (!converted) {
        reject_xa();
        return;
    }
    const std::size_t count = converted.value();
    const auto mix = [](std::int16_t left, std::int16_t right,
                        std::uint8_t left_gain, std::uint8_t right_gain) noexcept {
        const std::int64_t sum = static_cast<std::int64_t>(left) * left_gain +
                                 static_cast<std::int64_t>(right) * right_gain;
        const std::int64_t scaled = sum / 128;
        return static_cast<std::int16_t>(std::clamp<std::int64_t>(
            scaled, std::numeric_limits<std::int16_t>::min(),
            std::numeric_limits<std::int16_t>::max()));
    };
    for (std::size_t index = 0U; index < count; ++index) {
        handoff_[index] = muted
            ? StereoSample{}
            : StereoSample{
                  mix(converted_[index].left, converted_[index].right,
                      matrix.left_to_left, matrix.right_to_left),
                  mix(converted_[index].left, converted_[index].right,
                      matrix.left_to_right, matrix.right_to_right)};
    }
    device_->set_audio_source(AudioSource::XaResampled);
    const PushResult pushed = device_->push_xa_resampled(
        std::span<const StereoSample>(handoff_.data(), count));
    status_.dropped_frames += pushed.dropped;
    ++status_.xa_sectors;
    next_lba_ = lba + 1U;
    stream_active_ = true;
}

} // namespace clean_aot::spu
