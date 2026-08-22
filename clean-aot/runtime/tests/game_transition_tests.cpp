#include "clean_aot/runtime/runtime.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
bool game_step(clean_aot::runtime::GeneratedCpuState& state) noexcept {
    state.gpr[7] = 0x47534d45U; state.pc = 0x1004U; return true;
}
std::unique_ptr<clean_aot::runtime::Runtime> runtime() {
    static const std::vector<std::uint8_t> bios(clean_aot::memory::kBiosBytes, 0U);
    clean_aot::runtime::Configuration configuration{}; configuration.bios = bios;
    return std::make_unique<clean_aot::runtime::Runtime>(configuration);
}
}

int main() {
    using namespace clean_aot::runtime;
    constexpr AccessContext access{};
    constexpr std::string_view identity =
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    const std::array<std::uint32_t, 1U> roots{0x1000U};
    const std::array entries{GeneratedRegionEntry{0x1000U, game_step, 1U}};
    const GeneratedExecutableBinding binding{identity,
        "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a",
        0x1000U, 0x1004U, roots, entries};
    auto valid = runtime();
    for (std::uint32_t index = 0U; index < 4U; ++index)
        if (!valid->write8(access, 0x1000U + index, static_cast<std::uint8_t>(index + 1U)).ok()) return 1;
    if (valid->bind_authenticated_resident_executable(identity, 0x1000U, binding) != DispatchStatus::Bound ||
        valid->dispatch_selected(false) != DispatchStatus::Executed) return 2;
    auto mutated = runtime();
    for (std::uint32_t index = 0U; index < 4U; ++index)
        if (!mutated->write8(access, 0x1000U + index, static_cast<std::uint8_t>(index + 1U)).ok()) return 3;
    if (!mutated->write8(access, 0x1003U, 5U).ok() ||
        mutated->bind_authenticated_resident_executable(identity, 0x1000U, binding) != DispatchStatus::ExecutableHashMismatch ||
        mutated->bind_authenticated_resident_executable(identity, 0x1004U, binding) != DispatchStatus::UnknownCode) return 4;
    std::cout << "synthetic BIOS-to-game resident transition: pass\n";
    return 0;
}
