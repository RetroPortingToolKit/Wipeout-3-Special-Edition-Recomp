#include "clean_aot/host/headless.hpp"
#include "clean_aot/host/pacing.hpp"
#include "clean_aot/host/sdl_vulkan.hpp"
#include "clean_aot/runtime/runtime.hpp"
#include "clean_aot/app/graphics.hpp"
#include "clean_aot/app/settings.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef CLEAN_AOT_PRODUCT_HAS_EPOCH_BINDINGS
#include "product_epoch_bindings.hpp"
#endif

#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
extern "C" const clean_aot::runtime::GeneratedExecutableBinding*
CLEAN_AOT_PRODUCT_BINDING_SYMBOL() noexcept;
#endif

namespace {
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
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::filesystem::path user_data, bios_path;
    std::filesystem::path settings_path;
    std::string profile;
    std::vector<std::string> settings_overrides;
    std::uint64_t workload_updates = 0U;
    bool hidden = false, reset_settings = false;
    Receipt receipt{};
    for (int index = 1; index < argc; ++index) {
        const std::string argument =
#ifdef _WIN32
            utf8(argv[index]);
#else
            argv[index];
#endif
        if ((argument == "--user-data" || argument == "--bios") && index + 1 < argc) {
            const auto value = std::filesystem::path(argv[++index]);
            if (argument == "--user-data") user_data = value; else bios_path = value;
        } else if (argument == "--smoke-hidden") hidden = true;
        else if (argument == "--settings" && index + 1 < argc) settings_path = std::filesystem::path(argv[++index]);
        else if (argument == "--set" && index + 1 < argc) settings_overrides.emplace_back(
#ifdef _WIN32
            utf8(argv[++index])
#else
            argv[++index]
#endif
        );
        else if (argument == "--reset-settings") reset_settings = true;
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
    clean_aot::app::RuntimeSettings settings{};
    if (settings_path.empty() && !user_data.empty()) settings_path = user_data / "config.json";
    if (!settings_path.empty()) {
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
    if (bios_path.empty() && !user_data.empty()) bios_path = read_user_bios_reference(user_data);
    const auto bios = read_bios(bios_path);
    if (bios.empty()) {
        if (!user_data.empty()) {
            receipt.stage = "setup-required";
            receipt.reason = "select-and-verify-user-bios-and-retail-media";
            std::cerr << "First run requires verified user BIOS and retail CUE/BIN media; "
                         "follow policy/first-run-setup.json.\n";
        }
        receipt.print(); return 78;
    }
#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
    const auto* binding = CLEAN_AOT_PRODUCT_BINDING_SYMBOL();
#else
    const clean_aot::runtime::GeneratedExecutableBinding* binding = nullptr;
#endif
    if (!binding) { receipt.reason = "missing-authenticated-generated-bios-binding"; receipt.print(); return 78; }
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
    host->shutdown(); receipt.print();
    return (receipt.stage == "bios-slice-stopped" || receipt.stage == "game-shard-stopped") ? 0 : 69;
}
