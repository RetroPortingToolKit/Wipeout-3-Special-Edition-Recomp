#pragma once

#include "clean_aot/host/sdl_vulkan.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace clean_aot::app {

struct RuntimeSettings final {
    std::uint32_t width = 3840U;
    std::uint32_t height = 2160U;
    clean_aot::host::FullscreenMode fullscreen = clean_aot::host::FullscreenMode::borderless;
    clean_aot::host::SdlPresentPolicy present_policy = clean_aot::host::SdlPresentPolicy::low_latency_100hz;
    clean_aot::host::PresentationFilter presentation_filter = clean_aot::host::PresentationFilter::Linear;
    std::uint32_t audio_latency_frames = 882U;
    std::uint32_t audio_max_frames = 3528U;
    std::string audio_device = "default";
    std::uint16_t deadzone_per_mille = 120U;
    bool rumble = true;
    bool pgxp = true;
    bool true_color = true;
    bool hd_assets = true;
    bool ultrawide = true;
    bool atomic_saves = true;
    std::uint8_t card_slots = 2U;

    [[nodiscard]] clean_aot::host::SdlVulkanConfig host_config(bool hidden) const noexcept;
};

enum class SettingsStatus : std::uint8_t { loaded, created, reset, upgraded, invalid, io_error };
struct SettingsResult final { SettingsStatus status = SettingsStatus::invalid; RuntimeSettings value{}; std::string error{}; };

[[nodiscard]] SettingsResult load_runtime_settings(const std::filesystem::path& path,
                                                   bool reset = false) noexcept;
[[nodiscard]] bool apply_settings_override(RuntimeSettings& settings,
                                           std::string_view assignment,
                                           std::string& error) noexcept;
[[nodiscard]] std::string runtime_settings_json(const RuntimeSettings& settings);

} // namespace clean_aot::app
