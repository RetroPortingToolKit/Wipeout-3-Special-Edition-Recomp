#include "clean_aot/media/media.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace clean_aot::media {

struct ParsedFile final {
    std::filesystem::path path{};
    std::uint64_t sector_count = 0U;
};

struct ParsedTrack final {
    std::uint8_t number = 0U;
    TrackType type = TrackType::Mode2_2352;
    std::uint32_t file_index = 0U;
    bool has_index00 = false;
    std::uint32_t index00_frames = 0U;
    bool has_index01 = false;
    std::uint32_t index01_frames = 0U;
    bool has_pregap = false;
    std::uint32_t pregap_frames = 0U;
    bool has_postgap = false;
    std::uint32_t postgap_frames = 0U;
};

struct ParsedDisc final {
    std::vector<ParsedFile> files{};
    std::vector<ParsedTrack> tracks{};
};

namespace {

constexpr std::uint64_t LeadInFrames = 150U;
constexpr std::uint64_t FramesPerSecond = 75U;
constexpr std::uint64_t FramesPerMinute = 4500U;
constexpr std::uint64_t MaximumDiscSectors = 449850U;
constexpr std::size_t MaximumTrackCount = 99U;
constexpr std::size_t MaximumCueBytes = 8U * 1024U * 1024U;
constexpr std::size_t MaximumCueLineBytes = 4096U;
constexpr std::uint32_t FormTwoEdcOffset = 2348U;
constexpr std::uint32_t FormOneEdcOffset = 2072U;
constexpr std::uint32_t Mode2PayloadOffset = 24U;

Error make_error(ErrorCode code, std::string detail, std::size_t line = 0U) {
    return Error{code, line, std::move(detail)};
}

bool is_ascii_space(char value) noexcept {
    return value == ' ' || value == '\t';
}

char ascii_upper(char value) noexcept {
    const unsigned char byte = static_cast<unsigned char>(value);
    if (byte >= static_cast<unsigned char>('a') && byte <= static_cast<unsigned char>('z')) {
        return static_cast<char>(byte - static_cast<unsigned char>('a') +
                                 static_cast<unsigned char>('A'));
    }
    return value;
}

std::string uppercase(std::string value) {
    for (char& character : value) {
        character = ascii_upper(character);
    }
    return value;
}

bool parse_decimal(std::string_view token, std::uint64_t& value) noexcept {
    if (token.empty()) {
        return false;
    }
    std::uint64_t result = 0U;
    for (const char character : token) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return false;
        }
        result = result * 10U + digit;
    }
    value = result;
    return true;
}

bool parse_msf(std::string_view token, std::uint32_t& frames) noexcept {
    if (token.size() != 8U || token[2U] != ':' || token[5U] != ':') {
        return false;
    }
    const auto two_digits = [](std::string_view value, std::uint8_t& result) noexcept {
        if (value.size() != 2U || value[0U] < '0' || value[0U] > '9' || value[1U] < '0' ||
            value[1U] > '9') {
            return false;
        }
        result = static_cast<std::uint8_t>((value[0U] - '0') * 10 + (value[1U] - '0'));
        return true;
    };

    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;
    if (!two_digits(token.substr(0U, 2U), minute) ||
        !two_digits(token.substr(3U, 2U), second) ||
        !two_digits(token.substr(6U, 2U), frame) || second >= 60U || frame >= 75U) {
        return false;
    }
    frames = static_cast<std::uint32_t>(minute) * static_cast<std::uint32_t>(FramesPerMinute) +
             static_cast<std::uint32_t>(second) * static_cast<std::uint32_t>(FramesPerSecond) +
             static_cast<std::uint32_t>(frame);
    return true;
}

Msf frames_to_msf(std::uint64_t frames) noexcept {
    const std::uint64_t minute = frames / FramesPerMinute;
    const std::uint64_t second = (frames / FramesPerSecond) % 60U;
    const std::uint64_t frame = frames % FramesPerSecond;
    if (minute > 99U) {
        return Msf{0xffU, 0xffU, 0xffU};
    }
    return Msf{static_cast<std::uint8_t>(minute), static_cast<std::uint8_t>(second),
               static_cast<std::uint8_t>(frame)};
}

bool bcd_to_decimal(std::uint8_t value, std::uint8_t& result) noexcept {
    const std::uint8_t high = static_cast<std::uint8_t>(value >> 4U);
    const std::uint8_t low = static_cast<std::uint8_t>(value & 0x0fU);
    if (high >= 10U || low >= 10U) {
        return false;
    }
    result = static_cast<std::uint8_t>(high * 10U + low);
    return true;
}

std::uint32_t read_le_u32(const std::uint8_t* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0U]) |
           (static_cast<std::uint32_t>(bytes[1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3U]) << 24U);
}

bool raw_sync_is_valid(std::span<const std::uint8_t, RawSectorBytes> raw) noexcept {
    if (raw[0U] != 0U || raw[11U] != 0U) {
        return false;
    }
    for (std::size_t index = 1U; index < 11U; ++index) {
        if (raw[index] != 0xffU) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool multiply_will_overflow(T first, T second) noexcept {
    return second != 0U && first > std::numeric_limits<T>::max() / second;
}

Result<std::vector<std::string>> tokenize(std::string_view line, std::size_t line_number) {
    std::vector<std::string> tokens;
    std::size_t cursor = 0U;
    while (cursor < line.size()) {
        while (cursor < line.size() && is_ascii_space(line[cursor])) {
            ++cursor;
        }
        if (cursor == line.size()) {
            break;
        }
        if (line[cursor] == '"') {
            ++cursor;
            const std::size_t start = cursor;
            while (cursor < line.size() && line[cursor] != '"') {
                const unsigned char byte = static_cast<unsigned char>(line[cursor]);
                if (byte < 0x20U || byte == 0x7fU) {
                    return Result<std::vector<std::string>>::failure(
                        make_error(ErrorCode::CueSyntax, "control character in quoted token",
                                   line_number));
                }
                ++cursor;
            }
            if (cursor == line.size()) {
                return Result<std::vector<std::string>>::failure(
                    make_error(ErrorCode::CueSyntax, "unterminated quoted token", line_number));
            }
            tokens.emplace_back(line.substr(start, cursor - start));
            ++cursor;
            if (cursor < line.size() && !is_ascii_space(line[cursor])) {
                return Result<std::vector<std::string>>::failure(make_error(
                    ErrorCode::CueSyntax, "quoted token must be separated by whitespace",
                    line_number));
            }
        } else {
            const std::size_t start = cursor;
            while (cursor < line.size() && !is_ascii_space(line[cursor])) {
                const unsigned char byte = static_cast<unsigned char>(line[cursor]);
                if (byte < 0x20U || byte == 0x7fU || line[cursor] == '"') {
                    return Result<std::vector<std::string>>::failure(
                        make_error(ErrorCode::CueSyntax, "invalid character in token", line_number));
                }
                ++cursor;
            }
            tokens.emplace_back(line.substr(start, cursor - start));
        }
        if (tokens.size() > 8U) {
            return Result<std::vector<std::string>>::failure(
                make_error(ErrorCode::CueSyntax, "too many tokens on one CUE line", line_number));
        }
    }
    return Result<std::vector<std::string>>::success(std::move(tokens));
}

Result<std::string> read_text_file(const std::filesystem::path& path) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > MaximumCueBytes) {
        return Result<std::string>::failure(make_error(
            error ? ErrorCode::OpenFailed : ErrorCode::CueTooLarge,
            error ? "unable to determine CUE size" : "CUE exceeds bounded text size"));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            make_error(ErrorCode::OpenFailed, "unable to open CUE file"));
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size != 0U) {
        input.read(text.data(), static_cast<std::streamsize>(size));
        if (!input || input.gcount() != static_cast<std::streamsize>(size)) {
            return Result<std::string>::failure(
                make_error(ErrorCode::ReadFailed, "unable to read complete CUE file"));
        }
    }
    if (text.size() >= 3U && static_cast<unsigned char>(text[0U]) == 0xefU &&
        static_cast<unsigned char>(text[1U]) == 0xbbU &&
        static_cast<unsigned char>(text[2U]) == 0xbfU) {
        text.erase(0U, 3U);
    }
    return Result<std::string>::success(std::move(text));
}

Result<std::filesystem::path> resolve_file_path(const std::filesystem::path& cue_path,
                                                std::string_view name,
                                                std::size_t line) {
    if (name.empty()) {
        return Result<std::filesystem::path>::failure(
            make_error(ErrorCode::InvalidPath, "empty FILE path", line));
    }
    const std::filesystem::path relative_path{std::string(name)};
    if (relative_path.empty() || relative_path.has_root_name() ||
        relative_path.has_root_directory() || relative_path.is_absolute()) {
        return Result<std::filesystem::path>::failure(
            make_error(ErrorCode::InvalidPath, "absolute FILE path is not allowed", line));
    }
    for (const auto& component : relative_path) {
        if (component == "..") {
            return Result<std::filesystem::path>::failure(
                make_error(ErrorCode::InvalidPath, "FILE path traversal is not allowed", line));
        }
    }

    std::error_code error;
    const std::filesystem::path cue_absolute = std::filesystem::weakly_canonical(cue_path, error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            make_error(ErrorCode::OpenFailed, "unable to canonicalize CUE path", line));
    }
    const std::filesystem::path cue_directory = cue_absolute.parent_path();
    const std::filesystem::path candidate = cue_directory / relative_path;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return Result<std::filesystem::path>::failure(
            make_error(ErrorCode::FileMissing, "unable to resolve FILE path", line));
    }
    const std::filesystem::path relative_to_cue =
        std::filesystem::relative(canonical, cue_directory, error);
    if (error || relative_to_cue.empty()) {
        return Result<std::filesystem::path>::failure(
            make_error(ErrorCode::InvalidPath, "FILE path leaves the CUE directory", line));
    }
    for (const auto& component : relative_to_cue) {
        if (component == "..") {
            return Result<std::filesystem::path>::failure(
                make_error(ErrorCode::InvalidPath, "FILE path leaves the CUE directory", line));
        }
    }
    return Result<std::filesystem::path>::success(canonical);
}

Result<std::uint64_t> file_sector_count(const std::filesystem::path& path,
                                        std::size_t line) {
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<std::uint64_t>::failure(
            make_error(ErrorCode::FileMissing, "FILE does not exist", line));
    }
    if (bytes == 0U || bytes % RawSectorBytes != 0U) {
        return Result<std::uint64_t>::failure(make_error(
            ErrorCode::FileSize, "BINARY FILE size is not a non-zero raw-sector multiple", line));
    }
    const std::uintmax_t sectors = bytes / RawSectorBytes;
    if (sectors > MaximumDiscSectors) {
        return Result<std::uint64_t>::failure(
            make_error(ErrorCode::FileSize, "BINARY FILE exceeds bounded disc size", line));
    }
    return Result<std::uint64_t>::success(static_cast<std::uint64_t>(sectors));
}

Result<ParsedDisc> parse_cue(const std::filesystem::path& cue_path) {
    auto text_result = read_text_file(cue_path);
    if (!text_result) {
        return Result<ParsedDisc>::failure(text_result.error());
    }
    std::string text = std::move(text_result).value();
    ParsedDisc parsed{};
    std::optional<ParsedTrack> current_track;
    std::optional<std::uint32_t> current_file;

    const auto finish_track = [&]() -> Result<void> {
        if (!current_track.has_value()) {
            return Result<void>::success();
        }
        if (!current_track->has_index01) {
            return Result<void>::failure(
                make_error(ErrorCode::CueSyntax, "TRACK is missing INDEX 01"));
        }
        parsed.tracks.push_back(*current_track);
        current_track.reset();
        return Result<void>::success();
    };

    std::size_t cursor = 0U;
    std::size_t line_number = 0U;
    while (cursor < text.size()) {
        const std::size_t line_end = text.find('\n', cursor);
        const std::size_t end = line_end == std::string::npos ? text.size() : line_end;
        std::string_view line(text.data() + cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        ++line_number;
        if (line.size() > MaximumCueLineBytes) {
            return Result<ParsedDisc>::failure(
                make_error(ErrorCode::CueTooLarge, "CUE line exceeds bounded length", line_number));
        }
        auto tokens_result = tokenize(line, line_number);
        if (!tokens_result) {
            return Result<ParsedDisc>::failure(tokens_result.error());
        }
        const std::vector<std::string>& tokens = tokens_result.value();
        if (!tokens.empty()) {
            const std::string command = uppercase(tokens[0U]);
            if (command == "REM") {
                // REM is deliberately opaque.  It cannot affect the media map.
            } else if (command == "FILE") {
                if (tokens.size() != 3U) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::CueSyntax, "FILE requires a quoted name and BINARY", line_number));
                }
                if (uppercase(tokens[2U]) != "BINARY") {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::UnsupportedFileType, "only FILE ... BINARY is accepted", line_number));
                }
                auto finish_result = finish_track();
                if (!finish_result) {
                    return Result<ParsedDisc>::failure(finish_result.error());
                }
                auto path_result = resolve_file_path(cue_path, tokens[1U], line_number);
                if (!path_result) {
                    return Result<ParsedDisc>::failure(path_result.error());
                }
                const std::filesystem::path path = path_result.value();
                std::uint32_t file_index = 0U;
                bool found = false;
                for (std::size_t index = 0U; index < parsed.files.size(); ++index) {
                    if (parsed.files[index].path == path) {
                        file_index = static_cast<std::uint32_t>(index);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    auto sectors_result = file_sector_count(path, line_number);
                    if (!sectors_result) {
                        return Result<ParsedDisc>::failure(sectors_result.error());
                    }
                    if (parsed.files.size() >= MaximumTrackCount) {
                        return Result<ParsedDisc>::failure(
                            make_error(ErrorCode::Overflow, "too many FILE entries", line_number));
                    }
                    file_index = static_cast<std::uint32_t>(parsed.files.size());
                    parsed.files.push_back(ParsedFile{path, sectors_result.value()});
                }
                current_file = file_index;
            } else if (command == "TRACK") {
                if (tokens.size() != 3U || !current_file.has_value()) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::CueSyntax, "TRACK requires a preceding FILE and a type", line_number));
                }
                auto finish_result = finish_track();
                if (!finish_result) {
                    return Result<ParsedDisc>::failure(finish_result.error());
                }
                std::uint64_t parsed_number = 0U;
                if (tokens[1U].size() != 2U || !parse_decimal(tokens[1U], parsed_number) ||
                    parsed_number == 0U || parsed_number > 99U) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::InvalidTrackNumber, "TRACK number must be 01 through 99", line_number));
                }
                const std::uint8_t number =
                    static_cast<std::uint8_t>(parsed_number);
                const std::string type = uppercase(tokens[2U]);
                TrackType track_type{};
                if (type == "MODE1/2352") {
                    track_type = TrackType::Mode1_2352;
                } else if (type == "MODE2/2352") {
                    track_type = TrackType::Mode2_2352;
                } else if (type == "AUDIO") {
                    track_type = TrackType::Audio;
                } else {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::UnsupportedTrackType,
                        "only TRACK MODE1/2352, MODE2/2352, and AUDIO are accepted", line_number));
                }
                const std::uint8_t expected_number =
                    static_cast<std::uint8_t>(parsed.tracks.size() + 1U);
                if (number != expected_number) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::InvalidTrackNumber, "TRACK numbers must be contiguous from 01",
                        line_number));
                }
                current_track = ParsedTrack{number, track_type, *current_file};
            } else if (command == "INDEX") {
                if (tokens.size() != 3U || !current_track.has_value()) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::CueSyntax, "INDEX requires an active TRACK", line_number));
                }
                if (tokens[1U] != "00" && tokens[1U] != "01") {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::CueSyntax, "only INDEX 00 and INDEX 01 are accepted", line_number));
                }
                std::uint32_t frames = 0U;
                if (!parse_msf(tokens[2U], frames)) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::InvalidMsf, "invalid INDEX MSF", line_number));
                }
                if (tokens[1U] == "00") {
                    if (current_track->has_index00 || current_track->has_index01) {
                        return Result<ParsedDisc>::failure(make_error(
                            ErrorCode::CueSyntax, "INDEX 00 must precede INDEX 01 and be unique",
                            line_number));
                    }
                    current_track->has_index00 = true;
                    current_track->index00_frames = frames;
                } else {
                    if (current_track->has_index01 ||
                        (current_track->has_index00 &&
                         current_track->index00_frames > frames)) {
                        return Result<ParsedDisc>::failure(make_error(
                            ErrorCode::CueSyntax, "INDEX 01 is duplicated or precedes INDEX 00",
                            line_number));
                    }
                    current_track->has_index01 = true;
                    current_track->index01_frames = frames;
                }
            } else if (command == "PREGAP" || command == "POSTGAP") {
                if (tokens.size() != 2U || !current_track.has_value()) {
                    return Result<ParsedDisc>::failure(make_error(
                        ErrorCode::CueSyntax, "gap directive requires an active TRACK", line_number));
                }
                std::uint32_t frames = 0U;
                if (!parse_msf(tokens[1U], frames)) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::InvalidMsf, "invalid gap MSF", line_number));
                }
                if (command == "PREGAP") {
                    if (current_track->has_pregap || current_track->has_index00 ||
                        current_track->has_index01) {
                        return Result<ParsedDisc>::failure(make_error(
                            ErrorCode::CueSyntax,
                            "PREGAP must be unique and precede all INDEX directives", line_number));
                    }
                    current_track->has_pregap = true;
                    current_track->pregap_frames = frames;
                } else {
                    if (current_track->has_postgap || !current_track->has_index01) {
                        return Result<ParsedDisc>::failure(make_error(
                            ErrorCode::CueSyntax,
                            "POSTGAP must be unique and follow INDEX 01", line_number));
                    }
                    current_track->has_postgap = true;
                    current_track->postgap_frames = frames;
                }
            } else if (command == "CATALOG") {
                std::uint64_t ignored = 0U;
                if (tokens.size() != 2U || tokens[1U].size() != 13U ||
                    !parse_decimal(tokens[1U], ignored)) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::CueSyntax, "invalid CATALOG directive", line_number));
                }
            } else if (command == "PERFORMER" || command == "TITLE" ||
                       command == "SONGWRITER" || command == "CDTEXTFILE") {
                if (tokens.size() != 2U || tokens[1U].empty()) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::CueSyntax, "metadata directive requires one value",
                                   line_number));
                }
            } else if (command == "ISRC") {
                if (tokens.size() != 2U || tokens[1U].size() != 12U) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::CueSyntax, "invalid ISRC directive", line_number));
                }
            } else if (command == "FLAGS") {
                if (tokens.size() < 2U) {
                    return Result<ParsedDisc>::failure(
                        make_error(ErrorCode::CueSyntax, "FLAGS requires at least one flag", line_number));
                }
            } else {
                return Result<ParsedDisc>::failure(
                    make_error(ErrorCode::UnsupportedDirective, "unsupported CUE directive", line_number));
            }
        }
        cursor = line_end == std::string::npos ? text.size() : line_end + 1U;
    }
    auto finish_result = finish_track();
    if (!finish_result) {
        return Result<ParsedDisc>::failure(finish_result.error());
    }
    if (parsed.tracks.empty()) {
        return Result<ParsedDisc>::failure(
            make_error(ErrorCode::DiscLayout, "CUE contains no tracks"));
    }
    return Result<ParsedDisc>::success(std::move(parsed));
}

} // namespace

Result<CueImage> build_image(ParsedDisc parsed) {
    CueImage image{};
    image.files_.reserve(parsed.files.size());
    for (ParsedFile& parsed_file : parsed.files) {
        image.files_.push_back(
            CueImage::FileEntry{std::move(parsed_file.path), parsed_file.sector_count});
    }
    const std::size_t track_count = parsed.tracks.size();
    image.disc_.tracks.reserve(track_count);

    for (std::size_t index = 0U; index < track_count; ++index) {
        const ParsedTrack& parsed_track = parsed.tracks[index];
        if (parsed_track.file_index >= image.files_.size()) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::DiscLayout, "TRACK references an unknown FILE"));
        }
        const std::uint64_t file_sectors =
            image.files_[parsed_track.file_index].sector_count;
        const std::uint64_t index01_offset = parsed_track.index01_frames;
        const std::uint64_t stored_start_offset =
            parsed_track.has_index00 ? parsed_track.index00_frames : index01_offset;
        if (index01_offset >= file_sectors || stored_start_offset >= file_sectors ||
            (parsed_track.has_index00 && parsed_track.index00_frames > index01_offset)) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::FileLayout, "INDEX points outside its BINARY FILE"));
        }
        if (parsed_track.has_index00 && parsed_track.pregap_frames != 0U) {
            return Result<CueImage>::failure(make_error(
                ErrorCode::DiscLayout, "INDEX 00 and non-zero PREGAP are ambiguous"));
        }

        if (index != 0U && parsed.tracks[index - 1U].file_index != parsed_track.file_index) {
            for (std::size_t earlier = 0U; earlier + 1U < index; ++earlier) {
                if (parsed.tracks[earlier].file_index == parsed_track.file_index) {
                    return Result<CueImage>::failure(make_error(
                        ErrorCode::DiscLayout,
                        "a BINARY FILE cannot be reopened after a file switch"));
                }
            }
        }
        const std::uint64_t next_offset =
            index + 1U < track_count &&
                    parsed.tracks[index + 1U].file_index == parsed_track.file_index
                ? (parsed.tracks[index + 1U].has_index00
                       ? parsed.tracks[index + 1U].index00_frames
                       : parsed.tracks[index + 1U].index01_frames)
                : file_sectors;
        if (next_offset <= stored_start_offset || next_offset <= index01_offset ||
            next_offset > file_sectors) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::FileLayout, "track ranges overlap or exceed BINARY FILE"));
        }

        const auto add_checked = [](std::uint64_t first, std::uint64_t second,
                                    std::uint64_t& result) noexcept {
            if (first > std::numeric_limits<std::uint64_t>::max() - second) {
                return false;
            }
            result = first + second;
            return true;
        };
        const std::uint64_t logical_start_lba =
            index == 0U ? 0U : image.disc_.tracks.back().logical_end_lba;
        std::uint64_t stored_start_lba = 0U;
        std::uint64_t index01_lba = 0U;
        if (parsed_track.has_index00) {
            if (index == 0U && parsed_track.index00_frames != index01_offset) {
                return Result<CueImage>::failure(make_error(
                    ErrorCode::DiscLayout,
                    "TRACK 01 INDEX 00 would expose hidden lead-in sectors"));
            }
            stored_start_lba = logical_start_lba;
            const std::uint64_t index_span = index01_offset - stored_start_offset;
            if (!add_checked(stored_start_lba, index_span, index01_lba)) {
                return Result<CueImage>::failure(
                    make_error(ErrorCode::Overflow, "INDEX 01 overflows logical LBA math"));
            }
        } else {
            if (index == 0U && parsed_track.pregap_frames != 0U) {
                return Result<CueImage>::failure(make_error(
                    ErrorCode::DiscLayout,
                    "TRACK 01 PREGAP would expose hidden lead-in sectors"));
            }
            if (!add_checked(logical_start_lba,
                             static_cast<std::uint64_t>(parsed_track.pregap_frames),
                             stored_start_lba)) {
                return Result<CueImage>::failure(
                    make_error(ErrorCode::Overflow, "PREGAP overflows logical LBA math"));
            }
            index01_lba = stored_start_lba;
        }
        const std::uint64_t stored_length = next_offset - stored_start_offset;
        std::uint64_t stored_end_lba = 0U;
        if (!add_checked(stored_start_lba, stored_length, stored_end_lba)) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::Overflow, "stored track overflows logical LBA math"));
        }
        std::uint64_t logical_end_lba = 0U;
        if (!add_checked(stored_end_lba,
                         static_cast<std::uint64_t>(parsed_track.postgap_frames),
                         logical_end_lba)) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::Overflow, "POSTGAP overflows logical LBA math"));
        }
        if (index01_lba < stored_start_lba || index01_lba >= stored_end_lba ||
            logical_end_lba > MaximumDiscSectors || logical_start_lba >= logical_end_lba) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::DiscLayout, "track is outside bounded logical LBA space"));
        }
        if (index == 0U && logical_start_lba != 0U) {
            return Result<CueImage>::failure(make_error(
                ErrorCode::DiscLayout,
                "track 01 must begin at logical LBA 0; hidden lead-in is unsupported"));
        }
        if (index != 0U &&
            logical_start_lba != image.disc_.tracks.back().logical_end_lba) {
            return Result<CueImage>::failure(
                make_error(ErrorCode::DiscLayout, "track gaps are not represented by PREGAP/POSTGAP"));
        }

        TrackInfo info{};
        info.number = parsed_track.number;
        info.type = parsed_track.type;
        info.file_index = parsed_track.file_index;
        info.file_sector_begin = stored_start_offset;
        info.file_sector_end = next_offset;
        info.has_index00 = parsed_track.has_index00;
        info.index00 = frames_to_msf(parsed_track.index00_frames);
        info.index01 = frames_to_msf(index01_offset);
        info.has_pregap = parsed_track.has_pregap;
        info.pregap_frames = parsed_track.pregap_frames;
        info.has_postgap = parsed_track.has_postgap;
        info.postgap_frames = parsed_track.postgap_frames;
        info.logical_start_lba = logical_start_lba;
        info.stored_start_lba = stored_start_lba;
        info.index01_lba = index01_lba;
        info.stored_end_lba = stored_end_lba;
        info.logical_end_lba = logical_end_lba;
        image.disc_.tracks.push_back(info);
    }
    image.disc_.lead_out_lba = image.disc_.tracks.back().logical_end_lba;
    return Result<CueImage>::success(std::move(image));
}

namespace {

bool valid_xa_format(const XaFormat& format) noexcept {
    return (format.sample_rate_hz == XaRateFullHz || format.sample_rate_hz == XaRateHalfHz) &&
           (format.bits_per_sample == XaBitsPerSample::Four ||
            format.bits_per_sample == XaBitsPerSample::Eight);
}

std::int64_t round_half_away_from_zero(std::int64_t numerator,
                                       std::int64_t denominator) noexcept {
    if (numerator >= 0) {
        return (numerator + denominator / 2) / denominator;
    }
    return -((-numerator + denominator / 2) / denominator);
}

std::int16_t clamp_sample(std::int64_t value) noexcept {
    if (value < static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min())) {
        return std::numeric_limits<std::int16_t>::min();
    }
    if (value > static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())) {
        return std::numeric_limits<std::int16_t>::max();
    }
    return static_cast<std::int16_t>(value);
}

} // namespace

Result<CueImage> CueImage::open(const std::filesystem::path& cue_path) {
    auto parsed_result = parse_cue(cue_path);
    if (!parsed_result) {
        return Result<CueImage>::failure(parsed_result.error());
    }
    return build_image(std::move(parsed_result).value());
}

const TrackInfo* CueImage::track_for_lba(Lba lba) const noexcept {
    for (const TrackInfo& track : disc_.tracks) {
        if (lba >= track.logical_start_lba && lba < track.logical_end_lba) {
            return &track;
        }
    }
    return nullptr;
}

Result<RawSector> CueImage::read_raw_sector(Lba lba) const {
    const TrackInfo* track = track_for_lba(lba);
    if (track == nullptr) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::LbaOutOfRange, "LBA is outside the disc program area"));
    }
    RawSector sector{};
    if (lba < track->stored_start_lba || lba >= track->stored_end_lba) {
        return Result<RawSector>::success(sector);
    }
    if (track->file_index >= files_.size() || track->file_sector_begin >= track->file_sector_end ||
        lba - track->stored_start_lba >= track->file_sector_end - track->file_sector_begin) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::FileLayout, "LBA maps outside the stored track range"));
    }
    const std::uint64_t file_sector =
        track->file_sector_begin + (lba - track->stored_start_lba);
    if (multiply_will_overflow(file_sector, static_cast<std::uint64_t>(RawSectorBytes))) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::Overflow, "raw-sector file offset overflow"));
    }
    const std::uint64_t offset = file_sector * static_cast<std::uint64_t>(RawSectorBytes);
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::Overflow, "raw-sector file offset exceeds stream range"));
    }
    std::ifstream input(files_[track->file_index].path, std::ios::binary);
    if (!input) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::OpenFailed, "unable to open referenced BINARY FILE"));
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::ReadFailed, "unable to seek referenced BINARY FILE"));
    }
    input.read(reinterpret_cast<char*>(sector.bytes.data()),
               static_cast<std::streamsize>(RawSectorBytes));
    if (!input || input.gcount() != static_cast<std::streamsize>(RawSectorBytes)) {
        return Result<RawSector>::failure(
            make_error(ErrorCode::ReadFailed, "raw sector is truncated"));
    }
    return Result<RawSector>::success(std::move(sector));
}

Result<Mode2Sector> CueImage::read_mode2_sector(Lba lba) const {
    const TrackInfo* track = track_for_lba(lba);
    if (track == nullptr) {
        return Result<Mode2Sector>::failure(
            make_error(ErrorCode::LbaOutOfRange, "LBA is outside the disc program area"));
    }
    if (lba < track->stored_start_lba || lba >= track->stored_end_lba) {
        return Result<Mode2Sector>::failure(
            make_error(ErrorCode::SyntheticSector, "pregap or postgap has no Mode2 payload"));
    }
    if (track->type != TrackType::Mode2_2352) {
        return Result<Mode2Sector>::failure(
            make_error(ErrorCode::WrongTrackType, "MODE2 read requested from an AUDIO track"));
    }
    auto raw_result = read_raw_sector(lba);
    if (!raw_result) {
        return Result<Mode2Sector>::failure(raw_result.error());
    }
    const RawSector& raw = raw_result.value();
    return decode_mode2(lba, std::span<const std::uint8_t, RawSectorBytes>(raw.bytes));
}

Result<Mode1Sector> CueImage::read_mode1_sector(Lba lba) const {
    const TrackInfo* track = track_for_lba(lba);
    if (track == nullptr) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::LbaOutOfRange, "LBA is outside the disc program area"));
    }
    if (lba < track->stored_start_lba || lba >= track->stored_end_lba) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::SyntheticSector, "pregap or postgap has no Mode1 payload"));
    }
    if (track->type != TrackType::Mode1_2352) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::WrongTrackType, "MODE1 read requested from another track type"));
    }
    auto raw_result = read_raw_sector(lba);
    if (!raw_result) {
        return Result<Mode1Sector>::failure(raw_result.error());
    }
    const RawSector& raw = raw_result.value();
    return decode_mode1(lba, std::span<const std::uint8_t, RawSectorBytes>(raw.bytes));
}

Result<CddaSector> CueImage::read_cdda_sector(Lba lba) const {
    const TrackInfo* track = track_for_lba(lba);
    if (track == nullptr) {
        return Result<CddaSector>::failure(
            make_error(ErrorCode::LbaOutOfRange, "LBA is outside the disc program area"));
    }
    if (track->type != TrackType::Audio) {
        return Result<CddaSector>::failure(
            make_error(ErrorCode::WrongTrackType, "CDDA read requested from a data track"));
    }
    if (lba < track->stored_start_lba || lba >= track->stored_end_lba) {
        return Result<CddaSector>::success(CddaSector{});
    }
    auto raw_result = read_raw_sector(lba);
    if (!raw_result) {
        return Result<CddaSector>::failure(raw_result.error());
    }
    const RawSector& raw = raw_result.value();
    return decode_cdda(std::span<const std::uint8_t, RawSectorBytes>(raw.bytes));
}

Result<XaSector> CueImage::read_xa_sector(Lba lba) const {
    auto mode2_result = read_mode2_sector(lba);
    if (!mode2_result) {
        return Result<XaSector>::failure(mode2_result.error());
    }
    return extract_xa(mode2_result.value());
}

Msf CueImage::lba_to_msf(Lba lba) noexcept {
    if (lba > std::numeric_limits<Lba>::max() - LeadInFrames) {
        return Msf{0xffU, 0xffU, 0xffU};
    }
    return frames_to_msf(lba + LeadInFrames);
}

bool CueImage::encode_bcd(std::uint8_t value, std::uint8_t& result) noexcept {
    if (value > 99U) {
        return false;
    }
    result = static_cast<std::uint8_t>((value / 10U) * 16U + value % 10U);
    return true;
}

std::uint32_t CueImage::compute_edc(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0U;
    for (const std::uint8_t byte : bytes) {
        value ^= byte;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const bool carry = (value & 1U) != 0U;
            value >>= 1U;
            if (carry) {
                value ^= 0xd8018001U;
            }
        }
    }
    return value;
}

Result<Mode1Sector> CueImage::decode_mode1(
    Lba lba,
    std::span<const std::uint8_t, RawSectorBytes> raw) {
    if (lba >= MaximumDiscSectors || !raw_sync_is_valid(raw) || raw[15U] != 1U) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::MalformedSector, "raw sector is not a Mode1 sector"));
    }
    const Msf expected = lba_to_msf(lba);
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;
    if (!bcd_to_decimal(raw[12U], minute) || !bcd_to_decimal(raw[13U], second) ||
        !bcd_to_decimal(raw[14U], frame) || Msf{minute, second, frame} != expected) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::MalformedSector, "Mode1 header MSF does not match LBA"));
    }
    const std::uint32_t stored = read_le_u32(raw.data() + 2064U);
    if (stored != compute_edc(raw.first(2064U))) {
        return Result<Mode1Sector>::failure(
            make_error(ErrorCode::InvalidEdc, "Mode1 EDC mismatch"));
    }
    Mode1Sector result{};
    result.lba = lba;
    result.edc = stored;
    std::copy_n(raw.begin() + 16, Mode1PayloadBytes, result.payload.begin());
    return Result<Mode1Sector>::success(std::move(result));
}

Result<Mode2Sector> CueImage::decode_mode2(
    Lba lba,
    std::span<const std::uint8_t, RawSectorBytes> raw) {
    if (lba >= MaximumDiscSectors || !raw_sync_is_valid(raw) || raw[15U] != 2U) {
        return Result<Mode2Sector>::failure(
            make_error(ErrorCode::MalformedSector, "raw sector is not a Mode2 sector"));
    }
    const Msf expected = lba_to_msf(lba);
    std::uint8_t minute = 0U;
    std::uint8_t second = 0U;
    std::uint8_t frame = 0U;
    if (!bcd_to_decimal(raw[12U], minute) || !bcd_to_decimal(raw[13U], second) ||
        !bcd_to_decimal(raw[14U], frame) ||
        Msf{minute, second, frame} != expected) {
        return Result<Mode2Sector>::failure(
            make_error(ErrorCode::MalformedSector, "Mode2 header MSF does not match LBA"));
    }
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (raw[16U + index] != raw[20U + index]) {
            return Result<Mode2Sector>::failure(
                make_error(ErrorCode::MalformedSector, "Mode2 subheader copies differ"));
        }
    }

    Mode2Sector result{};
    result.lba = lba;
    std::copy_n(raw.begin() + 16, result.subheader.size(), result.subheader.begin());
    result.xa.file_number = result.subheader[0U];
    result.xa.channel_number = result.subheader[1U];
    result.xa.submode = result.subheader[2U];
    result.xa.coding_info = result.subheader[3U];
    result.xa.end_of_record = (result.xa.submode & 0x01U) != 0U;
    result.xa.video = (result.xa.submode & 0x02U) != 0U;
    result.xa.audio = (result.xa.submode & 0x04U) != 0U;
    result.xa.data = (result.xa.submode & 0x08U) != 0U;
    result.xa.trigger = (result.xa.submode & 0x10U) != 0U;
    result.xa.form_two = (result.xa.submode & 0x20U) != 0U;
    result.xa.real_time = (result.xa.submode & 0x40U) != 0U;
    result.xa.end_of_file = (result.xa.submode & 0x80U) != 0U;
    result.form = result.xa.form_two ? Mode2Form::Form2 : Mode2Form::Form1;
    const std::size_t edc_offset = result.is_form2() ? FormTwoEdcOffset : FormOneEdcOffset;
    const std::size_t edc_input_offset = 16U;
    const std::size_t edc_input_length = edc_offset - edc_input_offset;
    const std::uint32_t stored_edc = read_le_u32(raw.data() + edc_offset);
    const std::uint32_t calculated_edc =
        compute_edc(std::span<const std::uint8_t>(raw.data() + edc_input_offset, edc_input_length));
    if (result.is_form2()) {
        if (stored_edc != 0U && stored_edc != calculated_edc) {
            return Result<Mode2Sector>::failure(
                make_error(ErrorCode::InvalidEdc, "Mode2 Form2 EDC mismatch"));
        }
        result.payload_size = static_cast<std::uint16_t>(Mode2FormTwoPayloadBytes);
    } else {
        if (stored_edc != calculated_edc) {
            return Result<Mode2Sector>::failure(
                make_error(ErrorCode::InvalidEdc, "Mode2 Form1 EDC mismatch"));
        }
        result.payload_size = static_cast<std::uint16_t>(Mode2FormOnePayloadBytes);
    }
    result.edc = stored_edc;
    std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(Mode2PayloadOffset),
                static_cast<std::size_t>(result.payload_size), result.payload.begin());
    return Result<Mode2Sector>::success(std::move(result));
}

Result<CddaSector> CueImage::decode_cdda(
    std::span<const std::uint8_t, RawSectorBytes> raw) {
    CddaSector result{};
    // Redump raw BIN audio sectors use interleaved signed 16-bit little-endian PCM.
    for (std::size_t frame = 0U; frame < CddaFramesPerSector; ++frame) {
        const std::size_t offset = frame * 4U;
        const std::uint16_t left_bits = static_cast<std::uint16_t>(raw[offset]) |
                                         static_cast<std::uint16_t>(raw[offset + 1U] << 8U);
        const std::uint16_t right_bits = static_cast<std::uint16_t>(raw[offset + 2U]) |
                                          static_cast<std::uint16_t>(raw[offset + 3U] << 8U);
        result.frames[frame] =
            StereoSample{static_cast<std::int16_t>(left_bits), static_cast<std::int16_t>(right_bits)};
    }
    return Result<CddaSector>::success(std::move(result));
}

Result<XaSector> CueImage::extract_xa(const Mode2Sector& sector) {
    if (!sector.is_form2() || !sector.xa.audio) {
        return Result<XaSector>::failure(make_error(
            ErrorCode::InvalidXaFormat, "sector is not an XA audio Mode2 Form2 sector"));
    }
    const std::uint8_t coding = sector.xa.coding_info;
    if ((coding & 0xaau) != 0U) {
        return Result<XaSector>::failure(
            make_error(ErrorCode::InvalidXaFormat, "XA coding info has reserved bits set"));
    }
    XaSector result{};
    result.lba = sector.lba;
    result.metadata = sector.xa;
    result.format.stereo = (coding & 0x01U) != 0U;
    result.format.sample_rate_hz = (coding & 0x04U) != 0U ? XaRateHalfHz : XaRateFullHz;
    result.format.bits_per_sample = (coding & 0x10U) != 0U ? XaBitsPerSample::Eight
                                                           : XaBitsPerSample::Four;
    result.format.emphasis = (coding & 0x40U) != 0U;
    std::copy_n(sector.payload.begin(), XaAdpcmBytes, result.adpcm.begin());
    return Result<XaSector>::success(std::move(result));
}

void XaAdpcmDecoder::reset() noexcept {
    history_ = {};
}

std::size_t XaAdpcmDecoder::samples_per_sector(const XaFormat& format) noexcept {
    const std::size_t blocks = format.bits_per_sample == XaBitsPerSample::Eight ? 4U : 8U;
    const std::size_t blocks_per_channel = format.stereo ? blocks / 2U : blocks;
    return 18U * blocks_per_channel * 28U;
}

Result<std::size_t> XaAdpcmDecoder::decode(const XaSector& sector,
                                           std::span<StereoSample> destination) {
    if (!valid_xa_format(sector.format) || sector.format.emphasis) {
        return Result<std::size_t>::failure(make_error(
            ErrorCode::InvalidXaFormat, "XA format is unsupported or uses emphasis"));
    }
    const std::size_t required = samples_per_sector(sector.format);
    if (destination.size() < required) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::OutputTooSmall, "XA destination does not hold one sector"));
    }

    const std::size_t block_count =
        sector.format.bits_per_sample == XaBitsPerSample::Eight ? 4U : 8U;
    for (std::size_t group = 0U; group < 18U; ++group) {
        const std::uint8_t* chunk = sector.adpcm.data() + group * 128U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            if (chunk[index] != chunk[4U + index] || chunk[12U + index] != chunk[8U + index]) {
                return Result<std::size_t>::failure(
                    make_error(ErrorCode::InvalidXaHeader, "XA header copies differ"));
            }
        }
        for (std::size_t block = 0U; block < block_count; ++block) {
            const std::uint8_t parameter = chunk[4U + block];
            const std::uint8_t range = static_cast<std::uint8_t>(parameter & 0x0fU);
            const std::uint8_t filter = static_cast<std::uint8_t>((parameter >> 4U) & 0x03U);
            const std::uint8_t reserved = static_cast<std::uint8_t>(parameter & 0xc0U);
            const std::uint8_t maximum_range =
                sector.format.bits_per_sample == XaBitsPerSample::Eight ? 8U : 12U;
            if (reserved != 0U || filter > 3U || range > maximum_range) {
                return Result<std::size_t>::failure(
                    make_error(ErrorCode::InvalidXaHeader, "XA shift/filter header is invalid"));
            }
        }
    }

    constexpr std::array<std::int64_t, 4U> Positive{{0, 60, 115, 98}};
    constexpr std::array<std::int64_t, 4U> Negative{{0, 0, -52, -55}};
    std::array<History, 2U> work = history_;
    for (std::size_t group = 0U; group < 18U; ++group) {
        const std::uint8_t* chunk = sector.adpcm.data() + group * 128U;
        const std::size_t blocks_per_channel = sector.format.stereo ? block_count / 2U : block_count;
        for (std::size_t block = 0U; block < block_count; ++block) {
            const std::size_t channel = sector.format.stereo ? block & 1U : 0U;
            const std::size_t channel_block = sector.format.stereo ? block / 2U : block;
            const std::size_t output_base =
                group * blocks_per_channel * 28U + channel_block * 28U;
            const std::uint8_t parameter = chunk[4U + block];
            const std::uint8_t range = static_cast<std::uint8_t>(parameter & 0x0fU);
            const std::uint8_t filter = static_cast<std::uint8_t>((parameter >> 4U) & 0x03U);
            for (std::size_t sample = 0U; sample < 28U; ++sample) {
                const std::size_t word_offset = 16U + sample * 4U;
                const std::uint32_t word = static_cast<std::uint32_t>(chunk[word_offset]) |
                                           (static_cast<std::uint32_t>(chunk[word_offset + 1U]) << 8U) |
                                           (static_cast<std::uint32_t>(chunk[word_offset + 2U]) << 16U) |
                                           (static_cast<std::uint32_t>(chunk[word_offset + 3U]) << 24U);
                std::int64_t delta = 0;
                if (sector.format.bits_per_sample == XaBitsPerSample::Eight) {
                    const std::uint8_t raw_byte =
                        static_cast<std::uint8_t>((word >> (block * 8U)) & 0xffU);
                    delta = raw_byte < 0x80U ? static_cast<std::int64_t>(raw_byte)
                                             : static_cast<std::int64_t>(raw_byte) - 256;
                    delta <<= 8U;
                } else {
                    const std::uint8_t raw_nibble =
                        static_cast<std::uint8_t>((word >> (block * 4U)) & 0x0fU);
                    delta = raw_nibble < 0x08U ? static_cast<std::int64_t>(raw_nibble)
                                               : static_cast<std::int64_t>(raw_nibble) - 16;
                    delta <<= 12U;
                }
                delta >>= range;
                const std::int64_t prediction = round_half_away_from_zero(
                    work[channel].previous * Positive[filter] +
                        work[channel].older * Negative[filter],
                    64);
                const std::int16_t output = clamp_sample(delta + prediction);
                work[channel].older = work[channel].previous;
                work[channel].previous = output;
                if (sector.format.stereo) {
                    if (channel == 0U) {
                        destination[output_base + sample].left = output;
                    } else {
                        destination[output_base + sample].right = output;
                    }
                } else {
                    destination[output_base + sample] = StereoSample{output, output};
                }
            }
        }
    }
    history_ = work;
    return Result<std::size_t>::success(required);
}

Result<XaRateConverter> XaRateConverter::create(std::uint32_t source_rate_hz) {
    if (source_rate_hz != XaRateFullHz && source_rate_hz != XaRateHalfHz) {
        return Result<XaRateConverter>::failure(
            make_error(ErrorCode::InvalidRate, "XA source rate must be 37800 or 18900 Hz"));
    }
    return Result<XaRateConverter>::success(XaRateConverter(source_rate_hz));
}

void XaRateConverter::reset() noexcept {
    input_frames_ = 0U;
    output_frames_ = 0U;
    ring_.fill({});
    ring_position_ = 0U;
    six_step_ = 6U;
    finished_ = false;
}

XaRateConverter::Snapshot XaRateConverter::snapshot() const noexcept {
    return Snapshot{source_rate_hz_, input_frames_, output_frames_, ring_,
                    ring_position_, six_step_, finished_};
}

bool XaRateConverter::restore_snapshot(const Snapshot& snapshot) noexcept {
    if (snapshot.source_rate_hz != XaRateFullHz &&
        snapshot.source_rate_hz != XaRateHalfHz) {
        return false;
    }
    const std::uint64_t expansion =
        snapshot.source_rate_hz == XaRateHalfHz ? 2U : 1U;
    if (snapshot.input_frames >
        std::numeric_limits<std::uint64_t>::max() / expansion) {
        return false;
    }
    const std::uint64_t expanded = snapshot.input_frames * expansion;
    const std::uint64_t groups = expanded / 6U;
    if (groups > std::numeric_limits<std::uint64_t>::max() / 7U ||
        snapshot.ring_position >= snapshot.ring.size() ||
        snapshot.ring_position != static_cast<std::uint8_t>(expanded & 31U) ||
        snapshot.six_step == 0U || snapshot.six_step > 6U ||
        expanded % 6U != 6U - snapshot.six_step ||
        snapshot.output_frames != groups * 7U) {
        return false;
    }
    source_rate_hz_ = snapshot.source_rate_hz;
    input_frames_ = snapshot.input_frames;
    output_frames_ = snapshot.output_frames;
    ring_ = snapshot.ring;
    ring_position_ = snapshot.ring_position;
    six_step_ = snapshot.six_step;
    finished_ = snapshot.finished;
    return true;
}

namespace {

constexpr std::array<std::array<std::int16_t, 29U>, 7U> kXaZigzag = {{
    {{0,0,0,0,0,-2,10,-34,65,-84,52,9,-266,1024,-2680,9036,26516,-6016,3021,-1571,848,-365,107,10,-16,17,-8,3,-1}},
    {{0,0,0,-2,0,3,-19,60,-75,162,-227,306,-67,-615,3229,29883,-4532,2488,-1471,882,-424,166,-27,5,6,-8,3,-1,0}},
    {{0,0,-1,3,-2,-5,31,-74,179,-402,689,-926,1272,-1446,31033,-1446,1272,-926,689,-402,179,-74,31,-5,-2,3,-1,0,0}},
    {{0,-1,3,-8,6,5,-27,166,-424,882,-1471,2488,-4532,29883,3229,-615,-67,306,-227,162,-75,60,-19,3,0,-2,0,0,0}},
    {{-1,3,-8,17,-16,10,107,-365,848,-1571,3021,-6016,26516,9036,-2680,1024,-266,9,52,-84,65,-34,10,-1,3,1,0,0,0}},
    {{2,-8,16,-35,43,26,-235,635,-1352,2810,-5882,21472,15367,-4681,2062,-839,347,-68,-23,70,-35,17,-5,0,0,0,0,0,0}},
    {{-5,17,-35,70,-23,-68,347,-839,2062,-4681,15367,21472,-5882,2810,-1352,635,-235,26,43,-35,16,-8,2,0,0,0,0,0,0}},
}};

} // namespace

StereoSample XaRateConverter::interpolate_phase(std::size_t phase) const noexcept {
    std::int64_t left = 0;
    std::int64_t right = 0;
    for (std::size_t tap = 0U; tap < kXaZigzag[phase].size(); ++tap) {
        const std::size_t index =
            (static_cast<std::size_t>(ring_position_) + 31U - tap) & 31U;
        const std::int64_t coefficient = kXaZigzag[phase][tap];
        left += static_cast<std::int64_t>(ring_[index].left) * coefficient / 0x8000;
        right += static_cast<std::int64_t>(ring_[index].right) * coefficient / 0x8000;
    }
    return StereoSample{clamp_sample(left), clamp_sample(right)};
}

void XaRateConverter::push_37800_sample(const StereoSample& sample,
                                        std::span<StereoSample> output,
                                        std::size_t& produced) noexcept {
    ring_[ring_position_] = sample;
    ring_position_ = static_cast<std::uint8_t>(
        (static_cast<std::size_t>(ring_position_) + 1U) & 31U);
    --six_step_;
    if (six_step_ != 0U) {
        return;
    }
    six_step_ = 6U;
    for (std::size_t phase = 0U; phase < kXaZigzag.size(); ++phase) {
        output[produced++] = interpolate_phase(phase);
    }
}

Result<std::size_t> XaRateConverter::push(std::span<const StereoSample> input,
                                          std::span<StereoSample> output,
                                          bool end_of_stream) {
    if (finished_) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::ConverterFinished, "XA rate converter is already finished"));
    }
    if (input.size() > std::numeric_limits<std::uint64_t>::max() - input_frames_) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::Overflow, "XA converter input frame count overflow"));
    }
    const std::uint64_t expansion = source_rate_hz_ == XaRateHalfHz ? 2U : 1U;
    if (input.size() > std::numeric_limits<std::uint64_t>::max() / expansion) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::Overflow, "XA converter timeline overflow"));
    }
    const std::uint64_t pending = 6U - six_step_;
    const std::uint64_t expanded =
        static_cast<std::uint64_t>(input.size()) * expansion;
    const std::uint64_t groups = (pending + expanded) / 6U;
    if (groups > std::numeric_limits<std::uint64_t>::max() / 7U) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::Overflow, "XA converter output count overflow"));
    }
    const std::uint64_t required_outputs = groups * 7U;
    if (required_outputs > output.size() ||
        required_outputs > std::numeric_limits<std::uint64_t>::max() - output_frames_) {
        return Result<std::size_t>::failure(
            make_error(ErrorCode::OutputTooSmall, "XA converter output span is too small"));
    }

    std::size_t produced = 0U;
    for (const StereoSample& sample : input) {
        push_37800_sample(sample, output, produced);
        if (expansion == 2U) {
            push_37800_sample(sample, output, produced);
        }
    }
    input_frames_ += static_cast<std::uint64_t>(input.size());
    output_frames_ += static_cast<std::uint64_t>(produced);
    if (end_of_stream) {
        finished_ = true;
    }
    return Result<std::size_t>::success(produced);
}

} // namespace clean_aot::media
