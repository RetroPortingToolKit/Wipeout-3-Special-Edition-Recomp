#include "clean_aot/runtime/target_trace.hpp"

#include <array>
#include <string>

namespace {
using namespace clean_aot::runtime;
constexpr std::string_view kMain =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::string_view kNfe =
    "2222222222222222222222222222222222222222222222222222222222222222";
const std::array kImages{
    RuntimeTraceImage{"callback-epoch", "raw-payload", kNfe, kNfe, 0x1000U, 0x1010ULL},
    RuntimeTraceImage{"main", "raw-payload", kMain, kMain, 0x1000U, 0x1010ULL},
};

bool generated_target(GeneratedCpuState& state) noexcept {
    state.pc += 4U;
    return true;
}

std::string replay() {
    RuntimeTargetTrace trace{kImages, 8U, 40U};
    const std::array<std::uint32_t, 1U> roots{0x1004U};
    const std::array entries{GeneratedRegionEntry{0x1004U, generated_target, 1U}};
    const GeneratedExecutableBinding binding{kNfe, kNfe, 0x1000U, 0x1010ULL,
                                               roots, entries};
    const std::array candidates{GeneratedEpochBinding{kNfe, &binding}};
    GeneratedEpochDispatcher epoch;
    epoch.set_trace(&trace);
    (void)epoch.select(kNfe, candidates);
    const std::array targets{GeneratedCfiTarget{kNfe, 0x1004U}};
    const std::array policies{GeneratedCfiSite{kMain, 0x1000U, targets}};
    GeneratedCfiDispatcher cfi;
    cfi.set_trace(&trace);
    GeneratedCpuState state{};
    state.pc = 0x1004U;
    (void)cfi.dispatch_transfer(kMain, 0x1000U, kNfe, binding, state, policies,
                                RuntimeTraceTransfer::IndirectCall);
    state.pc = 0x100CU;
    (void)cfi.dispatch_transfer(kMain, 0x1000U, kNfe, binding, state, policies,
                                RuntimeTraceTransfer::IndirectCall);
    trace.finish();
    return trace.canonical_receipt();
}
}

int main() {
    const std::string first = replay();
    if (first.empty() || first != replay()) return 1;
    if (first.find("\"event_count\":1,\"first_sequence\":40,\"last_sequence\":40") ==
        std::string::npos) return 2;
    if (first.find("\"status\":\"unknown-code\"") == std::string::npos ||
        first.find("\"active_epoch_identity_sha256\":\"2222") == std::string::npos)
        return 3;

    RuntimeTargetTrace overflow{kImages, 1U, 7U};
    overflow.record_epoch(kMain, DispatchStatus::Bound);
    overflow.record_dispatch(kMain, 0x1000U, 0x1004U, DispatchStatus::Executed);
    overflow.finish();
    if (overflow.complete() || overflow.dropped_events() != 1U ||
        overflow.canonical_receipt().find("\"complete\":false") == std::string::npos)
        return 4;

    auto bad_images = kImages;
    bad_images[1].variant_sha256 = "tampered";
    RuntimeTargetTrace tampered{bad_images};
    tampered.finish();
    if (tampered.valid() || !tampered.canonical_receipt().empty()) return 5;
    return 0;
}
