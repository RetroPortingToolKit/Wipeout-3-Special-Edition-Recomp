#include "clean_aot/runtime/game_profile_trace.hpp"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <new>
#include <sstream>

namespace clean_aot::runtime {
namespace {

constexpr std::uint64_t kHashOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kHashPrime = 1'099'511'628'211ULL;

bool sha256(std::string_view value) noexcept {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool safe_id(std::string_view value) noexcept {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '_' || c == '-';
    });
}

void hash_value(std::uint64_t& digest, std::uint64_t value) noexcept {
    if (digest == 0U) digest = kHashOffset;
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
        digest ^= static_cast<std::uint8_t>(value >> shift);
        digest *= kHashPrime;
    }
}

void write_hash(std::ostringstream& out, std::uint64_t value) {
    out << '"' << std::hex << std::setw(16) << std::setfill('0') << value
        << std::dec << std::setfill(' ') << '"';
}

} // namespace

GameProfileTrace::GameProfileTrace(
    std::span<const GameUpdateCandidate> candidates,
    GameProfileEvidence evidence,
    std::size_t frame_capacity) noexcept
    : frame_capacity_(std::min(frame_capacity, kMaximumFrames)), evidence_(evidence) {
    if (candidates.empty() || candidates.size() > kMaximumCandidates ||
        frame_capacity_ == 0U) return;
    frames_.reset(new (std::nothrow) Frame[frame_capacity_]);
    if (!frames_) return;
    std::string_view previous{};
    for (const auto& candidate : candidates) {
        if (!safe_id(candidate.id) || !sha256(candidate.identity_sha256) ||
            candidate.entry_pc % 4U != 0U || candidate.exit_pc % 4U != 0U ||
            (!previous.empty() && candidate.id <= previous)) return;
        previous = candidate.id;
    }
    for (std::size_t left = 0; left < candidates.size(); ++left) {
        for (std::size_t right = left + 1U; right < candidates.size(); ++right) {
            if (candidates[left].identity_sha256 == candidates[right].identity_sha256 &&
                (candidates[left].entry_pc == candidates[right].entry_pc ||
                 candidates[left].exit_pc == candidates[right].exit_pc)) return;
        }
    }
    for (const auto& candidate : candidates) candidates_[candidate_count_++] = candidate;
    valid_ = true;
}

void GameProfileTrace::invalidate_observation() noexcept {
    valid_ = false;
    ++dropped_observations_;
}

void GameProfileTrace::begin_frame(std::uint64_t frame_index) noexcept {
    if (!valid_ || finalized_) return;
    if (frame_open_ || frame_count_ >= frame_capacity_ ||
        (frame_count_ != 0U && frame_index != frames_[frame_count_ - 1U].frame_index + 1U)) {
        invalidate_observation();
        return;
    }
    frames_[frame_count_] = Frame{};
    frames_[frame_count_].frame_index = frame_index;
    frame_open_ = true;
}

void GameProfileTrace::record_candidate(
    std::string_view identity_sha256, std::uint32_t pc,
    std::uint64_t state_hash, bool entry) noexcept {
    if (!valid_ || finalized_) return;
    if (!frame_open_) {
        invalidate_observation();
        return;
    }
    for (std::size_t index = 0; index < candidate_count_; ++index) {
        const auto& candidate = candidates_[index];
        const std::uint32_t expected = entry ? candidate.entry_pc : candidate.exit_pc;
        if (candidate.identity_sha256 == identity_sha256 && expected == pc) {
            auto& observation = frames_[frame_count_].candidates[index];
            auto& count = entry ? observation.entry_count : observation.exit_count;
            auto& digest = entry ? observation.entry_hash_digest : observation.exit_hash_digest;
            if (count == UINT32_MAX) {
                invalidate_observation();
                return;
            }
            ++count;
            hash_value(digest, state_hash);
            return;
        }
    }
    invalidate_observation();
}

void GameProfileTrace::record_candidate_entry(
    std::string_view identity_sha256, std::uint32_t pc,
    std::uint64_t state_hash) noexcept {
    record_candidate(identity_sha256, pc, state_hash, true);
}

void GameProfileTrace::record_candidate_exit(
    std::string_view identity_sha256, std::uint32_t pc,
    std::uint64_t state_hash) noexcept {
    record_candidate(identity_sha256, pc, state_hash, false);
}

void GameProfileTrace::record_presentation(std::uint64_t state_hash) noexcept {
    if (!valid_ || finalized_) return;
    if (!frame_open_) {
        invalidate_observation();
        return;
    }
    auto& frame = frames_[frame_count_];
    if (frame.presentation_count == UINT32_MAX) {
        invalidate_observation();
        return;
    }
    ++frame.presentation_count;
    const auto begin = frame.presentation_hashes.begin();
    const auto end = begin + frame.unique_presentation_count;
    if (std::find(begin, end, state_hash) != end) return;
    if (frame.unique_presentation_count >= kMaximumUniquePresentations) {
        invalidate_observation();
        return;
    }
    frame.presentation_hashes[frame.unique_presentation_count++] = state_hash;
}

bool GameProfileTrace::valid_timing(
    const clean_aot_machine_frame_receipt& timing) const noexcept {
    if (timing.frame_index != frames_[frame_count_].frame_index ||
        timing.vblank_hz != 100U || timing.timebase_ticks != 1U ||
        timing.cpu_multiplier_numerator == 0U ||
        timing.cpu_multiplier_denominator == 0U || timing.spu_samples != 441U) return false;
    const std::uint64_t divisor = std::gcd(timing.cpu_multiplier_numerator,
                                           timing.cpu_multiplier_denominator);
    const std::uint64_t whole = timing.cpu_multiplier_numerator /
                                timing.cpu_multiplier_denominator;
    const std::uint64_t remainder = timing.cpu_multiplier_numerator %
                                    timing.cpu_multiplier_denominator;
    if (divisor != 1U || whole > 10U || (whole == 10U && remainder != 0U))
        return false;
    constexpr std::uint64_t native_cpu = 338'688U;
    const std::uint64_t cycle_divisor = std::gcd(timing.cpu_cycles, native_cpu);
    if (timing.cpu_cycles / cycle_divisor != timing.cpu_multiplier_numerator ||
        native_cpu / cycle_divisor != timing.cpu_multiplier_denominator ||
        timing.cpu_retirement_total != timing.total_cpu_cycles) return false;
    const std::uint64_t expected_cd = timing.frame_index % 4U == 0U ? 0U : 1U;
    if (timing.cdda_sectors != expected_cd) return false;
    if (frame_count_ != 0U) {
        const auto& previous = frames_[frame_count_ - 1U].timing;
        if (timing.total_cpu_cycles < previous.total_cpu_cycles ||
            timing.total_spu_samples < previous.total_spu_samples ||
            timing.total_cdda_sectors < previous.total_cdda_sectors ||
            timing.total_cpu_cycles - previous.total_cpu_cycles != timing.cpu_cycles ||
            timing.total_spu_samples - previous.total_spu_samples != timing.spu_samples ||
            timing.total_cdda_sectors - previous.total_cdda_sectors != timing.cdda_sectors)
            return false;
    }
    return true;
}

void GameProfileTrace::end_frame(
    const clean_aot_machine_frame_receipt& timing) noexcept {
    if (!valid_ || finalized_) return;
    if (!frame_open_ || !valid_timing(timing)) {
        invalidate_observation();
        frame_open_ = false;
        return;
    }
    frames_[frame_count_].timing = timing;
    ++frame_count_;
    frame_open_ = false;
}

void GameProfileTrace::finish() noexcept {
    if (frame_open_) invalidate_observation();
    frame_open_ = false;
    finalized_ = true;
}

bool GameProfileTrace::complete() const noexcept {
    return valid_ && finalized_ && !frame_open_ && frame_count_ != 0U &&
           dropped_observations_ == 0U;
}

std::string GameProfileTrace::canonical_receipt() const {
    if (!finalized_ || candidate_count_ == 0U) return {};
    const bool capture_complete = complete();
    std::ostringstream out;
    out << "{\"candidates\":[";
    for (std::size_t candidate = 0; candidate < candidate_count_; ++candidate) {
        if (candidate != 0U) out << ',';
        std::size_t exact_frames = 0U;
        std::size_t observed_frames = 0U;
        for (std::size_t frame = 0; frame < frame_count_; ++frame) {
            const auto& row = frames_[frame].candidates[candidate];
            if (row.entry_count != 0U || row.exit_count != 0U) ++observed_frames;
            if (row.entry_count == 1U && row.exit_count == 1U) ++exact_frames;
        }
        const bool observed_once = capture_complete && frame_count_ >= 2U &&
                                   exact_frames == frame_count_;
        const bool verified = observed_once &&
                              evidence_ == GameProfileEvidence::AuthenticatedGame;
        const auto& row = candidates_[candidate];
        out << "{\"entry_pc\":" << row.entry_pc << ",\"exact_frame_count\":"
            << exact_frames << ",\"exit_pc\":" << row.exit_pc << ",\"id\":\""
            << row.id << "\",\"identity_sha256\":\"" << row.identity_sha256
            << "\",\"observed_frame_count\":" << observed_frames
            << ",\"observed_once_per_vblank\":" << (observed_once ? "true" : "false")
            << ",\"verified_once_per_vblank\":" << (verified ? "true" : "false") << '}';
    }
    out << "],\"capture\":{\"complete\":" << (capture_complete ? "true" : "false")
        << ",\"dropped_observations\":" << dropped_observations_
        << ",\"evidence\":\""
        << (evidence_ == GameProfileEvidence::AuthenticatedGame ? "authenticated-game" : "synthetic")
        << "\",\"frame_count\":" << frame_count_ << "},\"frames\":[";
    for (std::size_t frame_index = 0; frame_index < frame_count_; ++frame_index) {
        if (frame_index != 0U) out << ',';
        const auto& frame = frames_[frame_index];
        out << "{\"candidates\":[";
        for (std::size_t candidate = 0; candidate < candidate_count_; ++candidate) {
            if (candidate != 0U) out << ',';
            const auto& observation = frame.candidates[candidate];
            out << "{\"entry_count\":" << observation.entry_count
                << ",\"entry_state_hash_digest\":";
            write_hash(out, observation.entry_hash_digest);
            out << ",\"exit_count\":" << observation.exit_count
                << ",\"exit_state_hash_digest\":";
            write_hash(out, observation.exit_hash_digest);
            out << ",\"id\":\"" << candidates_[candidate].id << "\"}";
        }
        const auto& timing = frame.timing;
        out << "],\"cpu_multiplier_denominator\":" << timing.cpu_multiplier_denominator
            << ",\"cpu_multiplier_numerator\":" << timing.cpu_multiplier_numerator
            << ",\"cpu_retirement\":" << timing.cpu_cycles
            << ",\"cpu_retirement_total\":" << timing.cpu_retirement_total
            << ",\"frame_index\":" << frame.frame_index
            << ",\"native_cd_sectors\":" << timing.cdda_sectors
            << ",\"native_cd_total\":" << timing.total_cdda_sectors
            << ",\"native_spu_samples\":" << timing.spu_samples
            << ",\"native_spu_total\":" << timing.total_spu_samples
            << ",\"presentation_count\":" << frame.presentation_count
            << ",\"presentation_unique_state_count\":"
            << frame.unique_presentation_count << '}';
    }
    out << "],\"schema\":\"clean-aot-game-profile-trace-v1\"}\n";
    return out.str();
}

} // namespace clean_aot::runtime
