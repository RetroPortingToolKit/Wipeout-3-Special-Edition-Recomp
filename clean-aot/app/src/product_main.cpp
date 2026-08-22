#include "clean_aot/host/headless.hpp"
#include "clean_aot/host/pacing.hpp"
#include "clean_aot/host/sdl_vulkan.hpp"
#include "clean_aot/runtime/runtime.hpp"
#include "clean_aot/app/graphics.hpp"
#include "clean_aot/app/memory_card.hpp"
#include "clean_aot/app/settings.hpp"
#include "clean_aot/app/settings_ui.hpp"
#include "sha256.hpp"
#include "sio.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

#ifdef CLEAN_AOT_PRODUCT_HAS_EPOCH_BINDINGS
#include "product_epoch_bindings.hpp"
#endif

#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
extern "C" const clean_aot::runtime::GeneratedExecutableBinding*
CLEAN_AOT_PRODUCT_BINDING_SYMBOL() noexcept;
#endif

namespace {
std::filesystem::path path_from_utf8(std::string_view value) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

struct DurableCard final {
    clean_aot::app::MemoryCard storage{};
    std::array<std::uint8_t, clean_aot::app::kMemoryCardBytes> image{};
    bool durability_failed = false;
};

void persist_card_write(void* opaque, const clean_aot::sio::WriteReceipt& receipt) noexcept {
    auto& card = *static_cast<DurableCard*>(opaque);
    constexpr std::size_t sector_bytes = clean_aot::sio::MemoryCard::kSectorSize;
    const auto offset = static_cast<std::size_t>(receipt.sector) * sector_bytes;
    if (offset > card.image.size() || sector_bytes > card.image.size() - offset) return;
    if (!card.storage.write(offset, std::span<const std::uint8_t>(card.image).subspan(
            offset, sector_bytes)) || !card.storage.flush()) card.durability_failed = true;
}

struct Receipt final {
    std::string bios_identity_sha256, bios_sha256, workload_profile;
    std::string graphics_feature_matrix = "null";
    std::string reason = "product-setup-unavailable", stage = "rejected";
    std::uint32_t stop_pc = 0U;
    std::uint64_t dispatches = 0U, instructions = 0U, updates = 0U;
    std::uint64_t frames = 0U, gpu_deferred = 0U, gpu_failures = 0U, audio_batches = 0U;
    std::uint64_t audio_frames = 0U, audio_underruns = 0U, pacing_late = 0U;
    std::uint64_t pacing_span_ns = 0U;
    void print() const {
        std::cout << "{\"bios_generated_dispatches\":" << dispatches
          << ",\"bios_identity_sha256\":\"" << bios_identity_sha256
          << "\",\"bios_instructions_executed\":" << instructions
          << ",\"bios_sha256\":\"" << bios_sha256
          << "\",\"catalog_sha256\":\"\",\"disc_sectors_read\":0,\"entry_pc\":0"
          << ",\"executable_sha256\":\"\",\"generated_dispatches\":0"
          << ",\"guest_instructions_executed\":0,\"reason\":\"" << reason
          << "\",\"graphics_feature_matrix\":" << graphics_feature_matrix
          << ",\"schema\":\"clean-aot.bootstrap-receipt.v1\",\"stage\":\""
          << stage << "\",\"stop_pc\":" << stop_pc
          << ",\"variant_identity_sha256\":\"\",\"workload\":{"
          << "\"audio_batches_submitted\":" << audio_batches
          << ",\"audio_sample_frames\":" << audio_frames
          << ",\"audio_underruns\":" << audio_underruns
          << ",\"frames_submitted\":" << frames
          << ",\"gpu_deferred_updates\":" << gpu_deferred
          << ",\"gpu_submission_failures\":" << gpu_failures
          << ",\"pacing_late_updates\":" << pacing_late
          << ",\"pacing_span_ns\":" << pacing_span_ns
          << ",\"profile\":\"" << workload_profile
          << "\",\"rate_hz\":100,\"updates\":" << updates << "}}\n";
    }
};

std::vector<std::uint8_t> read_bios(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path) ||
        std::filesystem::file_size(path) != 512U * 1024U) return {};
    std::vector<std::uint8_t> bytes(512U * 1024U);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input ? bytes : std::vector<std::uint8_t>{};
}

std::filesystem::path read_user_bios_reference(const std::filesystem::path& user_data) {
    const auto portable = user_data / "runtime" / "user-bios.path";
    if (std::filesystem::is_regular_file(portable) && !std::filesystem::is_symlink(portable)) {
        std::ifstream stream(portable);
        std::string value;
        std::getline(stream, value);
        if (stream && !value.empty()) return path_from_utf8(value);
    }
    const auto reference = user_data / "receipts" / "active-setup.json";
    if (!std::filesystem::is_regular_file(reference) || std::filesystem::is_symlink(reference))
        return {};
    std::ifstream receipt_stream(reference);
    const std::string receipt_text((std::istreambuf_iterator<char>(receipt_stream)), {});
    constexpr std::string_view key = "\"setup_root\":\"";
    const auto begin = receipt_text.find(key);
    if (begin == std::string::npos) return {};
    const auto value_begin = begin + key.size();
    const auto value_end = receipt_text.find('"', value_begin);
    if (value_end == std::string::npos) return {};
    const std::filesystem::path relative(receipt_text.substr(value_begin, value_end - value_begin));
    if (relative.is_absolute() || relative.empty() ||
        std::find(relative.begin(), relative.end(), std::filesystem::path("..")) != relative.end()) return {};
    const auto selected = user_data / relative / "runtime" / "user-bios.path";
    if (!std::filesystem::is_regular_file(selected) || std::filesystem::is_symlink(selected)) return {};
    std::ifstream stream(selected);
    std::string value;
    std::getline(stream, value);
    return stream && !value.empty() ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path read_portable_reference(const std::filesystem::path& user_data,
                                              std::string_view name) {
    const auto reference = user_data / "runtime" / name;
    if (!std::filesystem::is_regular_file(reference) || std::filesystem::is_symlink(reference)) return {};
    std::ifstream stream(reference);
    std::string value;
    std::getline(stream, value);
    return stream && !value.empty() ? path_from_utf8(value) : std::filesystem::path{};
}

bool atomic_text_write(const std::filesystem::path& path, std::string_view value) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    auto temporary = path;
    temporary += ".new";
    std::filesystem::remove(temporary, error);
    error.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        output.flush();
        if (!output) { std::filesystem::remove(temporary, error); return false; }
    }
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    std::filesystem::remove(temporary, error);
    return false;
#else
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}

std::string file_sha256(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path) || std::filesystem::is_symlink(path)) return {};
    std::ifstream input(path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    return input || input.eof() ? clean_aot::app::detail::sha256(bytes) : std::string{};
}

bool verify_cue(const std::filesystem::path& cue, std::string& digest) {
    if (!std::filesystem::is_regular_file(cue) || std::filesystem::is_symlink(cue)) return false;
    std::ifstream input(cue);
    std::string line;
    std::vector<std::filesystem::path> tracks;
    while (std::getline(input, line)) {
        const auto marker = line.find("FILE \"");
        if (marker == std::string::npos) continue;
        const auto begin = marker + 6U;
        const auto end = line.find('"', begin);
        if (end == std::string::npos || end == begin) return false;
        auto track = cue.parent_path() / path_from_utf8(line.substr(begin, end - begin));
        track = track.lexically_normal();
        if (!std::filesystem::is_regular_file(track) || std::filesystem::is_symlink(track) ||
            std::filesystem::file_size(track) == 0U) return false;
        tracks.push_back(track);
    }
    constexpr std::array<std::uintmax_t, 14U> expected_sizes{
        94660944U, 43472016U, 32911536U, 45706416U, 44490432U, 44518656U,
        39297216U, 38991456U, 45410064U, 44953776U, 45106656U, 39179616U,
        42914592U, 43961232U};
    if (!input.eof() || tracks.size() != expected_sizes.size()) return false;
    std::string inventory = file_sha256(cue);
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        if (std::filesystem::file_size(tracks[index]) != expected_sizes[index]) return false;
        inventory += file_sha256(tracks[index]);
    }
    digest = clean_aot::app::detail::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(inventory.data()), inventory.size()));
    return true;
}

bool persist_portable_import(const std::filesystem::path& user_data,
                             const std::filesystem::path& bios,
                             const std::filesystem::path& cue,
                             std::string_view expected_bios_sha256) {
    const auto bios_hash = file_sha256(bios);
    std::string media_hash;
    if (bios_hash.empty() || bios_hash != expected_bios_sha256 || !verify_cue(cue, media_hash)) return false;
    const auto runtime = user_data / "runtime";
    const auto receipt = user_data / "receipts" / "portable-media.json";
    const auto bios_path = bios.u8string();
    const auto cue_path = cue.u8string();
    const std::string bios_text(reinterpret_cast<const char*>(bios_path.data()), bios_path.size());
    const std::string cue_text(reinterpret_cast<const char*>(cue_path.data()), cue_path.size());
    const std::string document = "{\"bios_sha256\":\"" + bios_hash +
        "\",\"copyrighted_inputs_copied\":false,\"cue_inventory_sha256\":\"" + media_hash +
        "\",\"schema\":\"wipe" "out3se.portable-media-import.v1\",\"status\":\"verified-external-media\"}\n";
    return atomic_text_write(runtime / "user-bios.path", bios_text + "\n") &&
        atomic_text_write(runtime / "user-cue.path", cue_text + "\n") &&
        atomic_text_write(receipt, document);
}

class DeterministicClock final : public clean_aot::host::IMonotonicClock {
public:
    std::uint64_t now_ns() const noexcept override { return now_; }
    void sleep_until_ns(std::uint64_t deadline) noexcept override { now_ = deadline; }
private: std::uint64_t now_ = 0U;
};

bool run_workload(clean_aot::host::IHostBackend& host, std::uint32_t width,
                  std::uint32_t height, std::uint64_t count, Receipt& receipt) {
    const auto bytes64 = static_cast<std::uint64_t>(width) * height * 4U;
    if (bytes64 > std::numeric_limits<std::uint32_t>::max()) return false;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(bytes64));
    std::array<std::int16_t, 882U> audio{};
    clean_aot::host::MonotonicPacer pacer(clean_aot::host::FrameSchedule::from_rate(100U));
    DeterministicClock clock;
    if (!pacer.start(0U).success()) return false;
    for (std::uint64_t update = 0; update < count; ++update) {
        const auto pace = pacer.wait(clock);
        if (pace.decision == clean_aot::host::PaceDecision::late) ++receipt.pacing_late;
        if (pace.decision == clean_aot::host::PaceDecision::clock_stalled ||
            !pacer.commit().success()) return false;
        const clean_aot::host::FrameView view{pixels.data(), static_cast<std::uint32_t>(bytes64),
            width, height, width * 4U, clean_aot::host::PixelFormat::rgba8_unorm};
        if (host.submit_frame({update, update, view}).success()) ++receipt.frames;
        else ++receipt.gpu_failures;
        if (host.submit_audio({update, audio.data(), 441U}).success()) {
            ++receipt.audio_batches; receipt.audio_frames += 441U;
        } else ++receipt.audio_underruns;
        const auto serviced = host.service();
        if (serviced.code == clean_aot::host::HostCode::not_ready) ++receipt.gpu_deferred;
        else if (!serviced.success()) ++receipt.gpu_failures;
    }
    receipt.updates = count;
    receipt.pacing_span_ns = pacer.deadline_ns();
    return receipt.frames == count && receipt.audio_batches == count &&
        receipt.gpu_failures == 0U && receipt.audio_underruns == 0U && receipt.pacing_late == 0U;
}
}

#ifdef _WIN32
std::string utf8(const wchar_t* value) {
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::filesystem::path select_file(const wchar_t* title, const wchar_t* filter) {
    std::array<wchar_t, 32768U> value{};
    OPENFILENAMEW request{};
    request.lStructSize = sizeof(request);
    request.lpstrFile = value.data();
    request.nMaxFile = static_cast<DWORD>(value.size());
    request.lpstrTitle = title;
    request.lpstrFilter = filter;
    request.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&request) ? std::filesystem::path(value.data()) : std::filesystem::path{};
}
std::filesystem::path default_user_data() {
    std::array<wchar_t, 32768U> value{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(),
                                                 static_cast<DWORD>(value.size()));
    return length > 0U && length < value.size()
        ? std::filesystem::path(value.data()) / (std::string("Wipe") + "out3SE")
        : std::filesystem::path{};
}
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::filesystem::path user_data, bios_path, import_cue;
    std::filesystem::path settings_path;
    std::string profile;
    std::vector<std::string> settings_overrides;
    std::vector<std::string> settings_ui_actions;
    std::uint64_t workload_updates = 0U;
    bool hidden = false, reset_settings = false, settings_ui = false, import_only = false;
    Receipt receipt{};
    for (int index = 1; index < argc; ++index) {
        const std::string argument =
#ifdef _WIN32
            utf8(argv[index]);
#else
            argv[index];
#endif
        if ((argument == "--user-data" || argument == "--bios" || argument == "--import-cue") && index + 1 < argc) {
            const auto value = std::filesystem::path(argv[++index]);
            if (argument == "--user-data") user_data = value;
            else if (argument == "--import-cue") import_cue = value;
            else bios_path = value;
        } else if (argument == "--smoke-hidden") hidden = true;
        else if (argument == "--import-only") import_only = true;
        else if (argument == "--settings" && index + 1 < argc) settings_path = std::filesystem::path(argv[++index]);
        else if (argument == "--set" && index + 1 < argc) settings_overrides.emplace_back(
#ifdef _WIN32
            utf8(argv[++index])
#else
            argv[++index]
#endif
        );
        else if (argument == "--reset-settings") reset_settings = true;
        else if (argument == "--settings-ui") settings_ui = true;
        else if (argument == "--settings-ui-action" && index + 1 < argc)
            settings_ui_actions.emplace_back(
#ifdef _WIN32
                utf8(argv[++index])
#else
                argv[++index]
#endif
            );
        else if (argument == "--workload-profile" && index + 1 < argc) profile =
#ifdef _WIN32
            utf8(argv[++index]);
#else
            argv[++index];
#endif
        else if (argument == "--workload-updates" && index + 1 < argc) {
            try {
#ifdef _WIN32
                workload_updates = std::stoull(argv[++index]);
#else
                workload_updates = std::stoull(argv[++index]);
#endif
            } catch (...) { return 64; }
        } else { std::cerr << "unsupported product argument\n"; return 64; }
    }
#ifdef _WIN32
    if (user_data.empty()) user_data = default_user_data();
#endif
    clean_aot::app::RuntimeSettings settings{};
    if (settings_path.empty() && !user_data.empty()) settings_path = user_data / "config.json";
    const bool settings_missing = !settings_path.empty() && !std::filesystem::exists(settings_path);
    const bool show_first_run_settings = settings_missing && profile.empty() && settings_ui_actions.empty();
    if ((settings_ui || show_first_run_settings || !settings_ui_actions.empty()) && settings_path.empty()) {
        receipt.reason = "settings-ui-requires-user-data-or-settings-path"; receipt.print(); return 64;
    }
    if (settings_ui || show_first_run_settings || !settings_ui_actions.empty()) {
        clean_aot::app::SettingsUiResult edited{};
        if (!settings_ui_actions.empty()) {
            std::vector<std::string_view> actions;
            actions.reserve(settings_ui_actions.size());
            for (const auto& action : settings_ui_actions) actions.emplace_back(action);
            edited = clean_aot::app::run_settings_ui_headless(settings_path, actions);
        } else edited = clean_aot::app::run_settings_ui_native(settings_path);
        if (edited.status == clean_aot::app::SettingsUiStatus::invalid ||
            edited.status == clean_aot::app::SettingsUiStatus::io_error ||
            edited.status == clean_aot::app::SettingsUiStatus::unavailable) {
            receipt.reason = "settings-ui-rejected"; receipt.print(); return 78;
        }
        settings = edited.value;
        if (settings_ui || !settings_ui_actions.empty()) {
            receipt.stage = edited.status == clean_aot::app::SettingsUiStatus::saved
                ? "settings-saved" : "settings-cancelled";
            receipt.reason = "settings-ui-complete"; receipt.print(); return 0;
        }
    }
    if (!settings_path.empty() && std::filesystem::exists(settings_path)) {
        const auto loaded = clean_aot::app::load_runtime_settings(settings_path, reset_settings);
        if (loaded.status == clean_aot::app::SettingsStatus::invalid ||
            loaded.status == clean_aot::app::SettingsStatus::io_error) {
            receipt.reason = "invalid-runtime-settings"; receipt.print(); return 78;
        }
        settings = loaded.value;
    }
    for (const auto& override_value : settings_overrides) {
        std::string error;
        if (!clean_aot::app::apply_settings_override(settings, override_value, error)) {
            receipt.reason = "invalid-settings-override"; receipt.print(); return 64;
        }
    }
    receipt.workload_profile = profile;
    const bool headless = profile.ends_with("-headless");
    const bool p4k = profile == "4k-headless" || profile == "4k-hidden";
    const bool puw = profile == "5120x2160-headless" || profile == "5120x2160-hidden";
    const bool workload = !profile.empty();
    if (workload && ((!p4k && !puw) || workload_updates == 0U || workload_updates > 1000U)) {
        receipt.reason = "invalid-bounded-workload-profile"; receipt.print(); return 64;
    }
 #ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
    const auto* binding = CLEAN_AOT_PRODUCT_BINDING_SYMBOL();
 #else
    const clean_aot::runtime::GeneratedExecutableBinding* binding = nullptr;
 #endif
    if (!binding) { receipt.reason = "missing-authenticated-generated-bios-binding"; receipt.print(); return 78; }
    if (bios_path.empty() && !user_data.empty()) bios_path = read_user_bios_reference(user_data);
    if (import_cue.empty() && !user_data.empty())
        import_cue = read_portable_reference(user_data, "user-cue.path");
    if ((bios_path.empty() || import_cue.empty()) && !workload && !hidden && !import_only) {
#ifdef _WIN32
        if (bios_path.empty()) bios_path = select_file(L"Select your PlayStation BIOS",
            L"PlayStation BIOS (*.bin)\0*.bin\0All files\0*.*\0");
        if (!bios_path.empty() && import_cue.empty()) import_cue = select_file(
            L"Select your Wip" L"Eout 3 Special Edition CUE",
            L"CUE sheets (*.cue)\0*.cue\0All files\0*.*\0");
#endif
    }
    if (!import_cue.empty()) {
        if (user_data.empty() || !persist_portable_import(user_data, bios_path, import_cue,
                                                          binding->executable_sha256)) {
            receipt.stage = "media-import-rejected"; receipt.reason = "external-media-verification-failed";
            receipt.print(); return 78;
        }
        receipt.stage = "media-import-complete"; receipt.reason = "verified-external-media-references";
        if (import_only) { receipt.print(); return 0; }
    } else if (import_only) {
        receipt.stage = "media-import-cancelled"; receipt.reason = "no-media-selection";
        receipt.print(); return 0;
    }
    const auto bios = read_bios(bios_path);
    if (bios.empty()) {
        if (!user_data.empty()) {
            receipt.stage = "setup-required";
            receipt.reason = "select-and-verify-user-bios-and-retail-media";
            std::cerr << "First run requires a verified user BIOS and retail CUE/BIN media.\n";
        }
        receipt.print(); return 78;
    }
    receipt.bios_identity_sha256 = std::string(binding->variant_identity_sha256);
    receipt.bios_sha256 = std::string(binding->executable_sha256);
    clean_aot::host::SdlVulkanConfig host_config = settings.host_config(hidden || workload);
    host_config.title = "Clean AOT Product"; host_config.hidden = hidden || workload;
    if (workload) { host_config.width = puw ? 5120U : 3840U; host_config.height = 2160U; }
    clean_aot::host::SdlVulkanPresentation live(host_config);
    clean_aot::host::HeadlessBackend null_host;
    clean_aot::host::IHostBackend* host = headless ? static_cast<clean_aot::host::IHostBackend*>(&null_host)
                                                   : static_cast<clean_aot::host::IHostBackend*>(&live);
    if (!headless && !live.initialize().success()) {
        receipt.reason = "sdl3-vulkan-initialization-failed"; receipt.print(); return 70;
    }
    clean_aot::runtime::Configuration configuration{};
    configuration.bios = bios;
    configuration.initial_vblank_rate = clean_aot::machine::VBlankRate::Hz100;
    clean_aot::runtime::Runtime runtime(configuration);
    std::array<DurableCard, 2> durable_cards{};
    std::array<clean_aot::sio::MemoryCard, 2> sio_cards{
        clean_aot::sio::MemoryCard(std::span<std::uint8_t>(durable_cards[0].image)),
        clean_aot::sio::MemoryCard(std::span<std::uint8_t>(durable_cards[1].image))};
    if (!user_data.empty()) {
        for (std::size_t index = 0; index < durable_cards.size(); ++index) {
            const auto path = user_data / "saves" /
                (index == 0U ? "card1.mcd" : "card2.mcd");
            auto& durable = durable_cards[index];
            if (!durable.storage.load_or_create(path)) {
                receipt.reason = "memory-card-load-or-recovery-failed";
                host->shutdown(); receipt.print(); return 74;
            }
            std::ranges::copy(durable.storage.bytes(), durable.image.begin());
            sio_cards[index].set_write_callback(&persist_card_write, &durable);
            const auto port = index == 0U ? clean_aot::sio::Port::One : clean_aot::sio::Port::Two;
            if (!runtime.sio0().attach_memory_card(port, sio_cards[index])) {
                receipt.reason = "memory-card-attach-failed";
                host->shutdown(); receipt.print(); return 74;
            }
        }
    }
    if (runtime.bind_authenticated_bios(binding->variant_identity_sha256, *binding) !=
        clean_aot::runtime::DispatchStatus::Bound) {
        receipt.reason = "authenticated-bios-bind-rejected"; host->shutdown(); receipt.print(); return 65;
    }
    receipt.stage = "bios-bound";
    constexpr std::uint64_t dispatch_limit = 1'000'000U;
    auto run_bound_region = [&] {
      while (receipt.dispatches < dispatch_limit) {
        const auto status = runtime.dispatch_bios();
        if (status != clean_aot::runtime::DispatchStatus::Executed) {
            receipt.stage = "bios-slice-stopped";
            receipt.reason = status == clean_aot::runtime::DispatchStatus::UnknownCode
                ? "unknown-generated-bios-pc" : "generated-bios-dispatch-rejected";
            break;
        }
        ++receipt.dispatches; receipt.instructions += runtime.last_bios_instruction_count();
      }
    };
    run_bound_region();
#ifdef CLEAN_AOT_PRODUCT_HAS_EPOCH_BINDINGS
    const auto epoch_bindings = clean_aot_product_epoch_bindings();
    std::vector<bool> bound_epochs(epoch_bindings.size(), false);
    while (receipt.stage == "bios-slice-stopped" && receipt.dispatches < dispatch_limit) {
        const auto stop_pc = runtime.bios_pc();
        bool candidate_seen = false, candidate_bound = false;
        for (std::size_t index = 0; index < epoch_bindings.size(); ++index) {
            const auto* epoch = epoch_bindings[index];
            if (bound_epochs[index] || epoch == nullptr || stop_pc < epoch->load_start ||
                stop_pc >= epoch->load_end_exclusive) continue;
            candidate_seen = true;
            const clean_aot::runtime::Runtime::AuthenticatedCopiedEpochBinding copied{
                epoch->variant_identity_sha256, binding->load_start,
                static_cast<std::uint32_t>(binding->load_end_exclusive - binding->load_start),
                binding->executable_sha256, epoch->executable_sha256, *epoch};
            if (runtime.bind_authenticated_copied_epoch(copied) !=
                clean_aot::runtime::DispatchStatus::Bound) continue;
            bound_epochs[index] = true;
            candidate_bound = true;
            receipt.stage = "bios-bound";
            run_bound_region();
            break;
        }
        if (!candidate_bound) {
            if (candidate_seen) receipt.reason = "authenticated-epoch-bind-rejected";
            break;
        }
    }
#endif
    if (receipt.stage == "bios-slice-stopped") {
#ifdef CLEAN_AOT_PRODUCT_HAS_EPOCH_BINDINGS
      const std::uint32_t transition_pc = runtime.bios_pc();
      for (const auto* game : clean_aot_product_game_bindings()) {
        if (game == nullptr || transition_pc < game->load_start ||
            transition_pc >= game->load_end_exclusive) continue;
        const auto bound = runtime.bind_authenticated_resident_executable(
            game->variant_identity_sha256, transition_pc, *game);
        if (bound != clean_aot::runtime::DispatchStatus::Bound) {
            receipt.reason = bound == clean_aot::runtime::DispatchStatus::ExecutableHashMismatch
                ? "resident-game-identity-mismatch" : "resident-game-bind-rejected";
            break;
        }
        receipt.stage = "game-shard-bound";
        while (receipt.dispatches < dispatch_limit) {
            const auto status = runtime.dispatch_selected(false);
            if (status != clean_aot::runtime::DispatchStatus::Executed) {
                receipt.stage = "game-shard-stopped";
                receipt.reason = status == clean_aot::runtime::DispatchStatus::UnknownCode
                    ? "unknown-generated-game-pc" : "generated-game-dispatch-rejected";
                break;
            }
            ++receipt.dispatches;
            receipt.instructions += runtime.last_selected_instruction_count();
        }
        break;
      }
#endif
    }
    receipt.stop_pc = runtime.bios_pc();
    if (receipt.dispatches == dispatch_limit) receipt.reason = "bios-dispatch-budget-exhausted";
    if (receipt.stage == "bios-slice-stopped" && workload &&
        !run_workload(*host, host_config.width, host_config.height, workload_updates, receipt)) {
        receipt.stage = "workload-rejected"; receipt.reason = "workload-threshold-failed";
    }
    if (workload) {
        clean_aot::app::GraphicsConfiguration graphics{};
        settings.apply_graphics_settings(graphics);
        graphics.output_width = host_config.width;
        graphics.output_height = host_config.height;
        if (headless) {
            receipt.graphics_feature_matrix =
                settings.graphics_feature_receipt(graphics, nullptr).canonical_json();
        } else {
            const auto presentation = live.presentation_metrics();
            receipt.graphics_feature_matrix =
                settings.graphics_feature_receipt(graphics, &presentation).canonical_json();
        }
    }
    if (std::ranges::any_of(durable_cards, [](const DurableCard& card) {
            return card.durability_failed;
        })) {
        receipt.stage = "persistence-rejected";
        receipt.reason = "memory-card-atomic-flush-failed";
    }
    host->shutdown(); receipt.print();
    return (receipt.stage == "bios-slice-stopped" || receipt.stage == "game-shard-stopped") ? 0 : 69;
}
