#include "clean_aot/runtime/game_profile_trace.hpp"
#include "machine.hpp"

#include <array>
#include <string>

namespace {
using namespace clean_aot::runtime;
constexpr std::string_view kIdentity =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::array kCandidates{
    GameUpdateCandidate{"candidate-a", kIdentity, 0x80010000U, 0x80010080U}};

std::string replay(bool repeated_update = false) {
    clean_aot::machine::Machine machine{clean_aot::machine::VBlankRate::Hz100, 9U, 1U};
    GameProfileTrace trace{kCandidates, GameProfileEvidence::Synthetic, 8U};
    for (std::uint64_t frame = 0; frame < 4U; ++frame) {
        trace.begin_frame(frame);
        trace.record_candidate_entry(kIdentity, 0x80010000U, 0x1000U + frame);
        if (repeated_update && frame == 2U)
            trace.record_candidate_entry(kIdentity, 0x80010000U, 0x2000U + frame);
        trace.record_candidate_exit(kIdentity, 0x80010080U, 0x3000U + frame);
        trace.record_presentation(0x3000U + frame);
        trace.record_presentation(0x3000U + frame);
        trace.end_frame(machine.step_frame());
    }
    trace.finish();
    return trace.canonical_receipt();
}
}

int main() {
    const std::string receipt = replay();
    if (receipt.empty() || receipt != replay()) return 1;
    if (receipt.find("\"schema\":\"clean-aot-game-profile-trace-v1\"") ==
            std::string::npos ||
        receipt.find("\"observed_once_per_vblank\":true") == std::string::npos ||
        receipt.find("\"verified_once_per_vblank\":false") == std::string::npos ||
        receipt.find("\"cpu_multiplier_numerator\":9") == std::string::npos ||
        receipt.find("\"native_spu_samples\":441") == std::string::npos ||
        receipt.find("\"presentation_count\":2") == std::string::npos ||
        receipt.find("\"presentation_unique_state_count\":1") == std::string::npos)
        return 2;

    const std::string repeated = replay(true);
    if (repeated.find("\"observed_once_per_vblank\":false") == std::string::npos ||
        repeated.find("\"entry_count\":2") == std::string::npos)
        return 3;

    clean_aot::machine::Machine machine{clean_aot::machine::VBlankRate::Hz100, 2U, 1U};
    GameProfileTrace bad_timing{kCandidates, GameProfileEvidence::Synthetic};
    bad_timing.begin_frame(0U);
    bad_timing.record_candidate_entry(kIdentity, 0x80010000U, 1U);
    bad_timing.record_candidate_exit(kIdentity, 0x80010080U, 2U);
    auto timing = machine.step_frame();
    timing.spu_samples = 440U;
    bad_timing.end_frame(timing);
    bad_timing.finish();
    if (bad_timing.complete() ||
        bad_timing.canonical_receipt().find("\"complete\":false") == std::string::npos)
        return 4;

    GameProfileTrace wrong_identity{kCandidates, GameProfileEvidence::Synthetic};
    wrong_identity.begin_frame(0U);
    wrong_identity.record_candidate_entry(
        "2222222222222222222222222222222222222222222222222222222222222222",
        0x80010000U, 1U);
    wrong_identity.finish();
    if (wrong_identity.complete() || wrong_identity.dropped_observations() == 0U)
        return 5;

    const std::array unsorted{
        GameUpdateCandidate{"z", kIdentity, 0x1000U, 0x1004U},
        GameUpdateCandidate{"a", kIdentity, 0x2000U, 0x2004U}};
    GameProfileTrace malformed{unsorted, GameProfileEvidence::Synthetic};
    malformed.finish();
    if (malformed.valid() || !malformed.canonical_receipt().empty()) return 6;

    clean_aot::machine::Machine capacity_machine{clean_aot::machine::VBlankRate::Hz100};
    GameProfileTrace overflow{kCandidates, GameProfileEvidence::Synthetic, 1U};
    overflow.begin_frame(0U);
    overflow.record_candidate_entry(kIdentity, 0x80010000U, 1U);
    overflow.record_candidate_exit(kIdentity, 0x80010080U, 2U);
    overflow.end_frame(capacity_machine.step_frame());
    overflow.begin_frame(1U);
    overflow.finish();
    if (overflow.complete() || overflow.dropped_observations() != 1U ||
        overflow.canonical_receipt().find("\"dropped_observations\":1") ==
            std::string::npos)
        return 7;
    const std::array ambiguous{
        GameUpdateCandidate{"a", kIdentity, 0x1000U, 0x1004U},
        GameUpdateCandidate{"b", kIdentity, 0x1000U, 0x2004U}};
    GameProfileTrace ambiguous_trace{ambiguous, GameProfileEvidence::Synthetic};
    ambiguous_trace.finish();
    if (ambiguous_trace.valid() || !ambiguous_trace.canonical_receipt().empty()) return 8;
    return 0;
}
