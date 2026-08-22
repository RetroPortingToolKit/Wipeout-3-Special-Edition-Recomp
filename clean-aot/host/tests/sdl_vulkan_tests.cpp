#include "clean_aot/host/sdl_vulkan.hpp"
#include "clean_aot/host/direct_vulkan_bridge.hpp"
#include "present_shaders.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <string_view>

namespace {
using namespace clean_aot::host;
class MockPresentWaitProbe final : public IPresentCompletionProbe {
public:
    explicit MockPresentWaitProbe(PresentCompletionSource source =
        PresentCompletionSource::present_wait) noexcept : source_(source) {}
    [[nodiscard]] PresentCompletionSource source() const noexcept override {
        return source_;
    }
    void reset(std::uint64_t generation) noexcept override { generation_ = generation; }
    void submitted(std::uint64_t generation, std::uint64_t present_id) noexcept override {
        pending_ = PresentCompletion{generation, present_id,
            1'000'000'000U + (present_id + 1U) * 10'000'000U};
        has_pending_ = true;
    }
    [[nodiscard]] bool poll(PresentCompletion& completion) noexcept override {
        if (!has_pending_) return false;
        completion = pending_; has_pending_ = false; return true;
    }
    void forge_stale() noexcept {
        pending_ = PresentCompletion{generation_ - 1U, 3U, 2'000'000'000U};
        has_pending_ = true;
    }
private:
    PresentCompletion pending_{};
    PresentCompletionSource source_ = PresentCompletionSource::present_wait;
    std::uint64_t generation_ = 0U;
    bool has_pending_ = false;
};

[[nodiscard]] bool service_until_submitted(SdlVulkanPresentation& backend,
                                            std::uint64_t count) {
    for (std::uint32_t attempt = 0U; attempt < 512U; ++attempt) {
        const HostStatus status = backend.service();
        if (!status.success() && status.code != HostCode::not_ready) return false;
        if (backend.presentation_metrics().gpu_present_submissions == count) return true;
    }
    return false;
}
} // namespace

int main() {
    if (std::string_view{clean_aot::host::shaders::kPresentFragmentSpirvSha256} !=
        "ccfdfe170a126a56ff2566d8258b874e3bca59983d616ff71cb2e5338730e1f6") return 31;
    using namespace clean_aot::host;
    SdlVulkanConfig config{};
    config.title = "clean-aot hidden 100 Hz Vulkan acceptance";
    config.width = 3840U; config.height = 2160U;
    config.fullscreen = FullscreenMode::windowed;
    config.aspect = AspectPolicy::preserve_4x3;
    config.hidden = true;
    config.present_policy = SdlPresentPolicy::low_latency_100hz;
    config.target_refresh_hz = kUpdateRateHz;
    config.allowed_frames_in_flight = 1U;
    SdlVulkanPresentation hidden_backend(config);
    if (!hidden_backend.initialize().success()) return 1;
    const SdlPresentationMetrics initial = hidden_backend.presentation_metrics();
    if (initial.target_refresh_hz != 100U || initial.allowed_frames_in_flight != 1U ||
        initial.active_present_mode == SdlActivePresentMode::unknown ||
        initial.display_presentations_confirmed != 0U || initial.vrr_status_known) return 2;

    const std::array<std::uint8_t, 12U> rgb{
        255U,0U,0U, 0U,255U,0U, 0U,0U,255U, 255U,255U,255U};
    const FrameView surface{rgb.data(),static_cast<std::uint32_t>(rgb.size()),
        2U,2U,6U,PixelFormat::rgb8_unorm};
    if (!hidden_backend.submit_frame({0U,0U,surface}).success() ||
        hidden_backend.service().code != HostCode::not_ready) return 3;
    const SdlPresentationMetrics hidden_metrics = hidden_backend.presentation_metrics();
    if (hidden_metrics.frames_accepted != 1U ||
        hidden_metrics.swapchain_images_acquired != 0U ||
        hidden_metrics.gpu_present_submissions != 0U ||
        hidden_metrics.acquire_not_ready != 1U) return 4;
    hidden_backend.shutdown();

    // A hidden SDL window intentionally has no presentable swapchain image.
    // Use a real shown swapchain for the GPU submission acceptance tranche.
    config.hidden = false;
    SdlVulkanPresentation backend(config);
    if (!backend.initialize().success()) return 5;
    if (!backend.submit_frame({7U,0U,surface}).success()) return 3;
    if (backend.service().code != HostCode::sequence_error) return 4;
    const auto gpu_effect_begin = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < 100U; ++index) {
        FrameSubmission transformed{index,index,surface};
        transformed.presentation_width = 2880U;
        transformed.presentation_height = 2160U;
        transformed.presentation_effects = kPresentationEffectEdgeAware;
        if (!backend.submit_frame(transformed).success()) return 5;
        if (!service_until_submitted(backend,index+1U)) return 6;
    }
    const auto gpu_effect_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - gpu_effect_begin).count();
    std::cout << "real Vulkan 4K effect submissions=100 elapsed_ns="
              << gpu_effect_ns << '\n';
    SdlPresentationMetrics metrics = backend.presentation_metrics();
    if (metrics.frames_accepted != 101U || metrics.swapchain_images_acquired != 100U ||
        metrics.gpu_present_submissions != 100U ||
        metrics.display_presentations_confirmed != 0U ||
        metrics.next_sequence != 100U || metrics.next_update_index != 100U ||
        metrics.sequence_errors != 1U || metrics.queue_latency_ns_max == 0U ||
        metrics.gpu_effect_frames != 100U ||
        metrics.gpu_effect_output_pixels != 100ULL * 2880ULL * 2160ULL ||
        metrics.frame_upload_bytes != 100U * 2U * 2U * 4U) return 7;
    if (!backend.request_window({{5120U,2160U,FullscreenMode::windowed}}).success() ||
        !backend.recreate_swapchain().success()) return 8;
    if (!backend.submit_frame({100U,100U,surface}).success() ||
        !service_until_submitted(backend,101U)) return 9;
    metrics = backend.presentation_metrics();
    if (metrics.swapchain_recreations < 1U || metrics.gpu_present_submissions != 101U) return 10;

    const std::array<std::int16_t, 882U> audio{};
    constexpr std::uint64_t kSoakUpdates = 200U;
    for (std::uint64_t update = 0U; update < kSoakUpdates; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 11;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const SdlAudioMetrics audio_metrics = backend.audio_metrics();
    if (audio_metrics.submitted_updates != kSoakUpdates ||
        audio_metrics.submitted_frames != kSoakUpdates * 441U ||
        audio_metrics.dropped_updates != 0U || audio_metrics.overruns != 0U ||
        !audio_metrics.playback_started || !audio_metrics.exact_44100_stereo ||
        audio_metrics.time_stretch_used || audio_metrics.peak_queued_frames > 3528U) return 12;
    for (std::uint64_t update = kSoakUpdates; update < kSoakUpdates + 32U; ++update) {
        if (!backend.submit_audio({update,audio.data(),441U}).success()) return 13;
    }
    const SdlAudioMetrics saturated = backend.audio_metrics();
    if (saturated.overruns == 0U || saturated.dropped_updates == 0U ||
        saturated.queued_frames > 3528U || saturated.time_stretch_used) return 14;
    const std::array<std::uint8_t, 4U> replacement{1U,2U,3U,255U};
    ReplacementImage image{}; image.width=1U; image.height=1U; image.rgba8=replacement;
    std::uint64_t handle=0U;
    if (!backend.upload_replacement(image,handle).success() || handle==0U ||
        !backend.release_replacement(handle).success()) return 15;
    backend.mark_device_lost();
    if (backend.service().code != HostCode::device_lost ||
        backend.presentation_metrics().device_losses != 1U) return 16;
    backend.shutdown();

    MockPresentWaitProbe probe{};
    config.width = 640U; config.height = 360U;
    config.completion_probe = &probe;
    SdlVulkanPresentation evidenced(config);
    if (!evidenced.initialize().success()) return 17;
    for (std::uint64_t index = 0U; index < 3U; ++index) {
        if (!evidenced.submit_frame({index,index,surface}).success() ||
            !service_until_submitted(evidenced,index+1U)) return 18;
    }
    SdlPresentationMetrics evidence = evidenced.presentation_metrics();
    if (evidence.completion_source != PresentCompletionSource::present_wait ||
        evidence.display_presentations_confirmed != 3U ||
        evidence.last_completed_present_id != 2U ||
        evidence.completion_interval_ns_total != 20'000'000U ||
        evidence.completion_interval_ns_max != 10'000'000U ||
        evidence.completion_intervals_at_target != 2U ||
        evidence.completion_interval_violations != 0U) return 19;
    probe.forge_stale();
    if (evidenced.service().code != HostCode::not_ready) return 20;
    evidence = evidenced.presentation_metrics();
    if (!evidence.completion_evidence_fault ||
        evidence.completion_source != PresentCompletionSource::fault ||
        evidence.display_presentations_confirmed != 3U) return 21;
    evidenced.shutdown();

    MockPresentWaitProbe timing_probe{PresentCompletionSource::display_timing};
    config.completion_probe = &timing_probe;
    SdlVulkanPresentation timed(config);
    if (!timed.initialize().success() ||
        !timed.submit_frame({0U,0U,surface}).success() ||
        !service_until_submitted(timed,1U)) return 22;
    const SdlPresentationMetrics timed_metrics = timed.presentation_metrics();
    if (timed_metrics.completion_source != PresentCompletionSource::display_timing ||
        timed_metrics.display_presentations_confirmed != 1U ||
        timed_metrics.completion_evidence_fault) return 23;
    timed.shutdown();

    if (!SDL_Init(SDL_INIT_VIDEO)) return 24;
    SDL_Window* direct_window = SDL_CreateWindow("direct Vulkan evidence",
        3840, 2160, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (direct_window == nullptr) return 25;
    DirectVulkanPresentBridge direct{};
    if (!direct.initialize(direct_window,3840U,2160U,true).success()) return 26;
    DirectVulkanMetrics direct_metrics = direct.metrics();
    if (!direct_metrics.surface_created || direct_metrics.swapchain_created ||
        !direct_metrics.hidden || direct_metrics.completions != 0U) return 27;
    if (!direct.resize(5120U,2160U,true).success() ||
        direct.metrics().generation != 2U || direct.metrics().recreations != 1U) return 28;
    if (!SDL_ShowWindow(direct_window) ||
        !direct.resize(5120U,2160U,false).success()) return 29;
    direct_metrics = direct.metrics();
    if (!direct_metrics.swapchain_created || direct_metrics.hidden) return 30;
    if (direct.present_clear(2U).code != HostCode::sequence_error) return 31;
    for (std::uint64_t id = 1U; id <= 100U; ++id) {
        if (!direct.present_clear(id).success()) return 32;
    }
    direct_metrics = direct.metrics();
    if (direct_metrics.submissions != 100U ||
        (direct_metrics.source == PresentCompletionSource::unavailable &&
         direct_metrics.completions != 0U) ||
        (direct_metrics.source != PresentCompletionSource::unavailable &&
         (direct_metrics.completions != direct_metrics.submissions ||
          direct_metrics.last_present_id != 100U))) return 33;
    direct.shutdown(); SDL_DestroyWindow(direct_window); SDL_QuitSubSystem(SDL_INIT_VIDEO);
    std::cout << "clean_aot_host_sdl_vulkan_tests: 101 unique GPU submissions; "
                 "physical presentation intentionally unclaimed\n";
    return 0;
}
