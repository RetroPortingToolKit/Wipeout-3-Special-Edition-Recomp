#include "clean_aot/host/headless.hpp"
#include "clean_aot/host/pacing.hpp"
#include "clean_aot/host/sdl_vulkan.hpp"
#include "clean_aot/runtime/runtime.hpp"

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

#ifdef CLEAN_AOT_PRODUCT_BINDING_SYMBOL
extern "C" const clean_aot::runtime::GeneratedExecutableBinding*
CLEAN_AOT_PRODUCT_BINDING_SYMBOL() noexcept;
#endif

namespace {
struct Receipt final {
    std::string bios_identity_sha256, bios_sha256, workload_profile;
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
          << "\",\"schema\":\"clean-aot.bootstrap-receipt.v1\",\"stage\":\""
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

int main(int argc, char** argv) {
    std::filesystem::path user_data, bios_path;
    std::string profile;
    std::uint64_t workload_updates = 0U;
    bool hidden = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--user-data" || argument == "--bios") && index + 1 < argc) {
            const auto value = std::filesystem::path(argv[++index]);
            if (argument == "--user-data") user_data = value; else bios_path = value;
        } else if (argument == "--smoke-hidden") hidden = true;
        else if (argument == "--workload-profile" && index + 1 < argc) profile = argv[++index];
        else if (argument == "--workload-updates" && index + 1 < argc) {
            try { workload_updates = std::stoull(argv[++index]); } catch (...) { return 64; }
        } else { std::cerr << "unsupported product argument\n"; return 64; }
    }
    Receipt receipt{}; receipt.workload_profile = profile;
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
    clean_aot::host::SdlVulkanConfig host_config{};
    host_config.title = "Clean AOT Product"; host_config.hidden = hidden || workload;
    host_config.width = puw ? 5120U : 3840U; host_config.height = 2160U;
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
    receipt.stop_pc = runtime.bios_pc();
    if (receipt.dispatches == dispatch_limit) receipt.reason = "bios-dispatch-budget-exhausted";
    if (receipt.stage == "bios-slice-stopped" && workload &&
        !run_workload(*host, host_config.width, host_config.height, workload_updates, receipt)) {
        receipt.stage = "workload-rejected"; receipt.reason = "workload-threshold-failed";
    }
    host->shutdown(); receipt.print();
    return receipt.stage == "bios-slice-stopped" ? 0 : 69;
}
