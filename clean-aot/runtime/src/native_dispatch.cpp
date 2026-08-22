#include "clean_aot/runtime/native_dispatch.hpp"

#include <algorithm>

namespace clean_aot::runtime {
namespace {

[[nodiscard]] bool is_sha256(std::string_view value) noexcept {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

} // namespace

DispatchStatus GeneratedDispatcher::bind(
    std::string_view selected_variant_identity_sha256,
    const GeneratedExecutableBinding& binding) noexcept {
    entries_ = {};
    if (!is_sha256(selected_variant_identity_sha256) ||
        selected_variant_identity_sha256 != binding.variant_identity_sha256) {
        return DispatchStatus::IdentityMismatch;
    }
    if (binding.load_end_exclusive <= binding.load_start ||
        binding.load_end_exclusive > (std::uint64_t{1} << 32U) ||
        binding.entries.empty()) {
        return DispatchStatus::InvalidBinding;
    }
    std::uint64_t previous = 0;
    bool first = true;
    for (const GeneratedRegionEntry& entry : binding.entries) {
        if (entry.function == nullptr || (entry.pc % 4U) != 0U ||
            entry.pc < binding.load_start || entry.pc >= binding.load_end_exclusive ||
            (!first && entry.pc <= previous)) {
            return DispatchStatus::InvalidBinding;
        }
        previous = entry.pc;
        first = false;
    }
    load_start_ = binding.load_start;
    load_end_exclusive_ = binding.load_end_exclusive;
    entries_ = binding.entries;
    return DispatchStatus::Bound;
}

DispatchStatus GeneratedDispatcher::dispatch(std::uint32_t& pc,
                                             bool branch_taken) const noexcept {
    if (entries_.empty() || pc < load_start_ || pc >= load_end_exclusive_) {
        return DispatchStatus::UnknownCode;
    }
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), pc,
        [](const GeneratedRegionEntry& entry, std::uint32_t address) {
            return entry.pc < address;
        });
    if (found == entries_.end() || found->pc != pc) {
        return DispatchStatus::UnknownCode;
    }
    const std::uint32_t original_pc = pc;
    if (!found->function(pc, branch_taken) || pc == original_pc) {
        pc = original_pc;
        return DispatchStatus::UnknownCode;
    }
    return DispatchStatus::Executed;
}

} // namespace clean_aot::runtime
