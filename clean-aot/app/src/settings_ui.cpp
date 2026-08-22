#include "clean_aot/app/settings_ui.hpp"

#include <array>
#include <fstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace clean_aot::app {
namespace {

SettingsUiResult initial(const std::filesystem::path& path) noexcept {
    SettingsUiResult result{};
    result.status = SettingsUiStatus::cancelled;
    if (!std::filesystem::exists(path)) return result;
    const auto loaded = load_runtime_settings(path);
    if (loaded.status == SettingsStatus::invalid) {
        result.error = loaded.error; return result;
    }
    if (loaded.status == SettingsStatus::io_error) {
        result.status = SettingsUiStatus::io_error; result.error = loaded.error; return result;
    }
    result.value = loaded.value;
    return result;
}

SettingsUiResult commit(const std::filesystem::path& path, RuntimeSettings value) noexcept {
    SettingsUiResult result{}; result.value = std::move(value);
    if (!save_runtime_settings(path, result.value, result.error)) {
        result.status = SettingsUiStatus::io_error; return result;
    }
    result.status = SettingsUiStatus::saved; return result;
}

} // namespace

SettingsUiResult run_settings_ui_headless(
    const std::filesystem::path& path, std::span<const std::string_view> actions) noexcept {
    auto result = initial(path);
    if (result.status == SettingsUiStatus::invalid || result.status == SettingsUiStatus::io_error)
        return result;
    RuntimeSettings working = result.value;
    bool terminal = false;
    for (const auto action : actions) {
        if (terminal) { result.status = SettingsUiStatus::invalid;
            result.error = "actions follow terminal settings action"; return result; }
        if (action == "reset-defaults") working = RuntimeSettings{};
        else if (action == "cancel") { result.status = SettingsUiStatus::cancelled; terminal = true; }
        else if (action == "save") { result = commit(path, working); terminal = true; }
        else if (!apply_settings_override(working, action, result.error)) {
            result.status = SettingsUiStatus::invalid; return result;
        }
    }
    if (!terminal) { result.status = SettingsUiStatus::invalid;
        result.error = "settings actions require save or cancel"; }
    return result;
}

#ifdef _WIN32
namespace {
constexpr int kSave = 100, kReset = 101, kCancel = 102, kFirstField = 200;
constexpr std::array<std::string_view, 16> keys{{
    "display.width", "display.height", "display.mode", "display.aspect", "display.filter",
    "presentation.policy", "graphics.pgxp", "graphics.pgxp_verified_near_plane",
    "graphics.true_color", "graphics.hd_assets", "audio.device", "audio.latency_frames",
    "audio.max_queued_frames", "input.deadzone_per_mille", "input.rumble", "saves.card_slots"}};

std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::array<std::string, keys.size()> values(const RuntimeSettings& settings) {
    std::array<std::string, keys.size()> result{};
    // Use the already validated override vocabulary, not a second settings schema.
    result = {std::to_string(settings.width), std::to_string(settings.height),
        settings.fullscreen == host::FullscreenMode::windowed ? "windowed" :
        settings.fullscreen == host::FullscreenMode::exclusive ? "exclusive" : "borderless",
        settings.ultrawide ? "auto-ultrawide" : "native-4:3",
        settings.presentation_filter == host::PresentationFilter::Linear ? "linear" : "nearest",
        settings.present_policy == host::SdlPresentPolicy::vsync ? "vsync" :
        settings.present_policy == host::SdlPresentPolicy::mailbox ? "mailbox" : "low-latency-100hz",
        settings.pgxp ? "on" : "off", settings.pgxp_verified_near_plane ? "on" : "off",
        settings.true_color ? "on" : "off", settings.hd_assets ? "on" : "off",
        settings.audio_device, std::to_string(settings.audio_latency_frames),
        std::to_string(settings.audio_max_frames), std::to_string(settings.deadzone_per_mille),
        settings.rumble ? "on" : "off", std::to_string(settings.card_slots)};
    return result;
}

struct DialogState final {
    std::filesystem::path path;
    RuntimeSettings working{};
    SettingsUiResult result{};
    std::array<HWND, keys.size()> fields{};
};

void fill(DialogState& state) {
    const auto current = values(state.working);
    for (std::size_t index = 0; index < current.size(); ++index)
        SetWindowTextW(state.fields[index], wide(current[index]).c_str());
}

LRESULT CALLBACK settings_proc(HWND window, UINT message, WPARAM word, LPARAM parameter) {
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<DialogState*>(reinterpret_cast<CREATESTRUCTW*>(parameter)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(window, message, word, parameter);
    if (message == WM_COMMAND) {
        const int id = LOWORD(word);
        if (id == kCancel) { state->result.status = SettingsUiStatus::cancelled; DestroyWindow(window); return 0; }
        if (id == kReset) { state->working = RuntimeSettings{}; fill(*state); return 0; }
        if (id == kSave) {
            RuntimeSettings candidate{};
            for (std::size_t index = 0; index < keys.size(); ++index) {
                const int length = GetWindowTextLengthW(state->fields[index]);
                std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
                GetWindowTextW(state->fields[index], text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));
                const auto assignment = std::string(keys[index]) + "=" + narrow(text);
                if (!apply_settings_override(candidate, assignment, state->result.error)) {
                    MessageBoxW(window, wide(state->result.error).c_str(), L"Invalid setting", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            state->result = commit(state->path, candidate);
            if (state->result.status != SettingsUiStatus::saved) {
                MessageBoxW(window, wide(state->result.error).c_str(), L"Settings write failed", MB_OK | MB_ICONERROR);
                return 0;
            }
            DestroyWindow(window); return 0;
        }
    }
    if (message == WM_CLOSE) { state->result.status = SettingsUiStatus::cancelled; DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, word, parameter);
}
} // namespace
#endif

SettingsUiResult run_settings_ui_native(const std::filesystem::path& path) noexcept {
#ifndef _WIN32
    (void)path; return {};
#else
    try {
        DialogState state{}; state.path = path; state.result = initial(path);
        if (state.result.status == SettingsUiStatus::invalid ||
            state.result.status == SettingsUiStatus::io_error) return state.result;
        state.working = state.result.value;
        const wchar_t* class_name = L"Wipeout3SESettingsWindow";
        WNDCLASSW type{}; type.lpfnWndProc = settings_proc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = class_name;
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&type);
        HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, class_name, L"Wipeout 3 SE - Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 850, 520,
            nullptr, nullptr, type.hInstance, &state);
        if (!window) { state.result.status = SettingsUiStatus::unavailable; return state.result; }
        const auto current = values(state.working);
        for (std::size_t index = 0; index < keys.size(); ++index) {
            const int column = index >= 8U ? 1 : 0;
            const int row = static_cast<int>(column ? index - 8U : index);
            const int x = 20 + column * 410, y = 20 + row * 48;
            CreateWindowExW(0, L"STATIC", wide(keys[index]).c_str(), WS_CHILD | WS_VISIBLE,
                            x, y, 190, 22, window, nullptr, type.hInstance, nullptr);
            state.fields[index] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wide(current[index]).c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x + 195, y - 3, 190, 26,
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstField + index)),
                type.hInstance, nullptr);
        }
        CreateWindowExW(0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        500, 430, 90, 30, window, reinterpret_cast<HMENU>(kSave), type.hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Reset defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        600, 430, 110, 30, window, reinterpret_cast<HMENU>(kReset), type.hInstance, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        720, 430, 90, 30, window, reinterpret_cast<HMENU>(kCancel), type.hInstance, nullptr);
        ShowWindow(window, SW_SHOW); UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return state.result;
    } catch (...) { SettingsUiResult result{}; result.status = SettingsUiStatus::io_error;
        result.error = "native settings UI failed"; return result; }
#endif
}

} // namespace clean_aot::app
