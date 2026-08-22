#include "clean_aot/runtime/native_dispatch.hpp"

#include <array>

namespace {
bool target(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.gpr[2] = 0xCF1U;
    state.pc += 4U;
    return true;
}
}

int main() {
    using namespace clean_aot::runtime;
    constexpr std::string_view source =
        "1111111111111111111111111111111111111111111111111111111111111111";
    constexpr std::string_view destination =
        "2222222222222222222222222222222222222222222222222222222222222222";
    const std::array<std::uint32_t, 1U> roots{0x2000U};
    const std::array entries{GeneratedRegionEntry{0x2000U, target, 1U}};
    const GeneratedExecutableBinding binding{
        destination, destination, 0x2000U, 0x2010ULL, roots, entries};
    const std::array targets{GeneratedCfiTarget{destination, 0x2000U}};
    const std::array policies{GeneratedCfiSite{source, 0x1000U, targets}};
    GeneratedCfiDispatcher dispatcher;
    GeneratedCpuState state{};
    state.pc = 0x2000U;
    if (dispatcher.dispatch_transfer(source, 0x1000U, destination, binding,
                                     state, policies) != DispatchStatus::Executed ||
        state.pc != 0x2004U || state.gpr[2] != 0xCF1U) return 1;
    state = {}; state.pc = 0x2004U;
    if (dispatcher.dispatch_transfer(source, 0x1000U, destination, binding,
                                     state, policies) != DispatchStatus::UnknownCode) return 2;
    state.pc = 0x2000U;
    if (dispatcher.dispatch_transfer(source, 0x1004U, destination, binding,
                                     state, policies) != DispatchStatus::UnknownCode) return 3;
    const std::array duplicate_targets{
        GeneratedCfiTarget{destination, 0x2000U},
        GeneratedCfiTarget{destination, 0x2000U}};
    const std::array bad_policies{
        GeneratedCfiSite{source, 0x1000U, duplicate_targets}};
    if (dispatcher.dispatch_transfer(source, 0x1000U, destination, binding,
                                     state, bad_policies) != DispatchStatus::InvalidBinding) return 4;
    const std::array duplicate_policies{
        GeneratedCfiSite{source, 0x1000U, targets},
        GeneratedCfiSite{source, 0x1000U, targets}};
    if (dispatcher.dispatch_transfer(source, 0x1000U, destination, binding,
                                     state, duplicate_policies) != DispatchStatus::InvalidBinding) return 5;
    return 0;
}
