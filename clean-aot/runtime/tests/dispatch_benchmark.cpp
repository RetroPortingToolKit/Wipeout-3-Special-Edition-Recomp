#include "clean_aot/runtime/native_dispatch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace clean_aot::runtime;

namespace {
constexpr std::string_view kIdentity =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr std::uint32_t kLoad = 0x80010000U;
constexpr std::uint32_t kEntryCount = 135829U;
constexpr std::uint32_t kIterations = 1000000U;

bool step(GeneratedCpuState& state) noexcept { state.gpr[2] += 1U; return true; }

template<class Callback>
std::uint64_t measure(Callback&& callback) {
    const auto begin = std::chrono::steady_clock::now();
    callback();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
}
}

int main() {
    std::vector<GeneratedRegionEntry> legacy;
    std::vector<GeneratedPackedRegionEntry> packed;
    legacy.reserve(kEntryCount);
    packed.reserve(kEntryCount);
    for (std::uint32_t index = 0; index < kEntryCount; ++index) {
        const std::uint32_t pc = kLoad + index * 4U;
        legacy.push_back({pc, step, 1U});
        packed.push_back({pc, 0U, 1U});
    }
    const std::uint64_t load_end = std::uint64_t{kLoad} + std::uint64_t{kEntryCount} * 4U;
    const std::size_t page_count = static_cast<std::size_t>(
        ((load_end - 1U) >> 12U) - (kLoad >> 12U) + 1U);
    std::vector<GeneratedDispatchPage> pages(page_count);
    std::uint32_t cursor = 0U;
    for (std::size_t page = 0; page < pages.size(); ++page) {
        const std::uint32_t begin = cursor;
        while (cursor < kEntryCount &&
               (packed[cursor].pc >> 12U) == (kLoad >> 12U) + page) ++cursor;
        pages[page] = {begin, cursor};
    }
    const std::array roots{kLoad};
    const std::array functions{GeneratedRegionFunction{step}};
    const GeneratedExecutableBinding legacy_binding{kIdentity, kIdentity, kLoad, load_end,
                                                     roots, legacy};
    const GeneratedExecutableBinding packed_binding{kIdentity, kIdentity, kLoad, load_end,
                                                     roots, {}, functions, packed, pages};
    GeneratedDispatcher legacy_dispatcher;
    GeneratedDispatcher packed_dispatcher;
    const auto legacy_bind_ns = measure([&] { (void)legacy_dispatcher.bind(kIdentity, legacy_binding); });
    const auto packed_bind_ns = measure([&] { (void)packed_dispatcher.bind(kIdentity, packed_binding); });
    GeneratedCpuState legacy_state{};
    GeneratedCpuState packed_state{};
    std::uint64_t checksum = 0U;
    const auto run = [&](GeneratedDispatcher& dispatcher, GeneratedCpuState& state) {
        for (std::uint32_t index = 0; index < kIterations; ++index) {
            state.pc = kLoad + ((index * 7919U) % kEntryCount) * 4U;
            if (dispatcher.dispatch(state) != DispatchStatus::Executed) std::abort();
        }
        checksum += state.gpr[2];
    };
    const auto legacy_dispatch_ns = measure([&] { run(legacy_dispatcher, legacy_state); });
    const auto packed_dispatch_ns = measure([&] { run(packed_dispatcher, packed_state); });
    if (legacy_state.gpr != packed_state.gpr || checksum != 2ULL * kIterations) return 2;
    const std::array epoch_candidates{GeneratedEpochBinding{kIdentity, &packed_binding}};
    GeneratedEpochDispatcher epoch_dispatcher;
    // These paths intentionally reauthenticate the complete binding. Keep the
    // workload bounded while making that one-time/control-transfer cost visible.
    constexpr std::uint32_t kPolicyIterations = 100U;
    const auto epoch_select_ns = measure([&] {
        for (std::uint32_t index = 0; index < kPolicyIterations; ++index) {
            if (epoch_dispatcher.select(kIdentity, epoch_candidates) != DispatchStatus::Bound)
                std::abort();
        }
    });
    const std::array cfi_targets{GeneratedCfiTarget{kIdentity, kLoad}};
    const std::array cfi_sites{GeneratedCfiSite{kIdentity, kLoad + 4U, cfi_targets}};
    GeneratedCfiDispatcher cfi_dispatcher;
    GeneratedCpuState cfi_state{};
    const auto cfi_dispatch_ns = measure([&] {
        for (std::uint32_t index = 0; index < kPolicyIterations; ++index) {
            cfi_state.pc = kLoad;
            if (cfi_dispatcher.dispatch_transfer(kIdentity, kLoad + 4U, kIdentity,
                    packed_binding, cfi_state, cfi_sites) != DispatchStatus::Executed)
                std::abort();
        }
    });
    const auto packed_per_dispatch_ns = packed_dispatch_ns / kIterations;
    std::cout << "{\"schema\":\"clean-aot-dispatch-benchmark-v1\","
              << "\"entries\":" << kEntryCount << ','
              << "\"iterations\":" << kIterations << ','
              << "\"legacy_entry_bytes\":" << sizeof(GeneratedRegionEntry) << ','
              << "\"packed_entry_bytes\":" << sizeof(GeneratedPackedRegionEntry) << ','
              << "\"page_count\":" << page_count << ','
              << "\"legacy_bind_ns\":" << legacy_bind_ns << ','
              << "\"packed_bind_ns\":" << packed_bind_ns << ','
              << "\"legacy_dispatch_ns\":" << legacy_dispatch_ns << ','
              << "\"packed_dispatch_ns\":" << packed_dispatch_ns << ','
              << "\"epoch_iterations\":" << kPolicyIterations << ','
              << "\"epoch_select_ns\":" << epoch_select_ns << ','
              << "\"cfi_iterations\":" << kPolicyIterations << ','
              << "\"cfi_dispatch_ns\":" << cfi_dispatch_ns << ','
              << "\"dispatches_per_10ms\":"
              << (packed_per_dispatch_ns == 0U ? 0U : 10000000U / packed_per_dispatch_ns) << ','
              << "\"checksum\":" << checksum << "}\n";
    return 0;
}
