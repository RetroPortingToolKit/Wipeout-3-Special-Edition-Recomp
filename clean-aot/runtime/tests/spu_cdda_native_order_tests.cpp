#include "clean_aot/runtime/runtime.hpp"

#include "cdrom.hpp"
#include "clean_aot/spu/spu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

constexpr std::uint32_t kCdrom = 0x1f80'1800U;

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct AudioSource final : clean_aot::cdrom::RawSectorSource {
    std::uint32_t reads = 0U;

    bool read_raw_sector(clean_aot::cdrom::Lba lba,
                         clean_aot::cdrom::RawSector& destination) noexcept override {
        if (lba >= 3U) {
            return false;
        }
        ++reads;
        for (std::size_t frame = 0U;
             frame < clean_aot::cdrom::CddaSamplesPerSector; ++frame) {
            const std::int16_t left = static_cast<std::int16_t>(1000 + lba);
            const std::int16_t right = static_cast<std::int16_t>(-2000 -
                                                                 static_cast<int>(lba));
            const auto left_bits = static_cast<std::uint16_t>(left);
            const auto right_bits = static_cast<std::uint16_t>(right);
            const std::size_t offset = frame * 4U;
            destination.bytes[offset] = static_cast<std::uint8_t>(left_bits);
            destination.bytes[offset + 1U] = static_cast<std::uint8_t>(left_bits >> 8U);
            destination.bytes[offset + 2U] = static_cast<std::uint8_t>(right_bits);
            destination.bytes[offset + 3U] = static_cast<std::uint8_t>(right_bits >> 8U);
        }
        return true;
    }
};

clean_aot::cdrom::DiscLayout audio_layout() {
    clean_aot::cdrom::DiscLayout layout{};
    layout.tracks[0U] = clean_aot::cdrom::Track{
        1U, 0U, clean_aot::cdrom::TrackKind::Audio};
    layout.track_count = 1U;
    layout.lead_out_lba = 3U;
    return layout;
}

void write8(clean_aot::runtime::Runtime& runtime,
            std::uint32_t address,
            std::uint8_t value) {
    require(runtime.write8(
                {runtime.current_retirement_cycle()}, address, value).ok(),
            "CD MMIO write");
}

void advance(clean_aot::runtime::Runtime& runtime, std::uint64_t cycles) {
    require(runtime.advance_to_retirement_cycle(
                runtime.current_retirement_cycle() + cycles).ok(),
            "native-time advance");
}

std::unique_ptr<clean_aot::runtime::Runtime> make_runtime(AudioSource& source) {
    static const std::vector<std::uint8_t> bios(
        clean_aot::memory::kBiosBytes, 0U);
    static const clean_aot::cdrom::DiscLayout layout = audio_layout();
    clean_aot::runtime::Configuration configuration{};
    configuration.bios = bios;
    configuration.cpu_multiplier_numerator = 1U;
    configuration.cpu_multiplier_denominator = 1U;
    configuration.authenticated_region_capacity = 1U;
    configuration.disc_source = &source;
    configuration.disc_layout = &layout;
    auto runtime = std::make_unique<clean_aot::runtime::Runtime>(configuration);
    require(runtime->configured(), "configured audio runtime");
    return runtime;
}

void start_track(clean_aot::runtime::Runtime& runtime) {
    write8(runtime, kCdrom, 0U);
    write8(runtime, kCdrom + 2U, 0x01U);
    write8(runtime, kCdrom + 1U,
           static_cast<std::uint8_t>(clean_aot::cdrom::Command::Play));
    advance(runtime, clean_aot::cdrom::CommandLatencyCycles);
}

void drain_all_audio(clean_aot::runtime::Runtime& runtime) {
    std::array<clean_aot::runtime::StereoSample, 1024U> samples{};
    while (runtime.queued_audio_samples() != 0U) {
        require(runtime.drain_audio(samples) != 0U, "audio drain makes progress");
    }
}

void test_sector_publication_is_not_retroactive() {
    AudioSource source{};
    auto runtime = make_runtime(source);
    start_track(*runtime);
    drain_all_audio(*runtime);

    advance(*runtime, clean_aot::cdrom::SingleSpeedSectorCycles - 1U);
    require(source.reads == 0U &&
                runtime->spu().input_status(clean_aot::spu::AudioSource::Cdda).queued == 0U,
            "CDDA remains unavailable one native cycle before its deadline");

    advance(*runtime, 1U);
    const auto published =
        runtime->spu().input_status(clean_aot::spu::AudioSource::Cdda);
    require(source.reads == 1U &&
                published.queued == clean_aot::cdrom::CddaSamplesPerSector,
            "deadline publishes a full sector only after earlier SPU boundaries");

    static std::array<std::byte, clean_aot::spu::Device::SnapshotBytes> snapshot{};
    require(runtime->spu().save_snapshot(snapshot).ok(),
            "snapshot exact CDDA publication boundary");
    static clean_aot::spu::Device replay;
    replay.reset();
    require(replay.restore_snapshot(snapshot).ok() &&
                replay.deterministic_digest() == runtime->spu().deterministic_digest(),
            "snapshot preserves native CDDA phase and queued sector");

    advance(*runtime, 768U);
    std::array<clean_aot::spu::StereoSample, 1U> replay_sample{};
    require(replay.render(replay_sample).ok(), "render snapshot continuation");
    require(runtime->spu().input_status(clean_aot::spu::AudioSource::Cdda).queued ==
                clean_aot::cdrom::CddaSamplesPerSector - 1U &&
                replay.input_status(clean_aot::spu::AudioSource::Cdda).queued ==
                    clean_aot::cdrom::CddaSamplesPerSector - 1U &&
                replay.deterministic_digest() == runtime->spu().deterministic_digest(),
            "first post-deadline sample consumes exactly one queued CDDA frame");
}

void test_chunking_reaches_identical_boundary() {
    AudioSource direct_source{};
    AudioSource chunked_source{};
    auto direct = make_runtime(direct_source);
    auto chunked = make_runtime(chunked_source);
    start_track(*direct);
    start_track(*chunked);
    drain_all_audio(*direct);
    drain_all_audio(*chunked);

    advance(*direct, clean_aot::cdrom::SingleSpeedSectorCycles);
    constexpr std::array<std::uint64_t, 5U> chunks{
        1U, 767U, 338'688U, 100'000U,
        clean_aot::cdrom::SingleSpeedSectorCycles - 439'456U};
    for (const std::uint64_t chunk : chunks) {
        advance(*chunked, chunk);
    }
    require(direct_source.reads == 1U && chunked_source.reads == 1U &&
                direct->spu().deterministic_digest() ==
                    chunked->spu().deterministic_digest(),
            "arbitrary native chunks preserve exact CDDA/SPU event ordering");
}

} // namespace

int main() {
    test_sector_publication_is_not_retroactive();
    test_chunking_reaches_identical_boundary();
    std::cout << "clean_aot_spu_cdda_native_order_tests: ok\n";
    return EXIT_SUCCESS;
}
