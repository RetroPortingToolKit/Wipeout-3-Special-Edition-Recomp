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

} // namespace

int main() {
    test_native_handoff_and_worst_case_burst();
    test_rejection_and_cdda_handoff();
    std::cout << "clean_aot_cd_audio_tests: ok\n";
    return EXIT_SUCCESS;
}
