#include "cdrom.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>

namespace {

using clean_aot::cdrom::AudioMatrix;
using clean_aot::cdrom::CddaSamplesPerSector;
using clean_aot::cdrom::CddaSink;
using clean_aot::cdrom::Cdrom;
using clean_aot::cdrom::Command;
using clean_aot::cdrom::CommandError;
using clean_aot::cdrom::CommandLatencyCycles;
using clean_aot::cdrom::CpuClockHz;
using clean_aot::cdrom::CyclesPer100HzUpdate;
using clean_aot::cdrom::DataOnlyBytes;
using clean_aot::cdrom::DiscLayout;
using clean_aot::cdrom::DmaChannel3;
using clean_aot::cdrom::DoubleSpeedSectorCycles;
using clean_aot::cdrom::ErrorCode;
using clean_aot::cdrom::FormOnePayloadBytes;
using clean_aot::cdrom::FormTwoPayloadBytes;
using clean_aot::cdrom::Interrupt;
using clean_aot::cdrom::InitFirstResponseCycles;
using clean_aot::cdrom::Lba;
using clean_aot::cdrom::MachineScheduler;
using clean_aot::cdrom::MaximumAddressableLba;
using clean_aot::cdrom::ModeOnePayloadBytes;
using clean_aot::cdrom::RawSector;
using clean_aot::cdrom::RawSectorSource;
using clean_aot::cdrom::ScheduledEvent;
using clean_aot::cdrom::SeekLatencyCycles;
using clean_aot::cdrom::SingleSpeedSectorCycles;
using clean_aot::cdrom::SpuClockHz;
using clean_aot::cdrom::Status;
using clean_aot::cdrom::StereoSample;
using clean_aot::cdrom::Track;
using clean_aot::cdrom::TrackKind;
using clean_aot::cdrom::WholeSectorBytes;
using clean_aot::cdrom::XaAdpcmBytes;
using clean_aot::cdrom::XaMetadata;
using clean_aot::cdrom::XaSectorSink;

static_assert(noexcept(std::declval<Cdrom&>().advance_to(0U)));
static_assert(noexcept(std::declval<Cdrom&>().read_register(0U)));
static_assert(noexcept(std::declval<Cdrom&>().write_register(0U, 0U)));

constexpr std::uint8_t fixture_bcd(std::uint32_t value) noexcept {
    return static_cast<std::uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_u32(RawSector& sector, std::size_t offset, std::uint32_t value) noexcept {
    sector.bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    sector.bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    sector.bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    sector.bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void set_raw_header(RawSector& sector, Lba lba, std::uint8_t mode) noexcept {
    sector.bytes[0U] = 0U;
    std::fill(sector.bytes.begin() + 1, sector.bytes.begin() + 11, 0xffU);
    sector.bytes[11U] = 0U;
    const std::uint32_t absolute = static_cast<std::uint32_t>(lba) + 150U;
    sector.bytes[12U] = fixture_bcd(absolute / 4500U);
    sector.bytes[13U] = fixture_bcd((absolute / 75U) % 60U);
    sector.bytes[14U] = fixture_bcd(absolute % 75U);
    sector.bytes[15U] = mode;
}

struct ModeOneFixture final {
    std::uint32_t edc = 0U;
    std::uint8_t seed = 0U;
};

// Fixed fixture checksums were generated independently from the sector decoder.
constexpr std::array<ModeOneFixture, 20U> ModeOneFixtures{{
    {0xab84e140U, 0x10U}, {0x06e112b6U, 0x11U}, {0x9d4dbe42U, 0x12U},
    {0xdf7531a2U, 0x13U}, {0xf815406cU, 0x14U}, {0x13747f82U, 0x15U},
    {0x851ecfbaU, 0x16U}, {0xadeb4a46U, 0x17U}, {0x59a5c60dU, 0x18U},
    {0xb64fdfbaU, 0x19U}, {0x179e0314U, 0x1aU}, {0xbe23b43bU, 0x1bU},
    {0xd49a198aU, 0x1cU}, {0xb011852fU, 0x1dU}, {0x218602f3U, 0x1eU},
    {0x52e29baeU, 0x1fU}, {0x06a19d31U, 0x20U}, {0xe00a4486U, 0x21U},
    {0x35badd91U, 0x22U}, {0xf007a3ffU, 0x23U},
}};

RawSector mode_one_sector(Lba lba) {
    require(lba < ModeOneFixtures.size(), "Mode 1 fixture LBA range");
    const ModeOneFixture fixture = ModeOneFixtures[static_cast<std::size_t>(lba)];
    RawSector sector{};
    set_raw_header(sector, lba, 1U);
    for (std::size_t index = 0U; index < ModeOnePayloadBytes; ++index) {
        sector.bytes[16U + index] =
            static_cast<std::uint8_t>(fixture.seed + static_cast<std::uint8_t>(index));
    }
    write_u32(sector, 2064U, fixture.edc);
    return sector;
}

RawSector mode_two_form_one_sector() {
    RawSector sector{};
    set_raw_header(sector, 1U, 2U);
    const std::array<std::uint8_t, 4U> subheader{3U, 7U, 0x08U, 0U};
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 16);
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 20);
    for (std::size_t index = 0U; index < FormOnePayloadBytes; ++index) {
        sector.bytes[24U + index] =
            static_cast<std::uint8_t>(0x20U + static_cast<std::uint8_t>(index));
    }
    write_u32(sector, 2072U, 0x289fda83U);
    return sector;
}

RawSector mode_two_form_two_sector(bool zero_edc) {
    RawSector sector{};
    set_raw_header(sector, 2U, 2U);
    const std::array<std::uint8_t, 4U> subheader{3U, 7U, 0x28U, 0U};
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 16);
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 20);
    for (std::size_t index = 0U; index < FormTwoPayloadBytes; ++index) {
        sector.bytes[24U + index] =
            static_cast<std::uint8_t>(0x30U + static_cast<std::uint8_t>(index));
    }
    if (!zero_edc) {
        write_u32(sector, 2348U, 0x504fd78cU);
    }
    return sector;
}

RawSector xa_audio_sector() {
    RawSector sector{};
    set_raw_header(sector, 3U, 2U);
    const std::array<std::uint8_t, 4U> subheader{3U, 7U, 0x64U, 1U};
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 16);
    std::copy(subheader.begin(), subheader.end(), sector.bytes.begin() + 20);
    for (std::size_t index = 0U; index < FormTwoPayloadBytes; ++index) {
        sector.bytes[24U + index] =
            static_cast<std::uint8_t>(0x40U + static_cast<std::uint8_t>(index));
    }
    return sector;
}

RawSector cdda_sector(std::int16_t left, std::int16_t right) noexcept {
    RawSector sector{};
    const std::uint16_t left_word = static_cast<std::uint16_t>(left);
    const std::uint16_t right_word = static_cast<std::uint16_t>(right);
    for (std::size_t index = 0U; index < CddaSamplesPerSector; ++index) {
        const std::size_t offset = index * 4U;
        sector.bytes[offset] = static_cast<std::uint8_t>(left_word & 0xffU);
        sector.bytes[offset + 1U] = static_cast<std::uint8_t>(left_word >> 8U);
        sector.bytes[offset + 2U] = static_cast<std::uint8_t>(right_word & 0xffU);
        sector.bytes[offset + 3U] = static_cast<std::uint8_t>(right_word >> 8U);
    }
    return sector;
}

class MemorySource final : public RawSectorSource {
public:
    std::array<RawSector, 32U> sectors{};
    std::array<bool, 32U> present{};
    std::array<Lba, 64U> reads{};
    std::size_t read_count = 0U;

    bool read_raw_sector(Lba lba, RawSector& destination) noexcept override {
        if (lba >= sectors.size() || !present[static_cast<std::size_t>(lba)]) {
            return false;
        }
        destination = sectors[static_cast<std::size_t>(lba)];
        if (read_count < reads.size()) {
            reads[read_count] = lba;
        }
        ++read_count;
        return true;
    }
};

class RepeatingCddaSource final : public RawSectorSource {
public:
    std::size_t read_count = 0U;

    bool read_raw_sector(Lba,
                         RawSector& destination) noexcept override {
        destination = cdda_sector(std::numeric_limits<std::int16_t>::min(),
                                  1234);
        ++read_count;
        return true;
    }
};

class SchedulerLog final : public MachineScheduler {
public:
    struct Receipt final {
        clean_aot::cdrom::Cycle cycle = 0U;
        ScheduledEvent event = ScheduledEvent::CommandStart;
    };

    std::array<Receipt, 256U> receipts{};
    std::size_t count = 0U;
    std::size_t cancels = 0U;

    void schedule_cdrom_event(clean_aot::cdrom::Cycle cycle,
                              ScheduledEvent event) noexcept override {
        if (count < receipts.size()) {
            receipts[count] = Receipt{cycle, event};
        }
        ++count;
    }

    void cancel_cdrom_event(ScheduledEvent) noexcept override {
        ++cancels;
    }
};

class DmaLog final : public DmaChannel3 {
public:
    std::array<std::uint8_t, WholeSectorBytes * 4U> bytes{};
    std::array<std::size_t, 4U> lengths{};
    std::size_t count = 0U;
    std::size_t byte_count = 0U;

    void receive_cdrom_data(std::span<const std::uint8_t> data) noexcept override {
        if (count < lengths.size()) {
            lengths[count] = data.size();
        }
        ++count;
        for (const std::uint8_t value : data) {
            if (byte_count < bytes.size()) {
                bytes[byte_count] = value;
            }
            ++byte_count;
        }
    }
};

class AudioLog final : public CddaSink {
public:
    std::array<StereoSample, CddaSamplesPerSector * 8U> samples{};
    std::array<Lba, 8U> lbas{};
    std::size_t sample_count = 0U;
    std::size_t sector_count = 0U;

    void receive_cdda_samples(Lba lba,
                              std::span<const StereoSample> received) noexcept override {
        if (sector_count < lbas.size()) {
            lbas[sector_count] = lba;
        }
        ++sector_count;
        for (const StereoSample sample : received) {
            if (sample_count < samples.size()) {
                samples[sample_count] = sample;
            }
            ++sample_count;
        }
    }
};

class AudioDigestLog final : public CddaSink {
public:
    std::uint64_t digest = 1469598103934665603ULL;
    std::size_t sector_count = 0U;

    void receive_cdda_samples(Lba lba,
                              std::span<const StereoSample> received) noexcept override {
        const auto mix = [this](std::uint8_t byte) noexcept {
            digest ^= byte;
            digest *= 1099511628211ULL;
        };
        for (unsigned byte = 0U; byte < 4U; ++byte) {
            mix(static_cast<std::uint8_t>(lba >> (byte * 8U)));
        }
        for (const StereoSample sample : received) {
            const std::uint16_t left = static_cast<std::uint16_t>(sample.left);
            const std::uint16_t right = static_cast<std::uint16_t>(sample.right);
            mix(static_cast<std::uint8_t>(left));
            mix(static_cast<std::uint8_t>(left >> 8U));
            mix(static_cast<std::uint8_t>(right));
            mix(static_cast<std::uint8_t>(right >> 8U));
        }
        ++sector_count;
    }
};

class XaLog final : public XaSectorSink {
public:
    std::array<std::uint8_t, XaAdpcmBytes> bytes{};
    XaMetadata metadata{};
    AudioMatrix matrix{};
    Lba lba = 0U;
    std::size_t size = 0U;
    std::size_t count = 0U;
    bool muted = false;

    void receive_xa_sector(Lba received_lba,
                           const XaMetadata& received_metadata,
                           std::span<const std::uint8_t> adpcm_bytes,
                           AudioMatrix received_matrix,
                           bool received_muted) noexcept override {
        lba = received_lba;
        metadata = received_metadata;
        matrix = received_matrix;
        muted = received_muted;
        size = adpcm_bytes.size();
        std::copy(adpcm_bytes.begin(), adpcm_bytes.end(), bytes.begin());
        ++count;
    }
};

DiscLayout data_layout(Lba lead_out = 20U) {
    DiscLayout layout{};
    layout.track_count = 1U;
    layout.tracks[0U] = Track{1U, 0U, TrackKind::Data};
    layout.lead_out_lba = lead_out;
    layout.disc_type = 0x20U;
    layout.license_region = {'R', 'E', 'G', 'N'};
    return layout;
}

DiscLayout mixed_layout() {
    DiscLayout layout{};
    layout.track_count = 3U;
    layout.tracks[0U] = Track{1U, 0U, TrackKind::Data};
    layout.tracks[1U] = Track{2U, 2U, TrackKind::Audio};
    layout.tracks[2U] = Track{3U, 4U, TrackKind::Audio};
    layout.lead_out_lba = 6U;
    layout.disc_type = 0x20U;
    layout.license_region = {'R', 'E', 'G', 'N'};
    return layout;
}

DiscLayout wipeout_special_edition_layout() {
    DiscLayout layout{};
    layout.track_count = 14U;
    layout.lead_out_lba = 274479U;
    layout.disc_type = 0x20U;
    layout.license_region = {'S', 'C', 'E', 'E'};
    layout.tracks[0U] = Track{1U, 0U, TrackKind::Data};
    constexpr std::array<Lba, 13U> index_zero{
        40247U, 58730U, 72723U, 92156U, 111072U, 130000U, 146708U,
        163286U, 182593U, 201706U, 220884U, 237542U, 255788U};
    constexpr std::array<Lba, 13U> index_one{
        40397U, 58880U, 72873U, 92306U, 111222U, 130150U, 146858U,
        163436U, 182743U, 201856U, 221034U, 237692U, 255938U};
    for (std::size_t index = 0U; index < index_zero.size(); ++index) {
        layout.tracks[index + 1U] =
            Track{static_cast<std::uint8_t>(index + 2U), index_one[index],
                  TrackKind::Audio, index_zero[index], true};
    }
    return layout;
}

void populate_mode_one(MemorySource& source, Lba first, Lba count) {
    for (Lba offset = 0U; offset < count; ++offset) {
        const Lba lba = first + offset;
        source.sectors[static_cast<std::size_t>(lba)] = mode_one_sector(lba);
        source.present[static_cast<std::size_t>(lba)] = true;
    }
}

void populate_mixed(MemorySource& source) {
    populate_mode_one(source, 0U, 2U);
    source.sectors[2U] = cdda_sector(std::numeric_limits<std::int16_t>::min(),
                                     std::numeric_limits<std::int16_t>::max());
    source.sectors[3U] = cdda_sector(1000, -1000);
    source.sectors[4U] = cdda_sector(2000, -2000);
    source.sectors[5U] = cdda_sector(3000, -3000);
    for (std::size_t index = 2U; index < 6U; ++index) {
        source.present[index] = true;
    }
}

void advance_command(Cdrom& device) {
    device.advance_to(device.current_cycle() + CommandLatencyCycles);
}

void issue(Cdrom& device,
           Command command,
           std::initializer_list<std::uint8_t> parameters = {}) {
    for (const std::uint8_t parameter : parameters) {
        require(device.enqueue_parameter(parameter), "queue command parameter");
    }
    require(device.enqueue_command(static_cast<std::uint8_t>(command)), "queue command");
    advance_command(device);
}

void require_reason(const Cdrom& device, Interrupt reason, const std::string& message) {
    require(device.interrupt_flags() == clean_aot::cdrom::interrupt_code(reason), message);
}

void acknowledge(Cdrom& device) {
    device.acknowledge_interrupts(0x07U);
}

void acknowledge_one_byte(Cdrom& device, std::uint8_t expected) {
    require(device.response_count() == 1U, "one-byte response size");
    require(device.pop_response() == expected, "one-byte response value");
    acknowledge(device);
}

void set_location(Cdrom& device, Lba lba) {
    const std::uint32_t absolute = static_cast<std::uint32_t>(lba) + 150U;
    issue(device,
          Command::Setloc,
          {fixture_bcd(absolute / 4500U),
           fixture_bcd((absolute / 75U) % 60U),
           fixture_bcd(absolute % 75U)});
    require_reason(device, Interrupt::Acknowledge, "Setloc acknowledge reason");
    (void)device.pop_response();
    acknowledge(device);
}

void request_data(Cdrom& device) {
    device.write_register(0U, 0U);
    device.write_register(3U, 0x80U);
}

void clear_data_request(Cdrom& device) {
    device.write_register(0U, 0U);
    device.write_register(3U, 0U);
}

void test_timing_bcd_and_msf_boundaries() {
    require(CpuClockHz == 33868800U && SpuClockHz == 44100U, "system clocks");
    require(SingleSpeedSectorCycles == 451584U && DoubleSpeedSectorCycles == 225792U,
            "sector periods");
    require(CyclesPer100HzUpdate == 338688U &&
                CyclesPer100HzUpdate * 4U == SingleSpeedSectorCycles * 3U,
            "exact three-sector/four-update cadence");
    require(CddaSamplesPerSector == 588U && CddaSamplesPerSector * 75U == SpuClockHz,
            "CDDA sample cadence");

    std::uint8_t decoded = 0U;
    require(Cdrom::from_bcd(0x59U, decoded) && decoded == 59U, "valid BCD");
    require(!Cdrom::from_bcd(0x6aU, decoded), "invalid BCD digit");
    require(Cdrom::to_bcd(99U) == 0x99U && Cdrom::to_bcd(100U) == 0xffU,
            "bounded BCD encoding");
    const std::array<std::uint8_t, 3U> invalid_frame{0x00U, 0x00U, 0x75U};
    clean_aot::cdrom::Msf ignored{};
    require(!Cdrom::decode_msf_bcd(invalid_frame, ignored), "invalid frame BCD");
    require(Cdrom::lba_to_msf(0U) == clean_aot::cdrom::Msf{0U, 2U, 0U},
            "first logical sector address");
    require(Cdrom::frames_to_msf(0U) == clean_aot::cdrom::Msf{0U, 0U, 0U},
            "relative position has no lead-in bias");
    require(Cdrom::lba_to_msf(MaximumAddressableLba) ==
                clean_aot::cdrom::Msf{99U, 59U, 74U},
            "maximum representable address");
    require(Cdrom::lba_to_msf(MaximumAddressableLba + 1U) ==
                clean_aot::cdrom::Msf{0xffU, 0xffU, 0xffU},
            "unrepresentable address fails closed");
    Lba lba = 0U;
    require(Cdrom::msf_to_lba(clean_aot::cdrom::Msf{99U, 59U, 74U}, lba) &&
                lba == MaximumAddressableLba,
            "maximum address round trip");
    require(!Cdrom::msf_to_lba(clean_aot::cdrom::Msf{0U, 1U, 74U}, lba),
            "lead-in cannot become logical data LBA");
}

void test_layout_validation_and_boundaries() {
    DiscLayout layout = mixed_layout();
    require(layout.valid(), "mixed layout valid");
    require(layout.track_at(0U) == 1U && layout.track_at(2U) == 2U &&
                layout.track_at(4U) == 3U && layout.track_at(6U) == 0xaaU,
            "track and lead-out boundaries");
    require(layout.track_for_lba(1U)->kind == TrackKind::Data &&
                layout.track_for_lba(2U)->kind == TrackKind::Audio,
            "track type lookup");

    const DiscLayout wipeout = wipeout_special_edition_layout();
    require(wipeout.valid() && wipeout.track_at(40246U) == 1U &&
                wipeout.track_at(40247U) == 2U &&
                wipeout.track_at(40396U) == 2U &&
                wipeout.find_track(2U)->start_lba == 40397U &&
                wipeout.track_at(274479U) == 0xaaU,
            "verified 14-track INDEX 00 ownership and exclusive lead-out");

    DiscLayout invalid_index_zero = wipeout;
    invalid_index_zero.tracks[1U].index_zero_lba = 40397U;
    require(!invalid_index_zero.valid(),
            "INDEX 00 must strictly precede its INDEX 01");
    invalid_index_zero = wipeout;
    invalid_index_zero.tracks[1U].index_zero_lba = 0U;
    require(!invalid_index_zero.valid(),
            "INDEX 00 cannot overlap the preceding program start");

    layout.tracks[1U].start_lba = 0U;
    require(!layout.valid(), "duplicate start rejected");
    layout = mixed_layout();
    layout.tracks[0U].start_lba = 1U;
    require(!layout.valid(), "nonzero logical disc origin rejected");
    layout = mixed_layout();
    layout.track_count = 100U;
    require(!layout.valid() && layout.find_track(1U) != nullptr,
            "oversized count is invalid but bounded lookup stays safe");
    layout.track_count = 0U;
    require(layout.track_for_lba(0U) == nullptr && layout.track_at(0U) == 0xffU,
            "empty layout lookup is safe");
}

void test_raw_sector_contracts() {
    clean_aot::cdrom::SectorView view{};
    RawSector mode_one = mode_one_sector(0U);
    require(Cdrom::decode_data_sector(0U, mode_one, view), "known Mode 1 fixture");
    require(view.kind == clean_aot::cdrom::SectorKind::ModeOne &&
                view.payload_size == ModeOnePayloadBytes && view.payload[0U] == 0x10U,
            "Mode 1 payload");

    RawSector old_range_checksum = mode_one;
    write_u32(old_range_checksum, 2064U, 0x93b3619fU);
    require(!Cdrom::decode_data_sector(0U, old_range_checksum, view),
            "checksum excluding sync bytes is rejected");
    RawSector damaged = mode_one;
    damaged.bytes[1000U] ^= 0x01U;
    require(!Cdrom::decode_data_sector(0U, damaged, view), "payload damage rejected");
    damaged = mode_one;
    damaged.bytes[1U] = 0U;
    require(!Cdrom::decode_data_sector(0U, damaged, view), "sync damage rejected");
    require(!Cdrom::decode_data_sector(1U, mode_one, view), "header/LBA mismatch rejected");

    const RawSector form_one = mode_two_form_one_sector();
    require(Cdrom::decode_data_sector(1U, form_one, view), "known Mode 2 Form 1 fixture");
    require(view.kind == clean_aot::cdrom::SectorKind::ModeTwoFormOne &&
                view.payload_size == FormOnePayloadBytes && view.xa.file == 3U &&
                view.xa.channel == 7U && view.xa.data,
            "Form 1 payload and XA metadata");
    const RawSector form_two = mode_two_form_two_sector(false);
    require(Cdrom::decode_data_sector(2U, form_two, view),
            "known Mode 2 Form 2 fixture");
    require(view.kind == clean_aot::cdrom::SectorKind::ModeTwoFormTwo &&
                view.payload_size == FormTwoPayloadBytes && view.xa.form_two,
            "Form 2 payload");
    const RawSector form_two_without_edc = mode_two_form_two_sector(true);
    require(Cdrom::decode_data_sector(2U, form_two_without_edc, view),
            "optional zero Form 2 EDC accepted");
    damaged = form_one;
    damaged.bytes[20U] ^= 0x01U;
    require(!Cdrom::decode_data_sector(1U, damaged, view),
            "mismatched repeated subheader rejected");
}

void test_host_registers_interrupt_reasons_and_latched_audio() {
    Cdrom device;
    require(device.read_register(0U) == 0x18U, "initial host FIFO status");
    device.write_register(0U, 1U);
    device.write_register(2U, 0x1fU);
    device.write_register(0U, 0U);
    require(device.read_register(3U) == 0xffU, "interrupt mask reserved read bits");
    require(device.enqueue_command(static_cast<std::uint8_t>(Command::Nop)), "queue Nop");
    require((device.read_register(0U) & 0x80U) != 0U, "BUSYSTS while command transfers");
    advance_command(device);
    require_reason(device, Interrupt::Acknowledge, "Nop uses INT3 reason code");
    require(device.irq_line(), "reason code intersects enabled mask");
    require((device.read_register(0U) & 0x80U) == 0U, "BUSYSTS clears at first response");
    require(device.pop_response() == 0U && device.response_count() == 0U,
            "Nop status response");
    require(device.pop_response() == 0U && device.last_error() == ErrorCode::None,
            "result padding is not a synthetic underflow error");
    device.acknowledge_interrupts(0x01U);
    require_reason(device, Interrupt::Complete, "partial reason clear changes 3 to 2");
    device.acknowledge_interrupts(0x02U);
    require_reason(device, Interrupt::None, "remaining reason bit clears");

    device.write_register(0U, 2U);
    device.write_register(2U, 0x40U);
    device.write_register(3U, 0x20U);
    device.write_register(0U, 3U);
    device.write_register(1U, 0x60U);
    device.write_register(2U, 0x10U);
    require(device.audio_matrix() == AudioMatrix{}, "volume writes are staged");
    device.write_register(3U, 0x20U);
    require(device.audio_matrix() == AudioMatrix{0x40U, 0x10U, 0x20U, 0x60U},
            "ADPCTL applies all four staged coefficients atomically");
    device.write_register(3U, 0x01U);
    require(!device.muted(), "host XA mute is separate from command CD mute");
    require((device.read_register32(0U) & 0xffU) ==
                ((device.read_register32(0U) >> 24U) & 0xffU),
            "wide HSTS reads repeat the selected port");

    Cdrom invalid_width;
    require(invalid_width.read_register16(1U) == 0xffffU &&
                invalid_width.last_error() == ErrorCode::InvalidRegister,
            "unsupported wide result read fails closed");
}

void test_parameter_ownership_errors_and_second_response_queue() {
    Cdrom device;
    issue(device, Command::Setmode, {0x80U, 0x20U});
    require_reason(device, Interrupt::Error, "wrong count uses INT5");
    require(device.response_count() == 2U &&
                device.pop_response() == clean_aot::cdrom::status_bit(Status::Error) &&
                device.pop_response() ==
                    static_cast<std::uint8_t>(CommandError::WrongParameterCount),
            "wrong count response bytes");
    require(device.mode() == 0U, "wrong count has no mode side effect");
    acknowledge(device);

    issue(device, Command::Setloc, {0x00U, 0x02U, 0x7aU});
    require_reason(device, Interrupt::Error, "invalid packed BCD uses INT5");
    require(device.pop_response() == clean_aot::cdrom::status_bit(Status::Error) &&
                device.pop_response() ==
                    static_cast<std::uint8_t>(CommandError::InvalidParameter),
            "invalid parameter response bytes");
    acknowledge(device);

    issue(device, Command::Nop);
    require(device.enqueue_parameter(0x80U), "snapshot mode parameter");
    require(device.enqueue_command(static_cast<std::uint8_t>(Command::Setmode)),
            "command may wait behind pending interrupt");
    require(device.enqueue_parameter(0x20U), "next command parameter remains separate");
    require(!device.enqueue_command(static_cast<std::uint8_t>(Command::Nop)) &&
                device.last_error() == ErrorCode::CommandBusy,
            "single pending command register is bounded");
    (void)device.pop_response();
    acknowledge(device);
    advance_command(device);
    require(device.mode() == 0x80U && device.parameter_count() == 1U,
            "command uses write-time parameter snapshot");
    acknowledge_one_byte(device, 0U);
    require(device.enqueue_command(static_cast<std::uint8_t>(Command::Setmode)),
            "next command consumes only next parameter");
    advance_command(device);
    require(device.mode() == 0x20U, "next parameter does not bleed backward");
    acknowledge_one_byte(device, 0U);

    device.reset();
    for (std::size_t index = 0U; index < Cdrom::ParameterFifoCapacity; ++index) {
        require(device.enqueue_parameter(static_cast<std::uint8_t>(index)),
                "16-byte parameter FIFO accepts capacity");
    }
    require(!device.enqueue_parameter(0U) &&
                device.last_error() == ErrorCode::ParameterOverflow,
            "parameter FIFO rejects byte 17 without overwrite");

    device.reset();
    issue(device, Command::Standby);
    require_reason(device, Interrupt::Acknowledge, "Standby first response INT3");
    require(device.pop_response() == 0U, "Standby first status has motor off");
    device.advance_to(device.current_cycle() + CommandLatencyCycles);
    require(device.pending_interrupt_count() == 2U &&
                device.interrupt_flags() ==
                    clean_aot::cdrom::interrupt_code(Interrupt::Acknowledge),
            "completion waits behind unacknowledged first response");
    acknowledge(device);
    require_reason(device, Interrupt::Complete, "Standby second response INT2");
    require(device.pop_response() == clean_aot::cdrom::status_bit(Status::MotorOn),
            "Standby completion reports motor on");
    acknowledge(device);
}

void test_seek_status_queries_and_identity_packets() {
    auto source = std::make_unique<MemorySource>();
    populate_mixed(*source);
    Cdrom device(source.get(), mixed_layout());
    set_location(device, 2U);
    issue(device, Command::SeekP);
    require_reason(device, Interrupt::Acknowledge, "SeekP first response INT3");
    require(device.pop_response() ==
                static_cast<std::uint8_t>(clean_aot::cdrom::status_bit(Status::Seeking) |
                                          clean_aot::cdrom::status_bit(Status::MotorOn)),
            "seek response exposes only seek and motor status");
    require(device.current_lba() == 0U, "seek target is not applied before completion");
    acknowledge(device);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "SeekP completion INT2");
    require(device.current_lba() == 2U &&
                device.pop_response() == clean_aot::cdrom::status_bit(Status::MotorOn),
            "seek completion lands at prior Setloc without command parameters");
    acknowledge(device);

    issue(device, Command::GetlocP);
    require_reason(device, Interrupt::Acknowledge, "GetlocP response INT3");
    const std::array<std::uint8_t, 8U> expected_position{
        0x02U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x02U};
    for (const std::uint8_t expected : expected_position) {
        require(device.pop_response() == expected, "GetlocP track-relative and absolute byte");
    }
    acknowledge(device);

    issue(device, Command::GetTN);
    require(device.pop_response() == clean_aot::cdrom::status_bit(Status::MotorOn) &&
                device.pop_response() == 0x01U && device.pop_response() == 0x03U,
            "GetTN exact response");
    acknowledge(device);
    issue(device, Command::GetTD, {0x00U});
    require(device.pop_response() == clean_aot::cdrom::status_bit(Status::MotorOn) &&
                device.pop_response() == 0x00U && device.pop_response() == 0x02U,
            "GetTD lead-out response is status plus MM:SS");
    acknowledge(device);

    issue(device, Command::ID);
    require_reason(device, Interrupt::Acknowledge, "GetID first response INT3");
    require(device.response_count() == 1U, "GetID first packet is isolated");
    device.advance_to(device.current_cycle() + CommandLatencyCycles);
    require(device.pending_interrupt_count() == 2U, "GetID completion packet queued");
    (void)device.pop_response();
    acknowledge(device);
    require_reason(device, Interrupt::Complete, "GetID licensed completion INT2");
    const std::array<std::uint8_t, 8U> expected_id{
        0x02U, 0x00U, 0x20U, 0x00U, 'R', 'E', 'G', 'N'};
    require(device.response_count() == expected_id.size(), "GetID completion size");
    for (const std::uint8_t expected : expected_id) {
        require(device.pop_response() == expected, "GetID configured identity byte");
    }
    acknowledge(device);

    DiscLayout neutral_layout = mixed_layout();
    neutral_layout.license_region = {};
    Cdrom neutral_identity(source.get(), neutral_layout);
    issue(neutral_identity, Command::ID);
    (void)neutral_identity.pop_response();
    acknowledge(neutral_identity);
    neutral_identity.advance_to(neutral_identity.current_cycle() + CommandLatencyCycles);
    require_reason(neutral_identity, Interrupt::Error,
                   "missing caller identity produces denied GetID packet");
    const std::array<std::uint8_t, 8U> expected_denied{
        0x08U, 0x80U, 0x20U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    for (const std::uint8_t expected : expected_denied) {
        require(neutral_identity.pop_response() == expected,
                "neutral GetID denied response byte");
    }
}

void test_wipeout_real_layout_toc_and_pregap_setloc() {
    MemorySource source;
    Cdrom device(&source, wipeout_special_edition_layout());

    issue(device, Command::GetTN);
    require(device.pop_response() == 0U && device.pop_response() == 0x01U &&
                device.pop_response() == 0x14U,
            "verified WipEout layout reports tracks 01 through 14");
    acknowledge(device);

    issue(device, Command::GetTD, {0x02U});
    require(device.pop_response() == 0U && device.pop_response() == 0x09U &&
                device.pop_response() == 0x00U,
            "GetTD track 02 reports INDEX 01 at absolute 09:00:47");
    acknowledge(device);
    issue(device, Command::GetTD, {0x14U});
    require(device.pop_response() == 0U && device.pop_response() == 0x56U &&
                device.pop_response() == 0x54U,
            "GetTD track 14 reports INDEX 01 at absolute 56:54:38");
    acknowledge(device);
    issue(device, Command::GetTD, {0x00U});
    require(device.pop_response() == 0U && device.pop_response() == 0x61U &&
                device.pop_response() == 0x01U,
            "GetTD track 00 reports exclusive lead-out at absolute 61:01:54");
    acknowledge(device);

    set_location(device, 40247U);
    issue(device, Command::SeekP);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require(device.current_lba() == 40247U,
            "Setloc packed BCD resolves the stored track-02 pregap LBA");
    (void)device.pop_response();
    acknowledge(device);
    issue(device, Command::GetlocP);
    require(device.pop_response() == 0x02U,
            "INDEX 00 pregap is owned by audio track 02, not data track 01");
    require(device.pop_response() == 0x00U,
            "GetlocP reports INDEX 00 inside the stored audio pregap");
    acknowledge(device);
}

void test_data_request_widths_sector_sizes_and_location_header() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 4U);
    source->sectors[2U] = mode_two_form_two_sector(true);
    source->present[2U] = true;
    Cdrom device(source.get(), data_layout());
    issue(device, Command::ReadN);
    require_reason(device, Interrupt::Acknowledge, "ReadN acknowledge");
    require(device.pop_response() ==
                static_cast<std::uint8_t>(clean_aot::cdrom::status_bit(Status::Reading) |
                                          clean_aot::cdrom::status_bit(Status::MotorOn)),
            "ReadN status bits");
    acknowledge(device);
    const clean_aot::cdrom::Cycle first_sector =
        device.current_cycle() + SingleSpeedSectorCycles;
    device.advance_to(first_sector - 1U);
    require(source->read_count == 0U, "sector not delivered one cycle early");
    device.advance_to(first_sector);
    require_reason(device, Interrupt::DataReady, "sector uses INT1 reason");
    require(device.pop_response() == 0x22U, "INT1 carries read status byte");
    require(device.data_count() == 0U && (device.read_register(0U) & 0x40U) == 0U,
            "sector buffer is not exposed before BFRD");
    request_data(device);
    require(device.data_count() == DataOnlyBytes && (device.read_register(0U) & 0x40U) != 0U,
            "BFRD exposes one 2048-byte transfer");
    require(device.read_register16(2U) == 0x1110U, "16-bit RDDATA is little endian");
    require(device.read_register32(2U) == 0x15141312U,
            "32-bit RDDATA consumes four sequential bytes");
    while (device.data_count() != 0U) {
        (void)device.pop_data();
    }
    require(device.pop_data() == 0x08U, "data-only overread repeats byte size-minus-eight");
    acknowledge(device);

    issue(device, Command::GetlocL);
    require_reason(device, Interrupt::Acknowledge, "GetlocL INT3");
    const std::array<std::uint8_t, 8U> expected_header{
        0x00U, 0x02U, 0x00U, 0x01U, 0x10U, 0x11U, 0x12U, 0x13U};
    for (const std::uint8_t expected : expected_header) {
        require(device.pop_response() == expected, "GetlocL last delivered raw header byte");
    }
    acknowledge(device);
    issue(device, Command::GetlocP);
    const std::array<std::uint8_t, 8U> expected_position{
        0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U};
    for (const std::uint8_t expected : expected_position) {
        require(device.pop_response() == expected, "GetlocP last-sector position byte");
    }
    acknowledge(device);
    device.reset();

    issue(device, Command::Setmode, {0x20U});
    acknowledge_one_byte(device, 0U);
    issue(device, Command::ReadN);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    (void)device.pop_response();
    request_data(device);
    require(device.data_count() == WholeSectorBytes, "whole-sector mode exposes 2340 bytes");
    require(device.read_register32(2U) == 0x01000200U,
            "whole-sector transfer begins with minute/second/frame/mode header");
    clear_data_request(device);
    acknowledge(device);
    device.reset();

    set_location(device, 2U);
    issue(device, Command::ReadS);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    (void)device.pop_response();
    request_data(device);
    require(device.data_count() == DataOnlyBytes && device.pop_data() == 0x30U,
            "Form 2 data-only transfer is 2048 bytes, not the full 2324-byte payload");
}

void test_dma_is_request_gated_and_cadence_is_exact() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 5U);
    DmaLog dma;
    SchedulerLog scheduler;
    Cdrom device(source.get(), data_layout());
    device.attach_dma(&dma);
    device.attach_scheduler(&scheduler);
    device.set_dma_enabled(true);
    issue(device, Command::ReadN);
    (void)device.pop_response();
    acknowledge(device);
    clean_aot::cdrom::Cycle previous_sector_cycle = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        clear_data_request(device);
        const clean_aot::cdrom::Cycle event_cycle =
            device.current_cycle() + SingleSpeedSectorCycles;
        device.advance_to(event_cycle);
        require(dma.count == index, "DMA does not run merely because a sector arrived");
        (void)device.pop_response();
        request_data(device);
        require(dma.count == index + 1U && dma.lengths[index] == DataOnlyBytes,
                "DMA receives exactly one requested data-only block");
        require(device.data_count() == 0U, "DMA consumes the host transfer window");
        acknowledge(device);
        if (previous_sector_cycle != 0U) {
            require(event_cycle - previous_sector_cycle == SingleSpeedSectorCycles,
                    "successive sector events use exact one-speed period");
        }
        previous_sector_cycle = event_cycle;
    }
    require(source->read_count == 4U && dma.bytes[0U] == 0x10U &&
                dma.bytes[DataOnlyBytes] == 0x11U,
            "DMA preserves sector and byte order");

    Cdrom double_speed(source.get(), data_layout());
    issue(double_speed, Command::Setmode, {0x80U});
    acknowledge_one_byte(double_speed, 0U);
    issue(double_speed, Command::ReadN);
    (void)double_speed.pop_response();
    acknowledge(double_speed);
    const clean_aot::cdrom::Cycle start = double_speed.current_cycle();
    double_speed.advance_to(start + DoubleSpeedSectorCycles - 1U);
    require(double_speed.interrupt_flags() == 0U, "double-speed sector not early");
    double_speed.advance_to(start + DoubleSpeedSectorCycles);
    require_reason(double_speed, Interrupt::DataReady, "double-speed exact boundary");
}

void test_delayed_100hz_service_preserves_sector_irq_dma_order() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 8U);
    DmaLog dma;
    Cdrom device(source.get(), data_layout());
    device.attach_dma(&dma);
    device.set_dma_enabled(true);
    issue(device, Command::ReadN);
    (void)device.pop_response();
    acknowledge(device);

    /* Four 100 Hz updates span exactly three single-speed sector arrivals. */
    device.advance_to(device.current_cycle() + CyclesPer100HzUpdate * 4U);
    require(source->read_count == 3U &&
                device.pending_interrupt_count() == 3U && dma.count == 0U,
            "delayed four-frame service retains three INT1/data epochs");
    for (std::size_t sector = 0U; sector < 3U; ++sector) {
        require_reason(device, Interrupt::DataReady,
                       "buffered sector keeps its matching INT1 visible");
        (void)device.pop_response();
        clear_data_request(device);
        request_data(device);
        require(dma.count == sector + 1U &&
                    dma.lengths[sector] == DataOnlyBytes &&
                    dma.bytes[sector * DataOnlyBytes] ==
                        static_cast<std::uint8_t>(0x10U + sector),
                "buffered DMA payloads retain sector order at 100 Hz");
        acknowledge(device);
    }
    require(device.pending_interrupt_count() == 0U,
            "all delayed sector IRQ epochs drain exactly once");
}

void test_cdda_tracks_autopause_matrix_mute_and_100hz_relation() {
    auto source = std::make_unique<MemorySource>();
    populate_mixed(*source);
    AudioLog audio;
    Cdrom device(source.get(), mixed_layout());
    device.attach_cdda_sink(&audio);
    issue(device, Command::Play, {0x02U});
    require(device.pop_response() == 0x82U, "Play status has play and motor bits");
    acknowledge(device);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    require(audio.sector_count == 1U && audio.lbas[0U] == 2U &&
                audio.samples[0U] ==
                    StereoSample{std::numeric_limits<std::int16_t>::min(),
                                 std::numeric_limits<std::int16_t>::max()},
            "CDDA signed little-endian sample extremes are deterministic");
    require_reason(device, Interrupt::None, "ordinary CDDA sectors do not synthesize INT1");

    issue(device, Command::Mute);
    acknowledge_one_byte(device, 0x82U);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    require(audio.samples[CddaSamplesPerSector] == StereoSample{},
            "Mute preserves CDDA timing with zero samples");
    issue(device, Command::Demute);
    acknowledge_one_byte(device, 0x82U);

    auto matrix_source = std::make_unique<MemorySource>();
    populate_mixed(*matrix_source);
    matrix_source->sectors[2U] = cdda_sector(1000, -1000);
    AudioLog matrix_audio;
    Cdrom matrix_device(matrix_source.get(), mixed_layout());
    matrix_device.attach_cdda_sink(&matrix_audio);
    matrix_device.set_audio_matrix(AudioMatrix{0U, 0x80U, 0x80U, 0U});
    issue(matrix_device, Command::Play, {0x02U});
    (void)matrix_device.pop_response();
    acknowledge(matrix_device);
    matrix_device.advance_to(matrix_device.current_cycle() + SingleSpeedSectorCycles);
    require(matrix_audio.samples[0U] == StereoSample{-1000, 1000},
            "active CDDA matrix mixes left and right outputs");

    AudioLog data_track_audio;
    Cdrom data_track_play(source.get(), mixed_layout());
    data_track_play.attach_cdda_sink(&data_track_audio);
    issue(data_track_play, Command::Play, {0x01U});
    (void)data_track_play.pop_response();
    acknowledge(data_track_play);
    data_track_play.advance_to(data_track_play.current_cycle() + SingleSpeedSectorCycles);
    require(data_track_audio.samples[0U] == StereoSample{},
            "Play over a data track produces silence");

    AudioLog paused_audio;
    Cdrom autopause(source.get(), mixed_layout());
    autopause.attach_cdda_sink(&paused_audio);
    issue(autopause, Command::Setmode, {0x02U});
    acknowledge_one_byte(autopause, 0U);
    issue(autopause, Command::Play, {0x02U});
    (void)autopause.pop_response();
    acknowledge(autopause);
    autopause.advance_to(autopause.current_cycle() + SingleSpeedSectorCycles * 3U);
    require(paused_audio.sector_count == 2U && !autopause.playing() &&
                autopause.current_lba() == 3U,
            "autopause remains at the end of the old track");
    require_reason(autopause, Interrupt::DataEnd, "autopause track transition INT4");
    require(autopause.pop_response() == clean_aot::cdrom::status_bit(Status::MotorOn),
            "autopause leaves motor on and play bit clear");
    acknowledge(autopause);
    issue(autopause, Command::Play);
    (void)autopause.pop_response();
    acknowledge(autopause);
    autopause.advance_to(autopause.current_cycle() + SingleSpeedSectorCycles * 2U);
    require(paused_audio.sector_count == 3U && autopause.current_lba() == 3U,
            "resume without a new location replays the last old-track sector then re-pauses");
    require_reason(autopause, Interrupt::DataEnd, "resumed old track reissues INT4");

    AudioLog cadence_audio;
    Cdrom cadence(source.get(), mixed_layout());
    cadence.attach_cdda_sink(&cadence_audio);
    issue(cadence, Command::Play, {0x02U});
    (void)cadence.pop_response();
    acknowledge(cadence);
    cadence.advance_to(cadence.current_cycle() + CyclesPer100HzUpdate * 4U);
    require(cadence_audio.sector_count == 3U,
            "exactly three CDDA sectors span four 100-Hz updates");
    cadence.advance_to(cadence.current_cycle() + SingleSpeedSectorCycles * 2U);
    require_reason(cadence, Interrupt::DataEnd, "lead-out produces INT4");
    require(!cadence.playing() &&
                (cadence.status() & clean_aot::cdrom::status_bit(Status::MotorOn)) == 0U,
            "lead-out stops CDDA and motor");

    AudioLog double_mode_audio;
    Cdrom double_mode(source.get(), mixed_layout());
    double_mode.attach_cdda_sink(&double_mode_audio);
    issue(double_mode, Command::Setmode, {0x80U});
    acknowledge_one_byte(double_mode, 0U);
    issue(double_mode, Command::Play, {0x02U});
    (void)double_mode.pop_response();
    acknowledge(double_mode);
    const clean_aot::cdrom::Cycle play_start = double_mode.current_cycle();
    double_mode.advance_to(play_start + DoubleSpeedSectorCycles);
    require(double_mode_audio.sector_count == 0U,
            "data double-speed mode cannot accelerate the first CDDA sector");
    double_mode.advance_to(play_start + SingleSpeedSectorCycles);
    require(double_mode_audio.sector_count == 1U && double_mode_audio.lbas[0U] == 2U,
            "CDDA first sector remains exactly 75 Hz in data double-speed mode");
    double_mode.advance_to(play_start + SingleSpeedSectorCycles * 2U);
    require(double_mode_audio.sector_count == 2U && double_mode_audio.lbas[1U] == 3U,
            "successive CDDA sectors remain exactly 75 Hz in data double-speed mode");
}

void test_wipeout_cdda_report_irq_and_exact_cadence() {
    RepeatingCddaSource source;
    AudioDigestLog audio;
    Cdrom device(&source, wipeout_special_edition_layout());
    device.attach_cdda_sink(&audio);
    issue(device, Command::Setmode, {0x04U});
    acknowledge_one_byte(device, 0U);
    issue(device, Command::Play, {0x02U});
    (void)device.pop_response();
    acknowledge(device);

    device.advance_to(device.current_cycle() + CyclesPer100HzUpdate * 4U);
    require(audio.sector_count == 3U && source.read_count == 3U &&
                device.interrupt_flags() == 0U,
            "three 75-Hz sectors precede the first report in four 100-Hz updates");
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    require(audio.sector_count == 4U && source.read_count == 4U,
            "fourth sector lands at the next exact 75-Hz deadline");
    require_reason(device, Interrupt::DataReady,
                   "track-02 relative CDDA report uses INT1");
    const std::array<std::uint8_t, 8U> relative_report{
        0x82U, 0x02U, 0x01U, 0x00U, 0x80U, 0x03U, 0xffU, 0x7fU};
    for (const std::uint8_t expected : relative_report) {
        require(device.pop_response() == expected,
                "relative CDDA report byte and pre-mute peak");
    }
    acknowledge(device);

    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles * 10U);
    require_reason(device, Interrupt::DataReady,
                   "alternating absolute CDDA report uses INT1");
    require(device.response_count() == 8U,
            "one absolute report packet is visible");
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles * 10U);
    require(device.response_count() == 8U && audio.sector_count == 24U,
            "report collision is dropped without stalling deterministic audio");
    require(audio.digest == 15774102990737321859ULL,
            "verified-layout CDDA report-path audio digest");
    const std::array<std::uint8_t, 8U> absolute_report{
        0x82U, 0x02U, 0x01U, 0x09U, 0x00U, 0x60U, 0xffU, 0x7fU};
    for (const std::uint8_t expected : absolute_report) {
        require(device.pop_response() == expected,
                "absolute CDDA report remains stable across dropped collision");
    }
    acknowledge(device);
}

void test_xa_routing_filter_and_explicit_decode_boundary() {
    auto source = std::make_unique<MemorySource>();
    source->sectors[3U] = xa_audio_sector();
    source->present[3U] = true;
    DiscLayout layout = data_layout(8U);

    XaLog xa;
    Cdrom device(source.get(), layout);
    device.attach_xa_sink(&xa);
    const AudioMatrix xa_matrix{0x40U, 0x10U, 0x20U, 0x60U};
    device.set_audio_matrix(xa_matrix);
    device.write_register(0U, 3U);
    device.write_register(3U, 0x01U);
    issue(device, Command::Setmode, {0x40U});
    acknowledge_one_byte(device, 0U);
    set_location(device, 3U);
    issue(device, Command::ReadS);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    require(xa.count == 1U && xa.lba == 3U && xa.size == XaAdpcmBytes &&
                xa.bytes[0U] == 0x40U && xa.metadata.audio && xa.metadata.real_time,
            "XA audio/realtime sector routes 2304 compressed bytes to XA sink");
    require(xa.matrix == xa_matrix && xa.muted,
            "XA handoff carries active matrix and combined mute state");
    require_reason(device, Interrupt::None, "routed XA sector does not also produce data INT1");
    require(device.data_count() == 0U, "routed XA sector is not exposed as CPU data");

    XaLog mismatch;
    Cdrom filtered(source.get(), layout);
    filtered.attach_xa_sink(&mismatch);
    issue(filtered, Command::Setfilter, {9U, 9U});
    acknowledge_one_byte(filtered, 0U);
    issue(filtered, Command::Setmode, {0x48U});
    acknowledge_one_byte(filtered, 0U);
    set_location(filtered, 3U);
    issue(filtered, Command::ReadS);
    (void)filtered.pop_response();
    acknowledge(filtered);
    filtered.advance_to(filtered.current_cycle() + SingleSpeedSectorCycles);
    require(mismatch.count == 0U && filtered.interrupt_flags() == 0U,
            "XA filter mismatch drops the interleaved audio sector");

    XaLog match;
    Cdrom matching(source.get(), layout);
    matching.attach_xa_sink(&match);
    issue(matching, Command::Setfilter, {3U, 7U});
    acknowledge_one_byte(matching, 0U);
    issue(matching, Command::Setmode, {0x48U});
    acknowledge_one_byte(matching, 0U);
    set_location(matching, 3U);
    issue(matching, Command::ReadS);
    (void)matching.pop_response();
    acknowledge(matching);
    matching.advance_to(matching.current_cycle() + SingleSpeedSectorCycles);
    require(match.count == 1U, "XA filter match reaches sink");
}

void test_remaining_command_families_reset_controls_and_stream_end() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 4U);
    Cdrom device(source.get(), data_layout());

    issue(device, Command::Test, {0x20U});
    const std::array<std::uint8_t, 4U> expected_version{0x95U, 0x05U, 0x16U, 0xc1U};
    require_reason(device, Interrupt::Acknowledge, "version test uses INT3");
    for (const std::uint8_t expected : expected_version) {
        require(device.pop_response() == expected, "version test response byte");
    }
    acknowledge(device);

    issue(device, Command::Setfilter, {3U, 7U});
    acknowledge_one_byte(device, 0U);
    issue(device, Command::Setmode, {0x88U});
    acknowledge_one_byte(device, 0U);
    issue(device, Command::Getparam);
    const std::array<std::uint8_t, 5U> expected_parameters{0U, 0x88U, 0U, 3U, 7U};
    for (const std::uint8_t expected : expected_parameters) {
        require(device.pop_response() == expected, "Getparam response byte");
    }
    acknowledge(device);

    set_location(device, 1U);
    issue(device, Command::SeekL);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "SeekL completion INT2");
    require(device.current_lba() == 1U, "SeekL uses prior Setloc target");
    (void)device.pop_response();
    acknowledge(device);

    issue(device, Command::SetSession, {1U});
    require_reason(device, Interrupt::Acknowledge, "session first response INT3");
    require(device.pop_response() == 0x42U, "session first status seeks with motor on");
    acknowledge(device);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "session completion INT2");
    acknowledge_one_byte(device, 0x02U);

    issue(device, Command::ReadToc);
    require_reason(device, Interrupt::Acknowledge, "ReadTOC first response INT3");
    acknowledge_one_byte(device, 0x02U);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "ReadTOC completion INT2");
    acknowledge_one_byte(device, 0x02U);

    issue(device, Command::Init);
    require_reason(device, Interrupt::None,
                   "Init withholds INT3 during controller initialization");
    require(device.response_count() == 0U,
            "Init exposes no response bytes before its late INT3");
    require(!device.enqueue_command(static_cast<std::uint8_t>(Command::Nop)),
            "Init rejects a colliding command while first response is pending");
    device.advance_to(device.current_cycle() +
                      (InitFirstResponseCycles - CommandLatencyCycles - 1U));
    require_reason(device, Interrupt::None,
                   "Init INT3 remains absent one cycle before deadline");
    device.advance_to(device.current_cycle() + 1U);
    require_reason(device, Interrupt::Acknowledge, "Init first response INT3");
    require(device.mode() == 0x20U && device.pop_response() == 0x02U,
            "Init sets whole-sector mode and motor state");
    acknowledge(device);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "Init completion INT2");
    acknowledge_one_byte(device, 0x02U);

    Cdrom init_collision(source.get(), data_layout());
    issue(init_collision, Command::Init);
    init_collision.advance_to(init_collision.current_cycle() +
                              (InitFirstResponseCycles -
                               CommandLatencyCycles));
    require_reason(init_collision, Interrupt::Acknowledge,
                   "late Init INT3 becomes active at its exact deadline");
    init_collision.advance_to(init_collision.current_cycle() +
                              SeekLatencyCycles);
    require_reason(init_collision, Interrupt::Acknowledge,
                   "unacknowledged Init INT3 retains IRQ ownership");
    require(init_collision.pending_interrupt_count() == 2U,
            "Init INT2 queues behind an unacknowledged INT3");
    acknowledge_one_byte(init_collision, 0x02U);
    require_reason(init_collision, Interrupt::Complete,
                   "acknowledging Init INT3 exposes queued INT2");
    acknowledge_one_byte(init_collision, 0x02U);
    require(init_collision.current_cycle() ==
                CommandLatencyCycles +
                    (InitFirstResponseCycles - CommandLatencyCycles) +
                    SeekLatencyCycles &&
                init_collision.mode() == device.mode() &&
                init_collision.status() == device.status(),
            "chunked Init IRQ replay reaches the same exact drive state");

    issue(device, Command::Pause);
    require_reason(device, Interrupt::Acknowledge, "Pause first response INT3");
    acknowledge_one_byte(device, 0x02U);
    device.advance_to(device.current_cycle() + CommandLatencyCycles);
    require_reason(device, Interrupt::Complete, "already-paused Pause completion INT2");
    acknowledge_one_byte(device, 0x02U);

    issue(device, Command::Stop);
    require_reason(device, Interrupt::Acknowledge, "Stop first response INT3");
    acknowledge_one_byte(device, 0x02U);
    device.advance_to(device.current_cycle() + SeekLatencyCycles);
    require_reason(device, Interrupt::Complete, "Stop completion INT2");
    require(device.current_lba() == 0U, "Stop returns head to first track");
    acknowledge_one_byte(device, 0U);

    issue(device, Command::Mute);
    acknowledge_one_byte(device, 0U);
    require(device.muted(), "Mute command state");
    const clean_aot::cdrom::Cycle reset_command_cycle = device.current_cycle();
    issue(device, Command::Reset, {0xaaU, 0x55U});
    require(device.current_cycle() == reset_command_cycle + CommandLatencyCycles &&
                device.mode() == 0U && !device.muted() && device.status() == 0U,
            "Reset accepts extra parameters and resets drive state without rewinding time");
    acknowledge_one_byte(device, 0U);

    require(device.enqueue_parameter(1U) && device.enqueue_parameter(2U),
            "queue parameters before host clear");
    device.write_register(0U, 1U);
    device.write_register(3U, 0x40U);
    require(device.parameter_count() == 0U, "HCLRCTL clears parameter FIFO");
    issue(device, Command::Setmode, {0x80U});
    acknowledge_one_byte(device, 0U);
    DmaLog reset_dma;
    device.attach_dma(&reset_dma);
    device.set_dma_enabled(true);
    const clean_aot::cdrom::Cycle decoder_reset_cycle = device.current_cycle();
    device.write_register(0U, 1U);
    device.write_register(3U, 0x80U);
    require(device.current_cycle() == decoder_reset_cycle && device.mode() == 0U &&
                device.selected_register_bank() == 0U && device.interrupt_flags() == 0U,
            "decoder reset clears host state and preserves timeline");
    issue(device, Command::ReadN);
    (void)device.pop_response();
    acknowledge(device);
    device.advance_to(device.current_cycle() + SingleSpeedSectorCycles);
    (void)device.pop_response();
    request_data(device);
    require(reset_dma.count == 1U && reset_dma.lengths[0U] == DataOnlyBytes,
            "decoder reset preserves the external DMA integration binding");
    acknowledge(device);
    device.reset();

    auto mixed_source = std::make_unique<MemorySource>();
    populate_mixed(*mixed_source);
    Cdrom navigation(mixed_source.get(), mixed_layout());
    issue(navigation, Command::Play, {0x02U});
    (void)navigation.pop_response();
    acknowledge(navigation);
    navigation.advance_to(navigation.current_cycle() + SingleSpeedSectorCycles);
    require(navigation.current_lba() == 3U, "Play advances before navigation commands");
    issue(navigation, Command::Forward);
    require_reason(navigation, Interrupt::Acknowledge, "Forward response INT3");
    require(navigation.current_lba() == mixed_layout().lead_out_lba,
            "Forward performs bounded coarse movement");
    (void)navigation.pop_response();
    acknowledge(navigation);
    issue(navigation, Command::Backward);
    require_reason(navigation, Interrupt::Acknowledge, "Backward response INT3");
    require(navigation.current_lba() == 0U, "Backward movement is bounded at disc start");
    (void)navigation.pop_response();
    acknowledge(navigation);

    auto one_sector_source = std::make_unique<MemorySource>();
    populate_mode_one(*one_sector_source, 0U, 1U);
    Cdrom stream_end(one_sector_source.get(), data_layout(1U));
    issue(stream_end, Command::ReadN);
    (void)stream_end.pop_response();
    acknowledge(stream_end);
    stream_end.advance_to(stream_end.current_cycle() + SingleSpeedSectorCycles);
    (void)stream_end.pop_response();
    acknowledge(stream_end);
    stream_end.advance_to(stream_end.current_cycle() + SingleSpeedSectorCycles);
    require_reason(stream_end, Interrupt::DataEnd, "read lead-out produces INT4");
    require(!stream_end.reading() && stream_end.pop_response() == 0x02U,
            "read lead-out keeps motor on and clears read state");

    Cdrom audio_read(mixed_source.get(), mixed_layout());
    set_location(audio_read, 2U);
    issue(audio_read, Command::ReadN);
    (void)audio_read.pop_response();
    acknowledge(audio_read);
    audio_read.advance_to(audio_read.current_cycle() + SingleSpeedSectorCycles);
    require_reason(audio_read, Interrupt::Error, "audio-track read requires CDDA mode");
    require(audio_read.pop_response() == 0x03U &&
                audio_read.pop_response() ==
                    static_cast<std::uint8_t>(CommandError::InvalidCommand),
            "audio-track read gating response");
    acknowledge(audio_read);

    audio_read.reset();
    issue(audio_read, Command::Setmode, {0x01U});
    acknowledge_one_byte(audio_read, 0U);
    set_location(audio_read, 2U);
    issue(audio_read, Command::ReadN);
    (void)audio_read.pop_response();
    acknowledge(audio_read);
    audio_read.advance_to(audio_read.current_cycle() + SingleSpeedSectorCycles);
    require_reason(audio_read, Interrupt::DataReady, "CDDA-enabled audio read produces INT1");
    (void)audio_read.pop_response();
    request_data(audio_read);
    require(audio_read.data_count() == DataOnlyBytes && audio_read.pop_data() == 0x00U &&
                audio_read.pop_data() == 0x80U,
            "CDDA-enabled audio read exposes raw little-endian bytes");
}

void test_overflow_time_reset_scheduler_and_deterministic_chunking() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 12U);
    Cdrom overflow(source.get(), data_layout());
    issue(overflow, Command::ReadN);
    (void)overflow.pop_response();
    acknowledge(overflow);
    overflow.advance_to(overflow.current_cycle() + SingleSpeedSectorCycles * 10U);
    require(!overflow.reading() && overflow.last_error() == ErrorCode::DataOverflow &&
                overflow.pending_interrupt_count() == Cdrom::SectorBufferCapacity,
            "unserviced eight-sector buffer stops streaming before payload overwrite");

    Cdrom time_guard;
    time_guard.advance_to(123456U);
    time_guard.reset();
    require(time_guard.current_cycle() == 123456U, "device reset preserves machine timeline");
    time_guard.advance_to(123455U);
    require(time_guard.current_cycle() == 123456U &&
                time_guard.last_error() == ErrorCode::TimeReversal,
            "time reversal is rejected without moving the clock");
    time_guard.advance_to(std::numeric_limits<clean_aot::cdrom::Cycle>::max());
    require(!time_guard.enqueue_command(static_cast<std::uint8_t>(Command::Nop)) &&
                time_guard.last_error() == ErrorCode::SchedulerOverflow &&
                time_guard.command_count() == 0U,
            "command scheduling overflow is atomic");

    SchedulerLog first_scheduler;
    SchedulerLog second_scheduler;
    Cdrom scheduled;
    require(scheduled.enqueue_command(static_cast<std::uint8_t>(Command::Nop)),
            "schedule command before attaching machine scheduler");
    scheduled.attach_scheduler(&first_scheduler);
    require(first_scheduler.count == 1U &&
                first_scheduler.receipts[0U].event == ScheduledEvent::CommandStart,
            "late scheduler attachment receives outstanding event");
    scheduled.attach_scheduler(&second_scheduler);
    require(first_scheduler.cancels == 4U && second_scheduler.count == 1U,
            "scheduler replacement cancels old ownership and republishes event");
    scheduled.advance_to(CommandLatencyCycles);
    scheduled.reset();
    require(second_scheduler.cancels >= 4U, "reset cancels scheduler-visible events");

    auto replacement_source = std::make_unique<MemorySource>();
    Cdrom media_guard(source.get(), data_layout());
    issue(media_guard, Command::Nop);
    require(!media_guard.set_disc_layout(mixed_layout()) &&
                media_guard.last_error() == ErrorCode::CommandBusy,
            "layout replacement is rejected while a response is outstanding");
    media_guard.attach_source(replacement_source.get());
    require(media_guard.last_error() == ErrorCode::CommandBusy,
            "source replacement is rejected while a response is outstanding");
    (void)media_guard.pop_response();
    acknowledge(media_guard);
    require(media_guard.set_disc_layout(mixed_layout()),
            "layout replacement succeeds once the device is idle");

    auto first_source = std::make_unique<MemorySource>();
    auto second_source = std::make_unique<MemorySource>();
    populate_mode_one(*first_source, 0U, 2U);
    populate_mode_one(*second_source, 0U, 2U);
    Cdrom first(first_source.get(), data_layout());
    Cdrom second(second_source.get(), data_layout());
    issue(first, Command::ReadN);
    issue(second, Command::ReadN);
    (void)first.pop_response();
    (void)second.pop_response();
    acknowledge(first);
    acknowledge(second);
    const clean_aot::cdrom::Cycle horizon =
        first.current_cycle() + SingleSpeedSectorCycles;
    first.advance_to(horizon);
    second.advance_to(horizon - 17U);
    second.advance_to(horizon - 1U);
    second.advance_to(horizon);
    require(first.interrupt_flags() == second.interrupt_flags() &&
                first.current_lba() == second.current_lba() &&
                first_source->read_count == second_source->read_count,
            "advance chunking does not alter event state");
    (void)first.pop_response();
    (void)second.pop_response();
    request_data(first);
    request_data(second);
    std::array<std::uint8_t, 32U> first_bytes{};
    std::array<std::uint8_t, 32U> second_bytes{};
    for (std::size_t index = 0U; index < first_bytes.size(); ++index) {
        first_bytes[index] = first.pop_data();
        second_bytes[index] = second.pop_data();
    }
    require(first_bytes == second_bytes, "advance chunking preserves transfer bytes");
}

void test_runtime_lid_transition_stops_media_and_blocks_disc_commands() {
    auto source = std::make_unique<MemorySource>();
    populate_mode_one(*source, 0U, 4U);
    Cdrom device(source.get(), data_layout());

    issue(device, Command::ReadN);
    (void)device.pop_response();
    acknowledge(device);
    require(device.reading(), "read stream starts before lid transition");
    device.set_shell_open(true);
    require(device.shell_open() && !device.reading() && !device.playing() &&
                device.data_count() == 0U,
            "opening lid atomically stops streams and invalidates transfer data");

    issue(device, Command::Nop);
    require_reason(device, Interrupt::Acknowledge, "Getstat remains available with lid open");
    require((device.pop_response() & status_bit(Status::ShellOpen)) != 0U,
            "Getstat reports ShellOpen");
    acknowledge(device);

    issue(device, Command::ID);
    require_reason(device, Interrupt::Error, "GetID rejects open-lid media");
    require((device.pop_response() & status_bit(Status::ShellOpen)) != 0U,
            "open-lid error retains ShellOpen status");
    acknowledge(device);

    device.set_shell_open(false);
    require(!device.shell_open(), "closing lid clears ShellOpen");
    issue(device, Command::ID);
    require_reason(device, Interrupt::Acknowledge, "GetID resumes after lid closes");
}

} // namespace

int main() {
    test_timing_bcd_and_msf_boundaries();
    test_layout_validation_and_boundaries();
    test_raw_sector_contracts();
    test_host_registers_interrupt_reasons_and_latched_audio();
    test_parameter_ownership_errors_and_second_response_queue();
    test_seek_status_queries_and_identity_packets();
    test_wipeout_real_layout_toc_and_pregap_setloc();
    test_data_request_widths_sector_sizes_and_location_header();
    test_dma_is_request_gated_and_cadence_is_exact();
    test_delayed_100hz_service_preserves_sector_irq_dma_order();
    test_cdda_tracks_autopause_matrix_mute_and_100hz_relation();
    test_wipeout_cdda_report_irq_and_exact_cadence();
    test_xa_routing_filter_and_explicit_decode_boundary();
    test_remaining_command_families_reset_controls_and_stream_end();
    test_overflow_time_reset_scheduler_and_deterministic_chunking();
    test_runtime_lid_transition_stops_media_and_blocks_disc_commands();
    std::cout << "clean_aot_cdrom_tests: ok\n";
    return EXIT_SUCCESS;
}
