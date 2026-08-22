#ifndef CLEAN_AOT_MEDIA_HPP
#define CLEAN_AOT_MEDIA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace clean_aot::media {

inline constexpr std::size_t RawSectorBytes = 2352U;
inline constexpr std::size_t Mode2FormOnePayloadBytes = 2048U;
inline constexpr std::size_t Mode2FormTwoPayloadBytes = 2324U;
inline constexpr std::size_t Mode1PayloadBytes = 2048U;
inline constexpr std::size_t XaAdpcmBytes = 2304U;
inline constexpr std::size_t CddaFramesPerSector = 588U;
inline constexpr std::uint32_t CddaRateHz = 44100U;
inline constexpr std::uint32_t XaRateFullHz = 37800U;
inline constexpr std::uint32_t XaRateHalfHz = 18900U;

using Lba = std::uint64_t;

enum class ErrorCode : std::uint8_t {
    None = 0U,
    OpenFailed,
    ReadFailed,
    CueTooLarge,
    CueSyntax,
    UnsupportedDirective,
    UnsupportedFileType,
    UnsupportedTrackType,
    InvalidTrackNumber,
    InvalidMsf,
    InvalidPath,
    FileMissing,
    FileSize,
    FileLayout,
    DiscLayout,
    LbaOutOfRange,
    SyntheticSector,
    WrongTrackType,
    MalformedSector,
    InvalidEdc,
    InvalidXaFormat,
    InvalidXaHeader,
    OutputTooSmall,
    InvalidRate,
    ConverterFinished,
    Overflow,
};

struct Error final {
    ErrorCode code = ErrorCode::None;
    std::size_t line = 0U;
    std::string detail{};
};

template <typename T>
class Result final {
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(Error error) { return Result(std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept {
        return std::holds_alternative<T>(state_);
    }

    [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(state_); }
    [[nodiscard]] T& value() & { return std::get<T>(state_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(state_)); }

    [[nodiscard]] const Error& error() const noexcept { return std::get<Error>(state_); }

private:
    explicit Result(T value) : state_(std::move(value)) {}
    explicit Result(Error error) : state_(std::move(error)) {}

    std::variant<T, Error> state_;
};

template <>
class Result<void> final {
public:
    static Result success() { return Result(true, Error{}); }
    static Result failure(Error error) { return Result(false, std::move(error)); }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
    [[nodiscard]] const Error& error() const noexcept { return error_; }

private:
    Result(bool ok, Error error) : ok_(ok), error_(std::move(error)) {}

    bool ok_ = false;
    Error error_{};
};

struct Msf final {
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;

    friend constexpr bool operator==(const Msf&, const Msf&) noexcept = default;
};

enum class TrackType : std::uint8_t {
    Mode1_2352,
    Mode2_2352,
    Audio,
};

struct TrackInfo final {
    std::uint8_t number = 0U;
    TrackType type = TrackType::Mode2_2352;
    std::uint32_t file_index = 0U;
    std::uint64_t file_sector_begin = 0U;
    std::uint64_t file_sector_end = 0U;
    bool has_index00 = false;
    Msf index00{};
    Msf index01{};
    bool has_pregap = false;
    std::uint32_t pregap_frames = 0U;
    bool has_postgap = false;
    std::uint32_t postgap_frames = 0U;
    Lba logical_start_lba = 0U;
    Lba stored_start_lba = 0U;
    Lba index01_lba = 0U;
    Lba stored_end_lba = 0U;
    Lba logical_end_lba = 0U;
};

struct DiscInfo final {
    Lba lead_out_lba = 0U;
    std::vector<TrackInfo> tracks{};
};

struct RawSector final {
    std::array<std::uint8_t, RawSectorBytes> bytes{};
};

struct StereoSample final {
    std::int16_t left = 0;
    std::int16_t right = 0;

    friend constexpr bool operator==(const StereoSample&, const StereoSample&) noexcept =
        default;
};

struct CddaSector final {
    std::array<StereoSample, CddaFramesPerSector> frames{};
};

struct Mode1Sector final {
    Lba lba = 0U;
    std::array<std::uint8_t, Mode1PayloadBytes> payload{};
    std::uint32_t edc = 0U;
};

struct XaMetadata final {
    std::uint8_t file_number = 0U;
    std::uint8_t channel_number = 0U;
    std::uint8_t submode = 0U;
    std::uint8_t coding_info = 0U;
    bool end_of_record = false;
    bool video = false;
    bool audio = false;
    bool data = false;
    bool trigger = false;
    bool form_two = false;
    bool real_time = false;
    bool end_of_file = false;
};

enum class Mode2Form : std::uint8_t {
    Form1,
    Form2,
};

struct Mode2Sector final {
    Lba lba = 0U;
    Mode2Form form = Mode2Form::Form1;
    std::array<std::uint8_t, 8U> subheader{};
    std::array<std::uint8_t, Mode2FormTwoPayloadBytes> payload{};
    std::uint16_t payload_size = 0U;
    std::uint32_t edc = 0U;
    XaMetadata xa{};

    [[nodiscard]] bool is_form2() const noexcept { return form == Mode2Form::Form2; }

    [[nodiscard]] std::span<const std::uint8_t, XaAdpcmBytes> xa_adpcm_bytes() const noexcept {
        return std::span<const std::uint8_t, XaAdpcmBytes>(payload.data(), XaAdpcmBytes);
    }
};

enum class XaBitsPerSample : std::uint8_t {
    Four = 4U,
    Eight = 8U,
};

struct XaFormat final {
    bool stereo = false;
    std::uint32_t sample_rate_hz = XaRateFullHz;
    XaBitsPerSample bits_per_sample = XaBitsPerSample::Four;
    bool emphasis = false;

    friend constexpr bool operator==(const XaFormat&, const XaFormat&) noexcept = default;
};

struct XaSector final {
    Lba lba = 0U;
    XaMetadata metadata{};
    XaFormat format{};
    std::array<std::uint8_t, XaAdpcmBytes> adpcm{};
};

struct ParsedDisc;

class CueImage final {
public:
    static Result<CueImage> open(const std::filesystem::path& cue_path);

    [[nodiscard]] const DiscInfo& disc() const noexcept { return disc_; }
    [[nodiscard]] std::span<const TrackInfo> tracks() const noexcept { return disc_.tracks; }
    [[nodiscard]] const TrackInfo* track_for_lba(Lba lba) const noexcept;

    Result<RawSector> read_raw_sector(Lba lba) const;
    Result<Mode1Sector> read_mode1_sector(Lba lba) const;
    Result<Mode2Sector> read_mode2_sector(Lba lba) const;
    Result<CddaSector> read_cdda_sector(Lba lba) const;
    Result<XaSector> read_xa_sector(Lba lba) const;

    static Result<Mode2Sector> decode_mode2(
        Lba lba,
        std::span<const std::uint8_t, RawSectorBytes> raw);
    static Result<Mode1Sector> decode_mode1(
        Lba lba,
        std::span<const std::uint8_t, RawSectorBytes> raw);
    static Result<CddaSector> decode_cdda(
        std::span<const std::uint8_t, RawSectorBytes> raw);
    static Result<XaSector> extract_xa(const Mode2Sector& sector);
    static std::uint32_t compute_edc(std::span<const std::uint8_t> bytes) noexcept;
    static Msf lba_to_msf(Lba lba) noexcept;
    static bool encode_bcd(std::uint8_t value, std::uint8_t& result) noexcept;

private:
    friend Result<CueImage> build_image(ParsedDisc parsed);

    struct FileEntry final {
        std::filesystem::path path{};
        std::uint64_t sector_count = 0U;
    };

    std::vector<FileEntry> files_{};
    DiscInfo disc_{};
};

class XaAdpcmDecoder final {
public:
    void reset() noexcept;

    static std::size_t samples_per_sector(const XaFormat& format) noexcept;
    Result<std::size_t> decode(const XaSector& sector,
                               std::span<StereoSample> destination);

private:
    struct History final {
        std::int64_t previous = 0;
        std::int64_t older = 0;
    };

    std::array<History, 2U> history_{};
};

class XaRateConverter final {
public:
    struct Snapshot final {
        std::uint32_t source_rate_hz = 0U;
        std::uint64_t input_frames = 0U;
        std::uint64_t output_frames = 0U;
        std::array<StereoSample, 32U> ring{};
        std::uint8_t ring_position = 0U;
        std::uint8_t six_step = 6U;
        bool finished = false;

        friend constexpr bool operator==(const Snapshot&, const Snapshot&) noexcept = default;
    };

    static Result<XaRateConverter> create(std::uint32_t source_rate_hz);

    void reset() noexcept;
    Result<std::size_t> push(std::span<const StereoSample> input,
                             std::span<StereoSample> output,
                             bool end_of_stream = false);

    [[nodiscard]] std::uint32_t source_rate_hz() const noexcept { return source_rate_hz_; }
    [[nodiscard]] std::uint64_t input_frames() const noexcept { return input_frames_; }
    [[nodiscard]] std::uint64_t output_frames() const noexcept { return output_frames_; }
    [[nodiscard]] Snapshot snapshot() const noexcept;
    [[nodiscard]] bool restore_snapshot(const Snapshot& snapshot) noexcept;

private:
    explicit XaRateConverter(std::uint32_t source_rate_hz) : source_rate_hz_(source_rate_hz) {}

    [[nodiscard]] StereoSample interpolate_phase(std::size_t phase) const noexcept;
    void push_37800_sample(const StereoSample& sample,
                           std::span<StereoSample> output,
                           std::size_t& produced) noexcept;

    std::uint32_t source_rate_hz_ = 0U;
    std::uint64_t input_frames_ = 0U;
    std::uint64_t output_frames_ = 0U;
    std::array<StereoSample, 32U> ring_{};
    std::uint8_t ring_position_ = 0U;
    std::uint8_t six_step_ = 6U;
    bool finished_ = false;
};

} // namespace clean_aot::media

#endif // CLEAN_AOT_MEDIA_HPP
