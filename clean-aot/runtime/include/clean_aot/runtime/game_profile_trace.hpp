#pragma once

#include "machine_c.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace clean_aot::runtime {

enum class GameProfileEvidence : std::uint8_t { Synthetic, AuthenticatedGame };

struct GameUpdateCandidate final {
    std::string_view id;
    std::string_view identity_sha256;
    std::uint32_t entry_pc = 0;
    std::uint32_t exit_pc = 0;
};

class GameProfileTrace final {
public:
    static constexpr std::size_t kMaximumCandidates = 8U;
    static constexpr std::size_t kMaximumFrames = 1024U;
    static constexpr std::size_t kMaximumUniquePresentations = 16U;

    GameProfileTrace(std::span<const GameUpdateCandidate> candidates,
                     GameProfileEvidence evidence,
                     std::size_t frame_capacity = kMaximumFrames) noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    void begin_frame(std::uint64_t frame_index) noexcept;
    // state_hash is a caller-defined canonical hash of the selected
    // architectural/game state; the recorder never reads or decodes guest bytes.
    void record_candidate_entry(std::string_view identity_sha256,
                                std::uint32_t pc,
                                std::uint64_t state_hash) noexcept;
    void record_candidate_exit(std::string_view identity_sha256,
                               std::uint32_t pc,
                               std::uint64_t state_hash) noexcept;
    void record_presentation(std::uint64_t state_hash) noexcept;
    void end_frame(const clean_aot_machine_frame_receipt& timing) noexcept;
    void finish() noexcept;

    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] std::uint64_t dropped_observations() const noexcept {
        return dropped_observations_;
    }
    [[nodiscard]] std::string canonical_receipt() const;

private:
    struct CandidateFrame final {
        std::uint32_t entry_count = 0;
        std::uint32_t exit_count = 0;
        std::uint64_t entry_hash_digest = 0;
        std::uint64_t exit_hash_digest = 0;
    };
    struct Frame final {
        std::uint64_t frame_index = 0;
        clean_aot_machine_frame_receipt timing{};
        std::array<CandidateFrame, kMaximumCandidates> candidates{};
        std::array<std::uint64_t, kMaximumUniquePresentations> presentation_hashes{};
        std::uint32_t presentation_count = 0;
        std::uint32_t unique_presentation_count = 0;
    };

    void record_candidate(std::string_view identity_sha256, std::uint32_t pc,
                          std::uint64_t state_hash, bool entry) noexcept;
    void invalidate_observation() noexcept;
    [[nodiscard]] bool valid_timing(
        const clean_aot_machine_frame_receipt& timing) const noexcept;

    std::array<GameUpdateCandidate, kMaximumCandidates> candidates_{};
    std::unique_ptr<Frame[]> frames_{};
    std::size_t candidate_count_ = 0;
    std::size_t frame_count_ = 0;
    std::size_t frame_capacity_ = 0;
    std::uint64_t dropped_observations_ = 0;
    GameProfileEvidence evidence_ = GameProfileEvidence::Synthetic;
    bool valid_ = false;
    bool frame_open_ = false;
    bool finalized_ = false;
};

} // namespace clean_aot::runtime
