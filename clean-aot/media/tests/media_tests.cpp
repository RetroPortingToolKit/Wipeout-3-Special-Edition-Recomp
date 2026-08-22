#include "clean_aot/media/media.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

using clean_aot::media::CddaFramesPerSector;
using clean_aot::media::CueImage;
using clean_aot::media::ErrorCode;
using clean_aot::media::Mode2Form;
using clean_aot::media::Mode2FormOnePayloadBytes;
using clean_aot::media::Mode2FormTwoPayloadBytes;
using clean_aot::media::RawSector;
using clean_aot::media::StereoSample;
using clean_aot::media::TrackType;
using clean_aot::media::XaAdpcmDecoder;
using clean_aot::media::XaBitsPerSample;
using clean_aot::media::XaFormat;
using clean_aot::media::XaRateHalfHz;
using clean_aot::media::XaRateFullHz;
using clean_aot::media::XaRateConverter;
using clean_aot::media::XaSector;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct TemporaryDirectory final {
    std::filesystem::path path{};

    TemporaryDirectory() {
        const std::uint64_t stamp = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        path = std::filesystem::temp_directory_path() /
               ("clean_aot_media_" + std::to_string(stamp));
        std::error_code error;
        std::filesystem::create_directories(path, error);
        require(!error, "temporary directory creation");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "open temporary binary");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "write temporary binary");
}

void write_sectors(const std::filesystem::path& path, const std::vector<RawSector>& sectors) {
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "open temporary sector file");
    for (const RawSector& sector : sectors) {
        output.write(reinterpret_cast<const char*>(sector.bytes.data()),
                     static_cast<std::streamsize>(sector.bytes.size()));
    }
    require(static_cast<bool>(output), "write temporary sector file");
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "open temporary CUE");
    output << text;
    require(static_cast<bool>(output), "write temporary CUE");
}

void write_le_u32(std::array<std::uint8_t, clean_aot::media::RawSectorBytes>& bytes,
                  std::size_t offset,
                  std::uint32_t value) noexcept {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

void set_raw_header(RawSector& sector, std::uint64_t lba, std::uint8_t mode) {
    sector.bytes.fill(0U);
    sector.bytes[0U] = 0U;
    std::fill(sector.bytes.begin() + 1, sector.bytes.begin() + 11, 0xffU);
    sector.bytes[11U] = 0U;
    const auto msf = CueImage::lba_to_msf(lba);
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;
    require(CueImage::encode_bcd(msf.minute, minute) && CueImage::encode_bcd(msf.second, second) &&
                CueImage::encode_bcd(msf.frame, frame),
            "encode raw sector MSF");
    sector.bytes[12U] = minute;
    sector.bytes[13U] = second;
    sector.bytes[14U] = frame;
    sector.bytes[15U] = mode;
}

RawSector mode2_sector(std::uint64_t lba,
                       bool form2,
                       std::uint8_t seed,
                       std::uint8_t coding = 0U,
                       std::uint8_t submode_extra = 0U) {
    RawSector sector{};
    set_raw_header(sector, lba, 2U);
    const std::uint8_t submode = static_cast<std::uint8_t>((form2 ? 0x20U : 0U) | submode_extra);
    sector.bytes[16U] = 1U;
    sector.bytes[17U] = 2U;
    sector.bytes[18U] = submode;
    sector.bytes[19U] = coding;
    std::copy_n(sector.bytes.begin() + 16, 4U, sector.bytes.begin() + 20);
    const std::size_t payload_size = form2 ? Mode2FormTwoPayloadBytes : Mode2FormOnePayloadBytes;
    for (std::size_t index = 0U; index < payload_size; ++index) {
        sector.bytes[24U + index] =
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index));
    }
    const std::size_t edc_offset = form2 ? 2348U : 2072U;
    const std::size_t edc_length = edc_offset - 16U;
    const std::uint32_t edc = CueImage::compute_edc(
        std::span<const std::uint8_t>(sector.bytes.data() + 16U, edc_length));
    write_le_u32(sector.bytes, edc_offset, edc);
    return sector;
}

RawSector cdda_sector(std::int16_t left, std::int16_t right) {
    RawSector sector{};
    const std::uint16_t left_bits = static_cast<std::uint16_t>(left);
    const std::uint16_t right_bits = static_cast<std::uint16_t>(right);
    for (std::size_t frame = 0U; frame < CddaFramesPerSector; ++frame) {
        const std::size_t offset = frame * 4U;
        sector.bytes[offset] = static_cast<std::uint8_t>(left_bits & 0xffU);
        sector.bytes[offset + 1U] = static_cast<std::uint8_t>(left_bits >> 8U);
        sector.bytes[offset + 2U] = static_cast<std::uint8_t>(right_bits & 0xffU);
        sector.bytes[offset + 3U] = static_cast<std::uint8_t>(right_bits >> 8U);
    }
    return sector;
}

XaSector xa_vector(bool stereo, XaBitsPerSample bits, std::uint32_t rate) {
    XaSector sector{};
    sector.metadata.audio = true;
    sector.metadata.form_two = true;
    sector.format = XaFormat{stereo, rate, bits, false};
    for (std::size_t group = 0U; group < 18U; ++group) {
        std::uint8_t* chunk = sector.adpcm.data() + group * 128U;
        const std::size_t block_count = bits == XaBitsPerSample::Eight ? 4U : 8U;
        for (std::size_t block = 0U; block < block_count; ++block) {
            chunk[4U + block] = bits == XaBitsPerSample::Eight ? 0x08U : 0x0cU;
        }
        for (std::size_t index = 0U; index < 4U; ++index) {
            chunk[index] = chunk[4U + index];
            chunk[12U + index] = chunk[8U + index];
        }
        if (group == 0U) {
            if (bits == XaBitsPerSample::Four) {
                chunk[16U] = 0xf1U; // left nibble 1, right nibble -1.
            } else {
                chunk[16U] = 0x01U;
                chunk[17U] = 0xffU;
            }
        }
    }
    return sector;
}

void test_cue_layout_and_random_reads() {
    TemporaryDirectory temporary;
    const auto track1_path = temporary.path / "track01.bin";
    const auto track2_path = temporary.path / "track02.bin";
    const auto track3_path = temporary.path / "track03.bin";
    write_sectors(track1_path, {mode2_sector(0U, false, 0x10U), mode2_sector(1U, false, 0x20U)});
    write_sectors(track2_path,
                  {cdda_sector(1000, -1000), cdda_sector(2000, -2000), cdda_sector(3000, -3000)});
    write_sectors(track3_path,
                  {mode2_sector(8U, false, 0x30U), mode2_sector(9U, false, 0x40U),
                   mode2_sector(10U, false, 0x50U)});
    const auto cue_path = temporary.path / "disc.cue";
    write_text(cue_path,
               "REM clean-room fixture\n"
               "FILE \"track01.bin\" BINARY\n"
               "  TRACK 01 MODE2/2352\n"
               "    INDEX 01 00:00:00\n"
               "FILE \"track02.bin\" BINARY\n"
               "  TRACK 02 AUDIO\n"
               "    PREGAP 00:00:02\n"
               "    INDEX 01 00:00:00\n"
               "    POSTGAP 00:00:01\n"
               "FILE \"track03.bin\" BINARY\n"
               "  TRACK 03 MODE2/2352\n"
               "    INDEX 00 00:00:00\n"
               "    INDEX 01 00:00:01\n");

    auto image_result = CueImage::open(cue_path);
    require(image_result.has_value(), "multi-file CUE opens");
    const CueImage& image = image_result.value();
    require(image.disc().lead_out_lba == 11U, "lead-out includes stored sectors and POSTGAP");
    require(image.tracks().size() == 3U, "three tracks parsed");
    require(image.tracks()[0U].logical_start_lba == 0U &&
                image.tracks()[0U].logical_end_lba == 2U,
            "track 01 boundary");
    require(image.tracks()[1U].logical_start_lba == 2U &&
                image.tracks()[1U].stored_start_lba == 4U &&
                image.tracks()[1U].stored_end_lba == 7U &&
                image.tracks()[1U].logical_end_lba == 8U,
            "PREGAP and POSTGAP mapping");
    require(image.tracks()[2U].has_index00 && image.tracks()[2U].logical_start_lba == 8U &&
                image.tracks()[2U].index01_lba == 9U,
            "stored INDEX 00 and INDEX 01 mapping");

    auto pregap_result = image.read_raw_sector(2U);
    require(pregap_result && pregap_result.value().bytes[0U] == 0U &&
                pregap_result.value().bytes[2340U] == 0U,
            "synthetic PREGAP is bounded zero data");
    auto audio_result = image.read_cdda_sector(4U);
    require(audio_result && audio_result.value().frames[0U] == StereoSample{1000, -1000},
            "CDDA random read uses Redump little-endian PCM");
    auto mode2_result = image.read_mode2_sector(9U);
    require(mode2_result && mode2_result.value().payload_size == Mode2FormOnePayloadBytes &&
                mode2_result.value().payload[0U] == 0x40U,
            "Mode2 payload is exposed at INDEX 01");
    auto gap_mode2_result = image.read_mode2_sector(2U);
    require(!gap_mode2_result && gap_mode2_result.error().code == ErrorCode::SyntheticSector,
            "synthetic gap is not misreported as Mode2");
}

void test_same_file_and_rejection_boundaries() {
    TemporaryDirectory temporary;
    const auto full_path = temporary.path / "full.bin";
    write_sectors(full_path,
                  {mode2_sector(0U, false, 1U), mode2_sector(1U, false, 2U),
                   cdda_sector(11, -11), cdda_sector(22, -22), cdda_sector(33, -33)});
    const auto same_file_cue = temporary.path / "same.cue";
    write_text(same_file_cue,
               "FILE \"full.bin\" BINARY\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 01 00:00:00\n"
               "TRACK 02 AUDIO\n"
               "INDEX 00 00:00:02\n"
               "INDEX 01 00:00:03\n");
    auto same_file_result = CueImage::open(same_file_cue);
    require(same_file_result && same_file_result.value().disc().lead_out_lba == 5U,
            "tracks sharing one BINARY FILE map by relative INDEX offsets");
    require(same_file_result.value().read_cdda_sector(2U).value().frames[0U] ==
                StereoSample{11, -11},
            "same-file INDEX 00 raw sector read");

    const auto gapped_path = temporary.path / "gapped.bin";
    write_sectors(gapped_path,
                  {mode2_sector(0U, false, 0x60U), mode2_sector(1U, false, 0x70U),
                   cdda_sector(101, -101), cdda_sector(202, -202)});
    const auto same_file_gaps_cue = temporary.path / "same_file_gaps.cue";
    write_text(same_file_gaps_cue,
               "FILE \"gapped.bin\" BINARY\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 01 00:00:00\n"
               "POSTGAP 00:00:01\n"
               "TRACK 02 AUDIO\n"
               "PREGAP 00:00:01\n"
               "INDEX 01 00:00:02\n");
    auto same_file_gaps_result = CueImage::open(same_file_gaps_cue);
    require(same_file_gaps_result &&
                same_file_gaps_result.value().disc().lead_out_lba == 6U &&
                same_file_gaps_result.value().tracks()[0U].logical_end_lba == 3U &&
                same_file_gaps_result.value().tracks()[0U].has_postgap &&
                same_file_gaps_result.value().tracks()[1U].logical_start_lba == 3U &&
                same_file_gaps_result.value().tracks()[1U].stored_start_lba == 4U &&
                same_file_gaps_result.value().tracks()[1U].index01_lba == 4U &&
                same_file_gaps_result.value().tracks()[1U].has_pregap,
            "same-file synthetic POSTGAP and PREGAP preserve logical boundaries");
    require(same_file_gaps_result.value().read_raw_sector(2U).value().bytes[0U] == 0U &&
                same_file_gaps_result.value().read_raw_sector(3U).value().bytes[0U] == 0U &&
                same_file_gaps_result.value().read_cdda_sector(4U).value().frames[0U] ==
                    StereoSample{101, -101},
            "same-file synthetic gaps do not consume or duplicate stored sectors");

    const auto traversal_cue = temporary.path / "traversal.cue";
    write_text(traversal_cue,
               "FILE \"../outside.bin\" BINARY\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 01 00:00:00\n");
    auto traversal_result = CueImage::open(traversal_cue);
    require(!traversal_result && traversal_result.error().code == ErrorCode::InvalidPath,
            "FILE traversal is rejected before filesystem access");

    const auto unsupported_cue = temporary.path / "unsupported.cue";
    write_text(unsupported_cue,
               "FILE \"full.bin\" WAVE\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 01 00:00:00\n");
    auto unsupported_result = CueImage::open(unsupported_cue);
    require(!unsupported_result &&
                unsupported_result.error().code == ErrorCode::UnsupportedFileType,
            "non-BINARY FILE is rejected");

    const auto malformed_cue = temporary.path / "malformed.cue";
    write_text(malformed_cue,
               "FILE \"full.bin\" BINARY\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 02 00:00:00\n"
               "INDEX 01 00:00:00\n");
    auto malformed_result = CueImage::open(malformed_cue);
    require(!malformed_result && malformed_result.error().code == ErrorCode::CueSyntax,
            "unsupported INDEX number is rejected");

    const auto truncated_path = temporary.path / "truncated.bin";
    const std::array<std::uint8_t, 2353U> truncated{};
    write_bytes(truncated_path, truncated);
    const auto truncated_cue = temporary.path / "truncated.cue";
    write_text(truncated_cue,
               "FILE \"truncated.bin\" BINARY\n"
               "TRACK 01 MODE2/2352\n"
               "INDEX 01 00:00:00\n");
    auto truncated_result = CueImage::open(truncated_cue);
    require(!truncated_result && truncated_result.error().code == ErrorCode::FileSize,
            "truncated raw image is rejected at open");
}

void test_mode2_and_xa_exposure() {
    require(CueImage::lba_to_msf(0U) == clean_aot::media::Msf{0U, 2U, 0U} &&
                CueImage::lba_to_msf(74U) == clean_aot::media::Msf{0U, 2U, 74U} &&
                CueImage::lba_to_msf(75U) == clean_aot::media::Msf{0U, 3U, 0U} &&
                CueImage::lba_to_msf(449849U) == clean_aot::media::Msf{99U, 59U, 74U},
            "MSF/LBA boundaries use the 150-frame lead-in exactly");

    const RawSector form1 = mode2_sector(0U, false, 0x12U);
    auto form1_result = CueImage::decode_mode2(
        0U, std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(form1.bytes));
    require(form1_result && form1_result.value().form == Mode2Form::Form1 &&
                form1_result.value().payload_size == Mode2FormOnePayloadBytes &&
                form1_result.value().payload[0U] == 0x12U,
            "Mode2 Form1 payload and subheader exposure");

    const RawSector form2 = mode2_sector(1U, true, 0x44U, 0x01U, 0x04U);
    auto form2_result = CueImage::decode_mode2(
        1U, std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(form2.bytes));
    require(form2_result && form2_result.value().form == Mode2Form::Form2 &&
                form2_result.value().payload_size == Mode2FormTwoPayloadBytes &&
                form2_result.value().xa.audio && form2_result.value().xa.form_two,
            "Mode2 Form2 exact payload and XA flags");
    auto xa_result = CueImage::extract_xa(form2_result.value());
    require(xa_result && xa_result.value().format.stereo &&
                xa_result.value().format.sample_rate_hz == XaRateFullHz &&
                xa_result.value().adpcm[0U] == 0x44U,
            "XA sector extraction uses coding-info fields");
    RawSector form2_without_edc = form2;
    form2_without_edc.bytes[2348U] = 0U;
    form2_without_edc.bytes[2349U] = 0U;
    form2_without_edc.bytes[2350U] = 0U;
    form2_without_edc.bytes[2351U] = 0U;
    auto optional_edc_result = CueImage::decode_mode2(
        1U, std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(
                form2_without_edc.bytes));
    require(optional_edc_result.has_value(), "Mode2 Form2 optional zero EDC is accepted");

    RawSector corrupted = form1;
    corrupted.bytes[20U] ^= 1U;
    auto bad_subheader = CueImage::decode_mode2(
        0U, std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(corrupted.bytes));
    require(!bad_subheader && bad_subheader.error().code == ErrorCode::MalformedSector,
            "Mode2 duplicate subheader mismatch fails closed");
    corrupted = form1;
    corrupted.bytes[2072U] ^= 1U;
    auto bad_edc = CueImage::decode_mode2(
        0U, std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(corrupted.bytes));
    require(!bad_edc && bad_edc.error().code == ErrorCode::InvalidEdc,
            "Mode2 EDC mismatch fails closed");
}

void test_cdda_and_xa_vectors() {
    const RawSector raw_audio = cdda_sector(std::numeric_limits<std::int16_t>::min(),
                                            std::numeric_limits<std::int16_t>::max());
    auto cdda_result = CueImage::decode_cdda(
        std::span<const std::uint8_t, clean_aot::media::RawSectorBytes>(raw_audio.bytes));
    require(cdda_result &&
                cdda_result.value().frames[0U] ==
                    StereoSample{std::numeric_limits<std::int16_t>::min(),
                                 std::numeric_limits<std::int16_t>::max()},
            "CDDA signed little-endian endpoint vector");

    XaAdpcmDecoder decoder;
    XaSector four_bit = xa_vector(true, XaBitsPerSample::Four, XaRateFullHz);
    std::array<StereoSample, 2016U> decoded{};
    auto decoded_result = decoder.decode(four_bit, decoded);
    require(decoded_result && decoded_result.value() == decoded.size() &&
                decoded[0U] == StereoSample{1, -1} && decoded[1U] == StereoSample{},
            "XA 4-bit stereo nibble ordering and exact sample count");

    XaSector eight_bit = xa_vector(false, XaBitsPerSample::Eight, XaRateHalfHz);
    std::array<StereoSample, 2016U> decoded_eight{};
    auto eight_result = decoder.decode(eight_bit, decoded_eight);
    require(eight_result && eight_result.value() == decoded_eight.size() &&
                decoded_eight[0U] == StereoSample{1, 1},
            "XA 8-bit mono byte ordering and exact sample count");

    XaSector bad_header = four_bit;
    bad_header.adpcm[0U] ^= 1U;
    auto bad_result = decoder.decode(bad_header, decoded);
    require(!bad_result && bad_result.error().code == ErrorCode::InvalidXaHeader,
            "XA redundant header mismatch fails closed");
}

void test_exact_rate_converter() {
    std::array<StereoSample, 6U> input{};
    for (std::size_t index = 0U; index < input.size(); ++index) {
        const auto value = static_cast<std::int16_t>(index * 1000U);
        input[index] = StereoSample{value, static_cast<std::int16_t>(-value)};
    }
    std::array<StereoSample, 8U> one_shot_output{};
    auto one_shot_result = XaRateConverter::create(XaRateFullHz);
    require(one_shot_result.has_value(), "37800 Hz converter creation");
    auto one_shot = std::move(one_shot_result).value();
    auto produced = one_shot.push(input, one_shot_output, true);
    require(produced && produced.value() == 7U && one_shot.output_frames() == 7U,
            "37800 to 44100 exact seven-sixths count");
    require(one_shot_output[0U] == input[0U] && one_shot_output[1U].left == 857,
            "deterministic rational linear interpolation");
    require(!one_shot.push(input, one_shot_output, false) &&
                one_shot.push(input, one_shot_output, false).error().code ==
                    ErrorCode::ConverterFinished,
            "finished converter rejects further input");

    std::array<StereoSample, 8U> chunked_output{};
    auto chunked_result = XaRateConverter::create(XaRateFullHz);
    require(chunked_result.has_value(), "chunked converter creation");
    auto chunked = std::move(chunked_result).value();
    auto first = chunked.push(std::span<const StereoSample>(input.data(), 3U), chunked_output);
    require(first.has_value(), "first chunk conversion");
    auto second = chunked.push(std::span<const StereoSample>(input.data() + 3U, 3U),
                               std::span<StereoSample>(chunked_output.data() + first.value(),
                                                       chunked_output.size() - first.value()),
                               true);
    require(first && second && first.value() + second.value() == 7U &&
                std::equal(one_shot_output.begin(), one_shot_output.begin() + 7U,
                           chunked_output.begin()),
            "converter carry is chunk-boundary independent");

    auto half_result = XaRateConverter::create(XaRateHalfHz);
    require(half_result.has_value(), "18900 Hz converter creation");
    auto half = std::move(half_result).value();
    std::array<StereoSample, 9U> half_output{};
    auto half_produced = half.push(std::span<const StereoSample>(input.data(), 3U), half_output, true);
    require(half_produced && half_produced.value() == 7U,
            "18900 to 44100 exact seven-thirds count");

    const std::vector<StereoSample> full_sector_input(2016U, StereoSample{123, -123});
    std::vector<StereoSample> full_sector_output(2352U);
    auto sector_converter_result = XaRateConverter::create(XaRateFullHz);
    require(sector_converter_result.has_value(), "full XA sector converter creation");
    auto sector_converter = std::move(sector_converter_result).value();
    auto sector_produced = sector_converter.push(full_sector_input, full_sector_output, true);
    require(sector_produced && sector_produced.value() == 2352U,
            "one 37800 Hz stereo XA sector converts to exactly 2352 frames");

    const std::vector<StereoSample> half_sector_input(2016U, StereoSample{321, -321});
    std::vector<StereoSample> half_sector_output(4704U);
    auto half_sector_converter_result = XaRateConverter::create(XaRateHalfHz);
    require(half_sector_converter_result.has_value(), "half-rate XA sector converter creation");
    auto half_sector_converter = std::move(half_sector_converter_result).value();
    auto half_sector_produced =
        half_sector_converter.push(half_sector_input, half_sector_output, true);
    require(half_sector_produced && half_sector_produced.value() == 4704U,
            "one 18900 Hz stereo XA sector converts to exactly 4704 frames");

    auto invalid_rate = XaRateConverter::create(44100U);
    require(!invalid_rate && invalid_rate.error().code == ErrorCode::InvalidRate,
            "unsupported XA rate fails closed");
}

} // namespace

int main() {
    test_cue_layout_and_random_reads();
    test_same_file_and_rejection_boundaries();
    test_mode2_and_xa_exposure();
    test_cdda_and_xa_vectors();
    test_exact_rate_converter();
    std::cout << "clean_aot_media_tests: ok\n";
    return EXIT_SUCCESS;
}
