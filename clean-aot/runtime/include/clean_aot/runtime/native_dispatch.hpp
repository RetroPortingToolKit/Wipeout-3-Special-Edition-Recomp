#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include "clean_aot/runtime/generated_cpu.hpp"

namespace clean_aot::runtime {

using GeneratedRegionFunction = bool (*)(GeneratedCpuState& state) noexcept;

struct GeneratedRegionEntry final {
    std::uint32_t pc = 0;
    GeneratedRegionFunction function = nullptr;
    std::uint32_t instruction_count = 0;
};

struct GeneratedExecutableBinding final {
    std::string_view variant_identity_sha256;
    std::string_view executable_sha256;
    std::uint32_t load_start = 0;
    std::uint64_t load_end_exclusive = 0;
    std::span<const std::uint32_t> dispatch_roots;
    std::span<const GeneratedRegionEntry> entries;
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
};

class GeneratedDispatcher final {
public:
    [[nodiscard]] DispatchStatus bind(
        std::string_view selected_variant_identity_sha256,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus dispatch(GeneratedCpuState& state) const noexcept;
    [[nodiscard]] bool bound() const noexcept { return entries_.data() != nullptr; }
    [[nodiscard]] std::uint32_t last_instruction_count() const noexcept {
        return last_instruction_count_;
    }

private:
    std::uint32_t load_start_ = 0;
    std::uint64_t load_end_exclusive_ = 0;
    std::span<const GeneratedRegionEntry> entries_{};
    mutable std::uint32_t last_instruction_count_ = 0U;
};

} // namespace clean_aot::runtime
