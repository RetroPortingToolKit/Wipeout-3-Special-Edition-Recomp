#pragma once

#include "clean_aot/app/settings.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace clean_aot::app {

enum class SettingsUiStatus : std::uint8_t { saved, cancelled, invalid, io_error, unavailable };
struct SettingsUiResult final {
    SettingsUiStatus status = SettingsUiStatus::unavailable;
    RuntimeSettings value{};
    std::string error{};
};

// Deterministic headless controller used by automation and by the native UI's
// commit boundary. Actions are key=value, "reset-defaults", "save", or "cancel".
[[nodiscard]] SettingsUiResult run_settings_ui_headless(
    const std::filesystem::path& path, std::span<const std::string_view> actions) noexcept;

// Native Win32 controls. Other platforms return unavailable without mutation.
[[nodiscard]] SettingsUiResult run_settings_ui_native(
    const std::filesystem::path& path) noexcept;

} // namespace clean_aot::app
