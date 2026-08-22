#include "clean_aot/runtime/native_dispatch.hpp"

#include <algorithm>
#include <utility>

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
        !is_sha256(binding.executable_sha256) ||
        selected_variant_identity_sha256 != binding.variant_identity_sha256) {
        return DispatchStatus::IdentityMismatch;
    }
    if (binding.load_end_exclusive <= binding.load_start ||
        binding.load_end_exclusive > (std::uint64_t{1} << 32U) ||
        binding.entries.empty() || binding.dispatch_roots.empty()) {
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
    previous = 0;
    first = true;
    for (const std::uint32_t root : binding.dispatch_roots) {
        if ((root % 4U) != 0U || root < binding.load_start ||
            root >= binding.load_end_exclusive || (!first && root <= previous)) {
            return DispatchStatus::InvalidBinding;
        }
        const auto entry = std::lower_bound(
            binding.entries.begin(), binding.entries.end(), root,
            [](const GeneratedRegionEntry& candidate, std::uint32_t value) {
                return candidate.pc < value;
            });
        if (entry == binding.entries.end() || entry->pc != root) {
            return DispatchStatus::InvalidBinding;
        }
        previous = root;
        first = false;
    }
    load_start_ = binding.load_start;
    load_end_exclusive_ = binding.load_end_exclusive;
    entries_ = binding.entries;
    return DispatchStatus::Bound;
}

DispatchStatus GeneratedDispatcher::dispatch(GeneratedCpuState& state) const noexcept {
    last_instruction_count_ = 0U;
    std::uint32_t& pc = state.pc;
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
    const GeneratedCpuState original_state = state;
    if (!found->function(state)) {
        const bool faulted = state.fault != CpuFault::None;
        state = original_state;
        return faulted ? DispatchStatus::CpuFault : DispatchStatus::UnknownCode;
    }
    if (state.fault != CpuFault::None) {
        state = original_state;
        return DispatchStatus::CpuFault;
    }
    last_instruction_count_ = found->instruction_count;
    return DispatchStatus::Executed;
}

DispatchStatus GeneratedEpochDispatcher::select(
    std::string_view authenticated_active_identity_sha256,
    std::span<const GeneratedEpochBinding> candidates) noexcept {
    selected_ = GeneratedDispatcher{};
    if (!is_sha256(authenticated_active_identity_sha256) || candidates.empty()) {
        return DispatchStatus::IdentityMismatch;
    }
    std::string_view previous{};
    bool first = true;
    for (const auto& candidate : candidates) {
        if (!is_sha256(candidate.identity_sha256) || candidate.binding == nullptr ||
            candidate.binding->variant_identity_sha256 != candidate.identity_sha256 ||
            (!first && candidate.identity_sha256 <= previous)) {
            return DispatchStatus::InvalidBinding;
        }
        previous = candidate.identity_sha256;
        first = false;
    }
    const auto found = std::lower_bound(
        candidates.begin(), candidates.end(), authenticated_active_identity_sha256,
        [](const GeneratedEpochBinding& candidate, std::string_view identity) {
            return candidate.identity_sha256 < identity;
        });
    if (found == candidates.end() ||
        found->identity_sha256 != authenticated_active_identity_sha256) {
        return DispatchStatus::IdentityMismatch;
    }
    return selected_.bind(authenticated_active_identity_sha256, *found->binding);
}

DispatchStatus GeneratedCfiDispatcher::dispatch_transfer(
    std::string_view source_identity_sha256,
    std::uint32_t source_pc,
    std::string_view target_identity_sha256,
    const GeneratedExecutableBinding& target_binding,
    GeneratedCpuState& state,
    std::span<const GeneratedCfiSite> policies) const noexcept {
    if (!is_sha256(source_identity_sha256) || !is_sha256(target_identity_sha256) ||
        target_binding.variant_identity_sha256 != target_identity_sha256 ||
        state.pc % 4U != 0U) {
        return DispatchStatus::IdentityMismatch;
    }
    std::pair<std::string_view, std::uint32_t> previous_site{};
    bool first_site = true;
    for (const auto& policy : policies) {
        const auto key = std::pair{policy.source_identity_sha256, policy.source_pc};
        if (!is_sha256(policy.source_identity_sha256) || policy.source_pc % 4U != 0U ||
            policy.targets.empty() || (!first_site && key <= previous_site)) {
            return DispatchStatus::InvalidBinding;
        }
        previous_site = key;
        first_site = false;
    }
    const auto site = std::lower_bound(
        policies.begin(), policies.end(), std::pair{source_identity_sha256, source_pc},
        [](const GeneratedCfiSite& candidate,
           const std::pair<std::string_view, std::uint32_t>& key) {
            return std::pair{candidate.source_identity_sha256, candidate.source_pc} < key;
        });
    if (site == policies.end() || site->source_identity_sha256 != source_identity_sha256 ||
        site->source_pc != source_pc || site->targets.empty()) {
        return DispatchStatus::UnknownCode;
    }
    std::pair<std::string_view, std::uint32_t> previous{};
    bool first = true;
    bool permitted = false;
    for (const auto& target : site->targets) {
        const auto key = std::pair{target.identity_sha256, target.pc};
        if (!is_sha256(target.identity_sha256) || target.pc % 4U != 0U ||
            (!first && key <= previous)) {
            return DispatchStatus::InvalidBinding;
        }
        permitted = permitted ||
                    (target.identity_sha256 == target_identity_sha256 && target.pc == state.pc);
        previous = key;
        first = false;
    }
    if (!permitted) {
        return DispatchStatus::UnknownCode;
    }
    GeneratedDispatcher selected;
    const auto bound = selected.bind(target_identity_sha256, target_binding);
    return bound == DispatchStatus::Bound ? selected.dispatch(state) : bound;
}

} // namespace clean_aot::runtime
