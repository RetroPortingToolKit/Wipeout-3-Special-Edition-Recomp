#include "clean_aot/runtime/native_dispatch.hpp"
#include "clean_aot/runtime/target_trace.hpp"

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
    region_functions_ = {};
    packed_entries_ = {};
    dispatch_pages_ = {};
    identity_ = {};
    if (!is_sha256(selected_variant_identity_sha256) ||
        !is_sha256(binding.executable_sha256) ||
        selected_variant_identity_sha256 != binding.variant_identity_sha256) {
        return DispatchStatus::IdentityMismatch;
    }
    if (binding.load_end_exclusive <= binding.load_start ||
        binding.load_end_exclusive > (std::uint64_t{1} << 32U) ||
        binding.dispatch_roots.empty()) {
        return DispatchStatus::InvalidBinding;
    }
    const bool legacy = !binding.entries.empty();
    const bool packed = !binding.packed_entries.empty();
    if (legacy == packed ||
        (legacy && (!binding.region_functions.empty() || !binding.dispatch_pages.empty())) ||
        (packed && (binding.region_functions.empty() || binding.dispatch_pages.empty()))) {
        return DispatchStatus::InvalidBinding;
    }
    std::uint64_t previous = 0;
    bool first = true;
    if (legacy) {
        for (const GeneratedRegionEntry& entry : binding.entries) {
            if (entry.function == nullptr || (entry.pc % 4U) != 0U ||
                entry.pc < binding.load_start || entry.pc >= binding.load_end_exclusive ||
                (!first && entry.pc <= previous)) {
                return DispatchStatus::InvalidBinding;
            }
            previous = entry.pc;
            first = false;
        }
    } else {
        if (binding.region_functions.size() > 65536U ||
            std::any_of(binding.region_functions.begin(), binding.region_functions.end(),
                        [](GeneratedRegionFunction function) { return function == nullptr; })) {
            return DispatchStatus::InvalidBinding;
        }
        for (const GeneratedPackedRegionEntry& entry : binding.packed_entries) {
            if ((entry.pc % 4U) != 0U || entry.pc < binding.load_start ||
                entry.pc >= binding.load_end_exclusive ||
                entry.region_index >= binding.region_functions.size() ||
                (!first && entry.pc <= previous)) {
                return DispatchStatus::InvalidBinding;
            }
            previous = entry.pc;
            first = false;
        }
        const std::uint64_t first_page = binding.load_start >> 12U;
        const std::uint64_t last_page = (binding.load_end_exclusive - 1U) >> 12U;
        if (binding.dispatch_pages.size() != last_page - first_page + 1U) {
            return DispatchStatus::InvalidBinding;
        }
        std::uint32_t expected = 0U;
        for (std::size_t page = 0; page < binding.dispatch_pages.size(); ++page) {
            const auto range = binding.dispatch_pages[page];
            if (range.begin_index != expected || range.end_index < range.begin_index ||
                range.end_index > binding.packed_entries.size()) {
                return DispatchStatus::InvalidBinding;
            }
            for (std::uint32_t index = range.begin_index; index < range.end_index; ++index) {
                if ((binding.packed_entries[index].pc >> 12U) != first_page + page) {
                    return DispatchStatus::InvalidBinding;
                }
            }
            expected = range.end_index;
        }
        if (expected != binding.packed_entries.size()) return DispatchStatus::InvalidBinding;
    }
    previous = 0;
    first = true;
    for (const std::uint32_t root : binding.dispatch_roots) {
        if ((root % 4U) != 0U || root < binding.load_start ||
            root >= binding.load_end_exclusive || (!first && root <= previous)) {
            return DispatchStatus::InvalidBinding;
        }
        bool found = false;
        if (legacy) {
            const auto candidate = std::lower_bound(
                binding.entries.begin(), binding.entries.end(), root,
                [](const GeneratedRegionEntry& entry, std::uint32_t value) {
                    return entry.pc < value;
                });
            found = candidate != binding.entries.end() && candidate->pc == root;
        } else {
            const auto candidate = std::lower_bound(
                binding.packed_entries.begin(), binding.packed_entries.end(), root,
                [](const GeneratedPackedRegionEntry& entry, std::uint32_t value) {
                    return entry.pc < value;
                });
            found = candidate != binding.packed_entries.end() && candidate->pc == root;
        }
        if (!found) {
            return DispatchStatus::InvalidBinding;
        }
        previous = root;
        first = false;
    }
    load_start_ = binding.load_start;
    load_end_exclusive_ = binding.load_end_exclusive;
    entries_ = binding.entries;
    region_functions_ = binding.region_functions;
    packed_entries_ = binding.packed_entries;
    dispatch_pages_ = binding.dispatch_pages;
    identity_ = binding.variant_identity_sha256;
    return DispatchStatus::Bound;
}

DispatchStatus GeneratedDispatcher::dispatch(GeneratedCpuState& state) const noexcept {
    last_instruction_count_ = 0U;
    std::uint32_t& pc = state.pc;
    const std::uint32_t source_pc = pc;
    if ((!bound()) || pc < load_start_ || pc >= load_end_exclusive_) {
        if (trace_ != nullptr) trace_->record_dispatch(identity_, source_pc, pc,
                                                       DispatchStatus::UnknownCode);
        return DispatchStatus::UnknownCode;
    }
    GeneratedRegionFunction function = nullptr;
    std::uint32_t instruction_count = 0U;
    if (!packed_entries_.empty()) {
        const std::size_t page = static_cast<std::size_t>((pc >> 12U) - (load_start_ >> 12U));
        const auto range = dispatch_pages_[page];
        const auto begin = packed_entries_.begin() + range.begin_index;
        const auto end = packed_entries_.begin() + range.end_index;
        const auto found = std::lower_bound(begin, end, pc,
            [](const GeneratedPackedRegionEntry& entry, std::uint32_t address) {
                return entry.pc < address;
            });
        if (found != end && found->pc == pc) {
            function = region_functions_[found->region_index];
            instruction_count = found->instruction_count;
        }
    } else {
        const auto found = std::lower_bound(entries_.begin(), entries_.end(), pc,
            [](const GeneratedRegionEntry& entry, std::uint32_t address) {
                return entry.pc < address;
            });
        if (found != entries_.end() && found->pc == pc) {
            function = found->function;
            instruction_count = found->instruction_count;
        }
    }
    if (function == nullptr) {
        if (trace_ != nullptr) trace_->record_dispatch(identity_, source_pc, pc,
                                                       DispatchStatus::UnknownCode);
        return DispatchStatus::UnknownCode;
    }
    const GeneratedCpuState original_state = state;
    if (!function(state)) {
        const bool faulted = state.fault != CpuFault::None;
        state = original_state;
        const auto status = faulted ? DispatchStatus::CpuFault : DispatchStatus::UnknownCode;
        if (trace_ != nullptr) trace_->record_dispatch(identity_, source_pc, state.pc, status);
        return status;
    }
    if (state.fault != CpuFault::None) {
        state = original_state;
        if (trace_ != nullptr) trace_->record_dispatch(identity_, source_pc, state.pc,
                                                       DispatchStatus::CpuFault);
        return DispatchStatus::CpuFault;
    }
    last_instruction_count_ = instruction_count;
    if (trace_ != nullptr) trace_->record_dispatch(identity_, source_pc, state.pc,
                                                   DispatchStatus::Executed);
    return DispatchStatus::Executed;
}

bool GeneratedDispatcher::resolve_entry(
    std::uint32_t pc, GeneratedRegionFunction& function,
    std::uint32_t& instruction_count) const noexcept {
    function = nullptr;
    instruction_count = 0U;
    if (!contains(pc)) return false;
    if (!packed_entries_.empty()) {
        const std::size_t page = static_cast<std::size_t>(
            (pc >> 12U) - (load_start_ >> 12U));
        const auto range = dispatch_pages_[page];
        const auto begin = packed_entries_.begin() + range.begin_index;
        const auto end = packed_entries_.begin() + range.end_index;
        const auto found = std::lower_bound(
            begin, end, pc,
            [](const GeneratedPackedRegionEntry& entry, std::uint32_t address) {
                return entry.pc < address;
            });
        if (found == end || found->pc != pc) return false;
        function = region_functions_[found->region_index];
        instruction_count = found->instruction_count;
        return true;
    }
    const auto found = std::lower_bound(
        entries_.begin(), entries_.end(), pc,
        [](const GeneratedRegionEntry& entry, std::uint32_t address) {
            return entry.pc < address;
        });
    if (found == entries_.end() || found->pc != pc) return false;
    function = found->function;
    instruction_count = found->instruction_count;
    return true;
}

DispatchStatus GeneratedEpochDispatcher::select(
    std::string_view authenticated_active_identity_sha256,
    std::span<const GeneratedEpochBinding> candidates) noexcept {
    selected_ = GeneratedDispatcher{};
    selected_.set_trace(trace_);
    auto finish = [&](DispatchStatus status) {
        if (trace_ != nullptr) trace_->record_epoch(authenticated_active_identity_sha256, status);
        return status;
    };
    if (!is_sha256(authenticated_active_identity_sha256) || candidates.empty()) {
        return finish(DispatchStatus::IdentityMismatch);
    }
    std::string_view previous{};
    bool first = true;
    for (const auto& candidate : candidates) {
        if (!is_sha256(candidate.identity_sha256) || candidate.binding == nullptr ||
            candidate.binding->variant_identity_sha256 != candidate.identity_sha256 ||
            (!first && candidate.identity_sha256 <= previous)) {
            return finish(DispatchStatus::InvalidBinding);
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
        return finish(DispatchStatus::IdentityMismatch);
    }
    return finish(selected_.bind(authenticated_active_identity_sha256, *found->binding));
}

DispatchStatus GeneratedCfiDispatcher::dispatch_transfer(
    std::string_view source_identity_sha256,
    std::uint32_t source_pc,
    std::string_view target_identity_sha256,
    const GeneratedExecutableBinding& target_binding,
    GeneratedCpuState& state,
    std::span<const GeneratedCfiSite> policies,
    RuntimeTraceTransfer transfer) const noexcept {
    const std::uint32_t attempted_target_pc = state.pc;
    auto finish = [&](DispatchStatus status) {
        if (trace_ != nullptr) trace_->record_target_attempt(
            target_identity_sha256, source_identity_sha256, source_pc,
            target_identity_sha256, attempted_target_pc, transfer, status);
        return status;
    };
    if (!is_sha256(source_identity_sha256) || !is_sha256(target_identity_sha256) ||
        target_binding.variant_identity_sha256 != target_identity_sha256 ||
        state.pc % 4U != 0U) {
        return finish(DispatchStatus::IdentityMismatch);
    }
    std::pair<std::string_view, std::uint32_t> previous_site{};
    bool first_site = true;
    for (const auto& policy : policies) {
        const auto key = std::pair{policy.source_identity_sha256, policy.source_pc};
        if (!is_sha256(policy.source_identity_sha256) || policy.source_pc % 4U != 0U ||
            policy.targets.empty() || (!first_site && key <= previous_site)) {
            return finish(DispatchStatus::InvalidBinding);
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
        return finish(DispatchStatus::UnknownCode);
    }
    std::pair<std::string_view, std::uint32_t> previous{};
    bool first = true;
    bool permitted = false;
    for (const auto& target : site->targets) {
        const auto key = std::pair{target.identity_sha256, target.pc};
        if (!is_sha256(target.identity_sha256) || target.pc % 4U != 0U ||
            (!first && key <= previous)) {
            return finish(DispatchStatus::InvalidBinding);
        }
        permitted = permitted ||
                    (target.identity_sha256 == target_identity_sha256 && target.pc == state.pc);
        previous = key;
        first = false;
    }
    if (!permitted) {
        return finish(DispatchStatus::UnknownCode);
    }
    GeneratedDispatcher selected;
    selected.set_trace(trace_);
    const auto bound = selected.bind(target_identity_sha256, target_binding);
    const auto status = bound == DispatchStatus::Bound ? selected.dispatch(state) : bound;
    return finish(status);
}

} // namespace clean_aot::runtime
