#pragma once

#include "clean_aot/runtime/native_dispatch.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace clean_aot::app {

struct LaunchInputs final {
    std::filesystem::path setup_receipt;
    std::filesystem::path finalize_receipt;
    std::filesystem::path catalog;
    std::filesystem::path selection_receipt;
    std::filesystem::path executable;
    std::filesystem::path artifact_root;
    std::filesystem::path bios;
};

enum class AuthorizationCode : std::uint8_t {
    Authorized,
    IoError,
    InvalidDocument,
    NotFinalized,
    IdentityMismatch,
    MeasurementMismatch,
    UnsafePath,
    BindingMismatch,
};

struct Authorization final {
    AuthorizationCode code = AuthorizationCode::InvalidDocument;
    std::string detail;
    std::string variant_identity_sha256;
    std::vector<std::uint8_t> bios;

    [[nodiscard]] bool authorized() const noexcept {
        return code == AuthorizationCode::Authorized;
    }
};

[[nodiscard]] Authorization authorize_launch(
    const LaunchInputs& inputs,
    const runtime::GeneratedExecutableBinding& binding) noexcept;

} // namespace clean_aot::app
