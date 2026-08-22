#include "clean_aot/spu/cd_audio.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::array<std::uint8_t, clean_aot::cdrom::XaAdpcmBytes> xa_data(
    bool eight_bit) {
    std::array<std::uint8_t, clean_aot::cdrom::XaAdpcmBytes> bytes{};
    for (std::size_t group = 0U; group < 18U; ++group) {
        std::uint8_t* chunk = bytes.data() + group * 128U;
        const std::size_t blocks = eight_bit ? 4U : 8U;
        for (std::size_t block = 0U; block < blocks; ++block) {
            chunk[4U + block] = eight_bit ? 0x08U : 0x0cU;
        }
        for (std::size_t index = 0U; index < 4U; ++index) {
            chunk[index] = chunk[4U + index];
            chunk[12U + index] = chunk[8U + index];
        }
        if (group == 0U) {
            chunk[16U] = eight_bit ? 0x01U : 0xf1U;
            if (eight_bit) {
                chunk[17U] = 0xffU;
            }
        }
    }
    return bytes;
}

void test_native_handoff_and_worst_case_burst() {
    static clean_aot::spu::Device device;
    device.reset();
    static clean_aot::spu::CdAudioSink sink(device);
    sink.reset();
    clean_aot::cdrom::XaMetadata metadata{};
    metadata.file = 1U;
    metadata.channel = 2U;
    metadata.audio = true;
    metadata.form_two = true;
    metadata.coding = 0x04U; // mono, 18.9 kHz, four bit: legal worst case.
    const auto bytes = xa_data(false);
    sink.receive_xa_sector(10U, metadata, bytes, clean_aot::cdrom::AudioMatrix{}, false);
    const auto status = sink.status();
    require(status.xa_sectors == 1U && status.rejected_xa_sectors == 0U &&
                status.dropped_frames == 0U,
            "worst-case XA sector is accepted without FIFO truncation");
    require(device.audio_source() == clean_aot::spu::AudioSource::XaResampled &&
                device.input_status(clean_aot::spu::AudioSource::XaResampled).queued > 2048U,
            "resampled XA burst reaches the bounded native SPU queue");

    /* Eight 75-Hz sectors elapse before this filtered channel recurs. */
    std::array<clean_aot::spu::StereoSample, 4704U> rendered{};
    require(device.render(rendered).ok(), "native 44.1-kHz consumption succeeds");
    const std::size_t before =
        device.input_status(clean_aot::spu::AudioSource::XaResampled).queued;
    sink.receive_xa_sector(18U, metadata, bytes, clean_aot::cdrom::AudioMatrix{}, false);
    if (sink.status().xa_sectors != 2U) {
        std::cerr << "XA status accepted=" << sink.status().xa_sectors
                  << " rejected=" << sink.status().rejected_xa_sectors
                  << " dropped=" << sink.status().dropped_frames << '\n';
    }
    require(sink.status().xa_sectors == 2U,
            "forward interleaved-channel LBA gap decodes a second sector");
    require(sink.status().dropped_frames == 0U,
            "native consumption prevents interleaved XA FIFO overflow");
    require(device.input_status(clean_aot::spu::AudioSource::XaResampled).queued > before,
            "forward interleaved-channel LBA gap preserves stream continuity");
}

void test_rejection_and_cdda_handoff() {
    static clean_aot::spu::Device device;
    device.reset();
    static clean_aot::spu::CdAudioSink sink(device);
    sink.reset();
    clean_aot::cdrom::XaMetadata bad{};
    bad.audio = true;
    bad.form_two = true;
    bad.coding = 0x02U;
    const auto bytes = xa_data(false);
    sink.receive_xa_sector(0U, bad, bytes, clean_aot::cdrom::AudioMatrix{}, false);
    require(sink.status().rejected_xa_sectors == 1U,
            "reserved XA coding bits fail closed before SPU handoff");

    const std::array<clean_aot::cdrom::StereoSample, 2U> cdda{{{123, -456}, {-789, 321}}};
    sink.receive_cdda_samples(0U, cdda);
    require(sink.status().cdda_sectors == 1U && sink.status().dropped_frames == 0U &&
                device.audio_source() == clean_aot::spu::AudioSource::Cdda &&
                device.input_status(clean_aot::spu::AudioSource::Cdda).queued == 2U,
            "CDDA enters the same bounded native-time SPU handoff");
}

void test_optical_audio_epoch_and_exact_100hz_cadence() {
    static clean_aot::spu::Device device;
    device.reset();
    static clean_aot::spu::CdAudioSink sink(device);
    sink.reset();
    std::array<clean_aot::cdrom::StereoSample,
               clean_aot::cdrom::CddaSamplesPerSector> sector{};
    sector.fill({100, -100});

    sink.receive_cdda_samples(10U, sector);
    std::array<clean_aot::spu::StereoSample,
               clean_aot::spu::SamplesPer100HzUpdate> frame{};
    require(device.render(frame).ok(), "first 100 Hz audio frame renders");
    require(device.input_status(clean_aot::spu::AudioSource::Cdda).queued == 147U,
            "one sector retains the exact 588-minus-441 sample phase");

    sink.receive_cdda_samples(20U, sector);
    require(sink.status().stream_discontinuities == 1U &&
                device.input_status(clean_aot::spu::AudioSource::Cdda).queued ==
                    clean_aot::cdrom::CddaSamplesPerSector,
            "seek starts a new queue epoch without stale CDDA samples");

    /* Three 75 Hz sectors exactly feed four native 100 Hz updates. */
    require(device.render(frame).ok(), "seek epoch frame one renders");
    sink.receive_cdda_samples(21U, sector);
    require(device.render(frame).ok(), "seek epoch frame two renders");
    sink.receive_cdda_samples(22U, sector);
    require(device.render(frame).ok(), "seek epoch frame three renders");
    require(device.render(frame).ok(), "seek epoch frame four renders");
    require(device.input_status(clean_aot::spu::AudioSource::Cdda).queued == 0U &&
                sink.status().dropped_frames == 0U,
            "three sectors and four 100 Hz frames close with zero phase or loss");
}

void test_xa_end_flags_close_predictor_and_fifo_epoch() {
    static clean_aot::spu::Device device;
    device.reset();
    static clean_aot::spu::CdAudioSink sink(device);
    sink.reset();
    clean_aot::cdrom::XaMetadata terminal{};
    terminal.file = 3U;
    terminal.channel = 4U;
    terminal.audio = true;
    terminal.form_two = true;
    terminal.end_of_file = true;
    const auto bytes = xa_data(false);
    sink.receive_xa_sector(100U, terminal, bytes,
                           clean_aot::cdrom::AudioMatrix{}, false);
    const std::size_t complete_sector =
        device.input_status(clean_aot::spu::AudioSource::XaResampled).queued;
    require(complete_sector != 0U && sink.status().stream_discontinuities == 1U,
            "XA EOF closes the predictor epoch after queuing its final sector");

    std::array<clean_aot::spu::StereoSample,
               clean_aot::spu::SamplesPer100HzUpdate> frame{};
    require(device.render(frame).ok(), "terminal XA 100 Hz frame renders");
    require(device.input_status(clean_aot::spu::AudioSource::XaResampled).queued ==
                complete_sector - clean_aot::spu::SamplesPer100HzUpdate,
            "terminal XA sector advances by exactly 441 native samples");

    terminal.end_of_file = false;
    sink.receive_xa_sector(101U, terminal, bytes,
                           clean_aot::cdrom::AudioMatrix{}, false);
    require(device.input_status(clean_aot::spu::AudioSource::XaResampled).queued ==
                complete_sector,
            "post-EOF XA restart replaces queued tail instead of appending to it");
    for (unsigned update = 0U; update < 4U; ++update) {
        require(device.render(frame).ok(), "restarted XA 100 Hz frame renders");
    }
    require(device.input_status(clean_aot::spu::AudioSource::XaResampled).queued ==
                complete_sector - 4U * clean_aot::spu::SamplesPer100HzUpdate,
            "restarted XA epoch retains exact four-frame 100 Hz accounting");
}

} // namespace

int main() {
    test_native_handoff_and_worst_case_burst();
    test_rejection_and_cdda_handoff();
    test_optical_audio_epoch_and_exact_100hz_cadence();
    test_xa_end_flags_close_predictor_and_fifo_epoch();
    std::cout << "clean_aot_cd_audio_tests: ok\n";
    return EXIT_SUCCESS;
}
