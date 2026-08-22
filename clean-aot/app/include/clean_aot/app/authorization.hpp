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
    std::filesystem::path media_root;
};

struct SourceMeasurement final {
    std::string role;
    std::string name;
    std::uint64_t size = 0U;
    std::string sha256;
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
    std::string catalog_sha256;
    std::string variant_identity_sha256;
    std::string executable_sha256;
    std::string artifact_sha256;
    std::uint32_t load_start = 0U;
    std::uint64_t load_end_exclusive = 0U;
    std::uint32_t selected_entry_pc = 0U;
    std::vector<std::uint8_t> bios;
    std::vector<std::uint8_t> executable;
    std::filesystem::path cue_path;
    std::vector<SourceMeasurement> media;

    [[nodiscard]] bool authorized() const noexcept {
        return code == AuthorizationCode::Authorized;
    }
};

[[nodiscard]] Authorization authorize_launch(
    const LaunchInputs& inputs,
    const runtime::GeneratedExecutableBinding& binding) noexcept;

} // namespace clean_aot::app
