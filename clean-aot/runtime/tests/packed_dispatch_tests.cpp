#include "clean_aot/runtime/native_dispatch.hpp"

#include <array>
#include <cstdint>
#include <iostream>

using namespace clean_aot::runtime;

namespace {
constexpr std::string_view kIdentity =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

bool step_a(GeneratedCpuState& state) noexcept { state.gpr[2] += 7U; state.pc += 4U; return true; }
bool step_b(GeneratedCpuState& state) noexcept { state.gpr[3] ^= 0x55aaU; state.pc += 8U; return true; }

int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "failed line " << __LINE__ << '\n'; ++failures; } } while (false)
}

int main() {
    CHECK(sizeof(GeneratedPackedRegionEntry) == 8U);
    CHECK(sizeof(GeneratedDispatchPage) == 8U);
    const std::array roots{0x1000U, 0x2000U};
    const std::array legacy_entries{
        GeneratedRegionEntry{0x1000U, step_a, 3U},
        GeneratedRegionEntry{0x1004U, step_b, 2U},
        GeneratedRegionEntry{0x2000U, step_a, 1U},
    };
    const std::array functions{GeneratedRegionFunction{step_a}, GeneratedRegionFunction{step_b}};
    const std::array packed_entries{
        GeneratedPackedRegionEntry{0x1000U, 0U, 3U},
        GeneratedPackedRegionEntry{0x1004U, 1U, 2U},
        GeneratedPackedRegionEntry{0x2000U, 0U, 1U},
    };
    const std::array pages{
        GeneratedDispatchPage{0U, 2U}, GeneratedDispatchPage{2U, 3U}};
    const GeneratedExecutableBinding legacy{kIdentity, kIdentity, 0x1000U, 0x3000ULL,
                                             roots, legacy_entries};
    const GeneratedExecutableBinding packed{kIdentity, kIdentity, 0x1000U, 0x3000ULL,
                                             roots, {}, functions, packed_entries, pages};
    GeneratedDispatcher old_dispatcher;
    GeneratedDispatcher new_dispatcher;
    CHECK(old_dispatcher.bind(kIdentity, legacy) == DispatchStatus::Bound);
    CHECK(new_dispatcher.bind(kIdentity, packed) == DispatchStatus::Bound);
    for (const std::uint32_t pc : {0x1000U, 0x1004U, 0x1008U, 0x1ffCU, 0x2000U}) {
        GeneratedCpuState old_state{};
        GeneratedCpuState new_state{};
        old_state.pc = pc;
        new_state.pc = pc;
        CHECK(old_dispatcher.dispatch(old_state) == new_dispatcher.dispatch(new_state));
        CHECK(old_state.pc == new_state.pc);
        CHECK(old_state.gpr == new_state.gpr);
        CHECK(old_dispatcher.last_instruction_count() == new_dispatcher.last_instruction_count());
    }

    auto bad_entry = packed_entries;
    bad_entry[1].region_index = 2U;
    auto bad = packed;
    bad.packed_entries = bad_entry;
    CHECK(new_dispatcher.bind(kIdentity, bad) == DispatchStatus::InvalidBinding);
    auto bad_pages = pages;
    bad_pages[0].end_index = 1U;
    bad.dispatch_pages = bad_pages;
    bad.packed_entries = packed_entries;
    CHECK(new_dispatcher.bind(kIdentity, bad) == DispatchStatus::InvalidBinding);
    bad = packed;
    bad.entries = legacy_entries;
    CHECK(new_dispatcher.bind(kIdentity, bad) == DispatchStatus::InvalidBinding);

    return failures == 0 ? 0 : 1;
}
