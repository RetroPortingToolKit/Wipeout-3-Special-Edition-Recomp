#pragma once

#include "clean_aot/app/authorization.hpp"
#include "clean_aot/app/disc_source.hpp"
#include "clean_aot/runtime/runtime.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace clean_aot::app {

enum class BootstrapStage : std::uint8_t {
    Rejected,
    MediaMounted,
    RuntimeInitialized,
    ExecutableLoaded,
    GeneratedSliceStopped,
};

struct BootstrapReceipt final {
    BootstrapStage stage = BootstrapStage::Rejected;
    std::string reason;
    std::string catalog_sha256;
    std::string variant_identity_sha256;
    std::string executable_sha256;
    std::string bios_sha256;
    std::uint32_t entry_pc = 0U;
    std::uint32_t stop_pc = 0U;
    std::uint64_t generated_dispatches = 0U;
    std::uint64_t bios_instructions_executed = 0U;
    std::uint64_t guest_instructions_executed = 0U;
    std::uint64_t disc_sectors_read = 0U;

    [[nodiscard]] std::string canonical_json() const;
};

class BootstrapSession final {
public:
    [[nodiscard]] BootstrapReceipt start(
        Authorization authorization,
        const runtime::GeneratedExecutableBinding* binding,
        std::uint64_t maximum_dispatches = 1'000'000U) noexcept;

    [[nodiscard]] const runtime::Runtime* runtime() const noexcept {
        return runtime_.get();
    }

private:
    Authorization authorization_{};
    std::unique_ptr<DiscSource> disc_{};
    std::unique_ptr<runtime::Runtime> runtime_{};
};

using GeneratedBindingAccessor =
    const runtime::GeneratedExecutableBinding* (*)() noexcept;

} // namespace clean_aot::app
