#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include "clean_aot/runtime/generated_cpu.hpp"

namespace clean_aot::runtime {

class RuntimeTargetTrace;
enum class RuntimeTraceTransfer : std::uint8_t { None, IndirectJump, IndirectCall };

using GeneratedRegionFunction = bool (*)(GeneratedCpuState& state) noexcept;

struct GeneratedRegionEntry final {
    std::uint32_t pc = 0;
    GeneratedRegionFunction function = nullptr;
    std::uint32_t instruction_count = 0;
};

// Compact generated tables keep the hot lookup records independent of native
// pointer size. A region function is stored once and selected by a checked
// 16-bit index. Page ranges are indexed from the page containing load_start.
struct GeneratedPackedRegionEntry final {
    std::uint32_t pc = 0;
    std::uint16_t region_index = 0;
    std::uint16_t instruction_count = 0;
};

struct GeneratedDispatchPage final {
    std::uint32_t begin_index = 0;
    std::uint32_t end_index = 0;
};

struct GeneratedExecutableBinding final {
    std::string_view variant_identity_sha256;
    std::string_view executable_sha256;
    std::uint32_t load_start = 0;
    std::uint64_t load_end_exclusive = 0;
    std::span<const std::uint32_t> dispatch_roots;
    std::span<const GeneratedRegionEntry> entries;
    std::span<const GeneratedRegionFunction> region_functions{};
    std::span<const GeneratedPackedRegionEntry> packed_entries{};
    std::span<const GeneratedDispatchPage> dispatch_pages{};
};

enum class DispatchStatus : std::uint8_t {
    Bound,
    Executed,
    UnknownCode,
    IdentityMismatch,
    InvalidBinding,
    ExecutableHashMismatch,
    LoadRejected,
    CpuFault,
    EpochMismatch,
};

class GeneratedDispatcher final {
public:
    [[nodiscard]] DispatchStatus bind(
        std::string_view selected_variant_identity_sha256,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus dispatch(GeneratedCpuState& state) const noexcept;
    [[nodiscard]] bool bound() const noexcept {
        return entries_.data() != nullptr || packed_entries_.data() != nullptr;
    }
    [[nodiscard]] std::uint32_t last_instruction_count() const noexcept {
        return last_instruction_count_;
    }
    [[nodiscard]] bool contains(std::uint32_t pc) const noexcept {
        return bound() && pc >= load_start_ && pc < load_end_exclusive_;
    }
    [[nodiscard]] bool resolve_entry(std::uint32_t pc,
                                     GeneratedRegionFunction& function,
                                     std::uint32_t& instruction_count) const noexcept;
    [[nodiscard]] std::uint32_t load_start() const noexcept { return load_start_; }
    [[nodiscard]] std::uint64_t load_end_exclusive() const noexcept {
        return load_end_exclusive_;
    }
    [[nodiscard]] std::string_view identity() const noexcept { return identity_; }
    void set_trace(RuntimeTargetTrace* trace) noexcept { trace_ = trace; }

private:
    std::uint32_t load_start_ = 0;
    std::uint64_t load_end_exclusive_ = 0;
    std::span<const GeneratedRegionEntry> entries_{};
    std::span<const GeneratedRegionFunction> region_functions_{};
    std::span<const GeneratedPackedRegionEntry> packed_entries_{};
    std::span<const GeneratedDispatchPage> dispatch_pages_{};
    mutable std::uint32_t last_instruction_count_ = 0U;
    std::string_view identity_{};
    RuntimeTargetTrace* trace_ = nullptr;
};

struct GeneratedEpochBinding final {
    std::string_view identity_sha256;
    const GeneratedExecutableBinding* binding = nullptr;
};

class GeneratedEpochDispatcher final {
public:
    [[nodiscard]] DispatchStatus select(
        std::string_view authenticated_active_identity_sha256,
        std::span<const GeneratedEpochBinding> candidates) noexcept;
    [[nodiscard]] DispatchStatus dispatch(GeneratedCpuState& state) const noexcept {
        return selected_.dispatch(state);
    }
    void set_trace(RuntimeTargetTrace* trace) noexcept {
        trace_ = trace;
        selected_.set_trace(trace);
    }

private:
    GeneratedDispatcher selected_{};
    RuntimeTargetTrace* trace_ = nullptr;
};

struct GeneratedCfiTarget final {
    std::string_view identity_sha256;
    std::uint32_t pc = 0;
};

struct GeneratedCfiSite final {
    std::string_view source_identity_sha256;
    std::uint32_t source_pc = 0;
    std::span<const GeneratedCfiTarget> targets;
};

class GeneratedCfiDispatcher final {
public:
    void set_trace(RuntimeTargetTrace* trace) noexcept { trace_ = trace; }
    [[nodiscard]] DispatchStatus dispatch_transfer(
        std::string_view source_identity_sha256,
        std::uint32_t source_pc,
        std::string_view target_identity_sha256,
        const GeneratedExecutableBinding& target_binding,
        GeneratedCpuState& state,
        std::span<const GeneratedCfiSite> policies,
        RuntimeTraceTransfer transfer = RuntimeTraceTransfer::IndirectCall) const noexcept;
private:
    RuntimeTargetTrace* trace_ = nullptr;
};

} // namespace clean_aot::runtime
