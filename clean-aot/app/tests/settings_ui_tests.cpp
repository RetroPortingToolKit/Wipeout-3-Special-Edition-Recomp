#include "clean_aot/app/settings_ui.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>

namespace {
int failures = 0;
void expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; } }
}

int main() {
    using namespace clean_aot::app;
    const auto root = std::filesystem::temp_directory_path() / "Wipeout3SE-UI-設定";
    std::error_code ignored; std::filesystem::remove_all(root, ignored);
    const auto path = root / "構成.json";
    const std::array save_actions{std::string_view{"display.width=5120"},
        std::string_view{"display.height=2160"}, std::string_view{"display.aspect=auto-ultrawide"},
        std::string_view{"graphics.pgxp=on"}, std::string_view{"graphics.true_color=on"},
        std::string_view{"graphics.hd_assets=on"}, std::string_view{"audio.device=音声デバイス"},
        std::string_view{"audio.latency_frames=1323"}, std::string_view{"input.deadzone_per_mille=180"},
        std::string_view{"input.rumble=on"}, std::string_view{"saves.card_slots=2"},
        std::string_view{"save"}};
    const auto saved = run_settings_ui_headless(path, save_actions);
    expect(saved.status == SettingsUiStatus::saved, "headless UI saves validated settings");
    const auto loaded = load_runtime_settings(path);
    expect(loaded.status == SettingsStatus::loaded && loaded.value.width == 5120U &&
           loaded.value.audio_device == "音声デバイス" && loaded.value.card_slots == 2U,
           "saved Unicode modern settings round trip");
    std::ifstream input(path, std::ios::binary); const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const std::array cancel_actions{std::string_view{"display.width=640"}, std::string_view{"cancel"}};
    expect(run_settings_ui_headless(path, cancel_actions).status == SettingsUiStatus::cancelled,
           "cancel is accepted");
    std::ifstream after_stream(path, std::ios::binary); const std::string after((std::istreambuf_iterator<char>(after_stream)), {});
    expect(bytes == after, "cancel does not mutate settings");
    input.close(); after_stream.close();
    { std::ofstream malformed(path, std::ios::trunc); malformed << "broken"; }
    const std::array reset_actions{std::string_view{"reset-defaults"}, std::string_view{"save"}};
    const auto reset = run_settings_ui_headless(path, reset_actions);
    expect(reset.status == SettingsUiStatus::saved &&
           load_runtime_settings(path).value.width == 3840U, "reset safely recovers invalid config");
    const std::array invalid_actions{std::string_view{"display.width=99999"}, std::string_view{"save"}};
    expect(run_settings_ui_headless(path, invalid_actions).status == SettingsUiStatus::invalid,
           "invalid values fail closed");
    std::filesystem::remove_all(root, ignored);
    if (failures == 0) std::cout << "settings UI tests: pass\n";
    return failures == 0 ? 0 : 1;
}
