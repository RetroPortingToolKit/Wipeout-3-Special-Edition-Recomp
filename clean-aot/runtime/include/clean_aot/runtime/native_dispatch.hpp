#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace clean_aot::runtime {

using GeneratedRegionFunction = bool (*)(std::uint32_t& pc, bool branch_taken) noexcept;

struct GeneratedRegionEntry final {
    std::uint32_t pc = 0;
    GeneratedRegionFunction function = nullptr;
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
};

class GeneratedDispatcher final {
public:
    [[nodiscard]] DispatchStatus bind(
        std::string_view selected_variant_identity_sha256,
        const GeneratedExecutableBinding& binding) noexcept;
    [[nodiscard]] DispatchStatus dispatch(std::uint32_t& pc,
                                          bool branch_taken) const noexcept;
    [[nodiscard]] bool bound() const noexcept { return entries_.data() != nullptr; }

private:
    std::uint32_t load_start_ = 0;
    std::uint64_t load_end_exclusive_ = 0;
    std::span<const GeneratedRegionEntry> entries_{};
};

} // namespace clean_aot::runtime
