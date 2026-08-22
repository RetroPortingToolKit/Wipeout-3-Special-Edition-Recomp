#include "clean_aot/host/sdl_vulkan.hpp"

#include <algorithm>

#if !defined(_WIN32)
#error "The SDL3/Vulkan presentation backend is Windows-only"
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_vulkan.h>

#include "present_shaders.hpp"

namespace clean_aot::host {
namespace {

constexpr std::size_t kFrameQueueDepth = 4U;
constexpr std::size_t kEventQueueDepth = 8U;
constexpr std::size_t kMaxEventsPerService = 32U;

[[nodiscard]] bool valid_frame(const FrameSubmission& frame) noexcept {
    const std::uint32_t bytes = bytes_per_pixel(frame.surface.format);
    if (frame.surface.pixels == nullptr || frame.surface.byte_size == 0U ||
        frame.surface.width == 0U || frame.surface.height == 0U ||
        bytes == 0U) {
        return false;
    }
    const std::uint64_t minimum_pitch =
        static_cast<std::uint64_t>(frame.surface.width) * bytes;
    const std::uint64_t prior_rows =
        static_cast<std::uint64_t>(frame.surface.height) - 1U;
    if (prior_rows != 0U &&
        frame.surface.pitch >
            (std::numeric_limits<std::uint64_t>::max() - minimum_pitch) /
                prior_rows) {
        return false;
    }
    const std::uint64_t required_bytes =
        static_cast<std::uint64_t>(frame.surface.pitch) *
        prior_rows + minimum_pitch;
    return frame.surface.pitch >= minimum_pitch &&
        required_bytes <= frame.surface.byte_size;
}

[[nodiscard]] HostStatus sdl_failure(
    HostCode code = HostCode::internal_error) noexcept {
    return HostStatus{code};
}

[[nodiscard]] bool apply_window_state(
    SDL_Window* window,
    const WindowState& state) noexcept {
    if (window == nullptr || state.pixel_width == 0U ||
        state.pixel_height == 0U ||
        state.pixel_width > static_cast<std::uint32_t>(INT_MAX) ||
        state.pixel_height > static_cast<std::uint32_t>(INT_MAX)) {
        return false;
    }
    if (!SDL_SetWindowSize(
            window,
            static_cast<int>(state.pixel_width),
            static_cast<int>(state.pixel_height))) {
        return false;
    }
    if (state.fullscreen == FullscreenMode::exclusive) {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
        SDL_DisplayMode mode{};
        if (display == 0U || !SDL_GetClosestFullscreenDisplayMode(
                display,
                static_cast<int>(state.pixel_width),
                static_cast<int>(state.pixel_height),
                0.0F,
                false,
                &mode) ||
            !SDL_SetWindowFullscreenMode(window, &mode)) {
            return false;
        }
    } else if (!SDL_SetWindowFullscreenMode(window, nullptr)) {
        return false;
    }
    return SDL_SetWindowFullscreen(
        window, state.fullscreen != FullscreenMode::windowed);
}

} // namespace

struct SdlVulkanPresentation::Impl final {
    struct QueuedFrame final { FrameSubmission frame{}; std::uint64_t accepted_ns = 0U; };
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_AudioStream* audio = nullptr;
    SDL_GPUGraphicsPipeline* present_pipeline = nullptr;
    SDL_GPUSampler* present_sampler = nullptr;
    SDL_GPUTexture* frame_texture = nullptr;
    SDL_GPUTransferBuffer* frame_transfer = nullptr;
    std::uint32_t frame_width = 0U;
    std::uint32_t frame_height = 0U;
    std::uint32_t frame_transfer_bytes = 0U;
    struct ReplacementTexture final {
        SDL_GPUTexture* texture = nullptr;
        std::uint64_t handle = 0U;
    };
    std::vector<ReplacementTexture> replacements{};
    std::uint64_t next_replacement_handle = 1U;
    HMODULE vulkan_loader = nullptr;
    FixedSpscQueue<QueuedFrame, kFrameQueueDepth> frames{};
    FixedSpscQueue<HostEvent, kEventQueueDepth> events{};
    FrameSubmission pending_frame{};
    std::uint64_t pending_frame_accepted_ns = 0U;
    HostEvent pending_event{};
    WindowState window_state{};
    FullscreenMode requested_fullscreen = FullscreenMode::windowed;
    bool has_pending_frame = false;
    bool has_pending_event = false;
    bool sdl_initialized = false;
    bool sdl_vulkan_loaded = false;
    bool window_claimed = false;
    bool initialized = false;
    bool shutting_down = false;
    bool device_lost = false;
    bool swapchain_recreation_requested = false;
    std::uint64_t next_sequence = 0U;
    std::uint64_t next_update_index = 0U;
    std::uint64_t next_audio_update = 0U;
    SdlAudioMetrics audio_metrics{};
    ControllerState controller{};
    SdlPresentationMetrics metrics{};
};

namespace {

template <typename ImplType>
[[nodiscard]] bool configure_presentation(ImplType& impl,
    const SdlVulkanConfig& config) noexcept {
    SDL_GPUPresentMode selected = SDL_GPU_PRESENTMODE_VSYNC;
    switch (config.present_policy) {
    case SdlPresentPolicy::vsync: break;
    case SdlPresentPolicy::immediate:
        if (!SDL_WindowSupportsGPUPresentMode(impl.device, impl.window,
                SDL_GPU_PRESENTMODE_IMMEDIATE)) return false;
        selected = SDL_GPU_PRESENTMODE_IMMEDIATE; break;
    case SdlPresentPolicy::mailbox:
        if (!SDL_WindowSupportsGPUPresentMode(impl.device, impl.window,
                SDL_GPU_PRESENTMODE_MAILBOX)) return false;
        selected = SDL_GPU_PRESENTMODE_MAILBOX; break;
    case SdlPresentPolicy::low_latency_100hz:
        if (SDL_WindowSupportsGPUPresentMode(impl.device, impl.window,
                SDL_GPU_PRESENTMODE_MAILBOX)) selected = SDL_GPU_PRESENTMODE_MAILBOX;
        else if (SDL_WindowSupportsGPUPresentMode(impl.device, impl.window,
                     SDL_GPU_PRESENTMODE_IMMEDIATE)) selected = SDL_GPU_PRESENTMODE_IMMEDIATE;
        break;
    }
    if (!SDL_SetGPUSwapchainParameters(impl.device, impl.window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR, selected)) return false;
    impl.metrics.active_present_mode = selected == SDL_GPU_PRESENTMODE_MAILBOX
        ? SdlActivePresentMode::mailbox : selected == SDL_GPU_PRESENTMODE_IMMEDIATE
            ? SdlActivePresentMode::immediate : SdlActivePresentMode::vsync;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(impl.window);
    const SDL_DisplayMode* mode = display == 0U ? nullptr : SDL_GetCurrentDisplayMode(display);
    if (mode != nullptr && mode->refresh_rate_numerator > 0 &&
        mode->refresh_rate_denominator > 0) {
        impl.metrics.display_refresh_numerator = static_cast<std::uint32_t>(mode->refresh_rate_numerator);
        impl.metrics.display_refresh_denominator = static_cast<std::uint32_t>(mode->refresh_rate_denominator);
        impl.metrics.exact_target_refresh =
            static_cast<std::uint64_t>(mode->refresh_rate_numerator) ==
            static_cast<std::uint64_t>(config.target_refresh_hz) *
            static_cast<std::uint64_t>(mode->refresh_rate_denominator);
    }
    return true;
}

template <typename ImplType>
[[nodiscard]] bool open_audio_stream(ImplType& impl) noexcept {
    const SDL_AudioSpec audio_spec{SDL_AUDIO_S16,
        static_cast<int>(kAudioChannels), static_cast<int>(kAudioSampleRateHz)};
    impl.audio = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio_spec, nullptr, nullptr);
    if (impl.audio == nullptr || !SDL_PauseAudioStreamDevice(impl.audio)) {
        if (impl.audio != nullptr) SDL_DestroyAudioStream(impl.audio);
        impl.audio = nullptr;
        return false;
    }
    impl.audio_metrics.playback_started = false;
    impl.audio_metrics.queued_frames = 0U;
    return true;
}

template <typename ImplType>
[[nodiscard]] bool reopen_audio_stream(ImplType& impl) noexcept {
    if (impl.audio != nullptr) SDL_DestroyAudioStream(impl.audio);
    impl.audio = nullptr;
    if (!open_audio_stream(impl)) return false;
    ++impl.audio_metrics.device_reopens;
    return true;
}

template <typename ImplType>
[[nodiscard]] bool create_texture_upload_resources(
    ImplType& impl, std::uint32_t width,
    std::uint32_t height) noexcept {
    const std::uint64_t bytes64 = static_cast<std::uint64_t>(width) * height * 4U;
    if (width == 0U || height == 0U || bytes64 > std::numeric_limits<Uint32>::max()) {
        return false;
    }
    if (impl.frame_texture != nullptr && impl.frame_width == width &&
        impl.frame_height == height) return true;
    if (impl.frame_texture != nullptr) SDL_ReleaseGPUTexture(impl.device, impl.frame_texture);
    if (impl.frame_transfer != nullptr) SDL_ReleaseGPUTransferBuffer(impl.device, impl.frame_transfer);
    impl.frame_texture = nullptr; impl.frame_transfer = nullptr;
    const SDL_GPUTextureCreateInfo texture_info{SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
        width, height, 1U, 1U, SDL_GPU_SAMPLECOUNT_1, 0U};
    impl.frame_texture = SDL_CreateGPUTexture(impl.device, &texture_info);
    const SDL_GPUTransferBufferCreateInfo transfer_info{
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(bytes64), 0U};
    impl.frame_transfer = SDL_CreateGPUTransferBuffer(impl.device, &transfer_info);
    if (impl.frame_texture == nullptr || impl.frame_transfer == nullptr) return false;
    impl.frame_width = width; impl.frame_height = height;
    impl.frame_transfer_bytes = static_cast<std::uint32_t>(bytes64);
    return true;
}

[[nodiscard]] bool copy_frame_rgba(const FrameView& frame,
                                   std::uint8_t* output) noexcept {
    for (std::uint32_t y = 0U; y < frame.height; ++y) {
        const std::uint8_t* source = frame.pixels + static_cast<std::size_t>(y) * frame.pitch;
        std::uint8_t* destination = output +
            static_cast<std::size_t>(y) * frame.width * 4U;
        for (std::uint32_t x = 0U; x < frame.width; ++x) {
            switch (frame.format) {
            case PixelFormat::rgba8_unorm:
                std::memcpy(destination, source, 4U); source += 4U; break;
            case PixelFormat::bgra8_unorm:
                destination[0] = source[2]; destination[1] = source[1];
                destination[2] = source[0]; destination[3] = source[3]; source += 4U; break;
            case PixelFormat::rgb8_unorm:
                destination[0] = source[0]; destination[1] = source[1];
                destination[2] = source[2]; destination[3] = 255U; source += 3U; break;
            case PixelFormat::rgb565: {
                const std::uint16_t value = static_cast<std::uint16_t>(source[0]) |
                    static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8U);
                destination[0] = static_cast<std::uint8_t>(((value >> 11U) & 31U) * 255U / 31U);
                destination[1] = static_cast<std::uint8_t>(((value >> 5U) & 63U) * 255U / 63U);
                destination[2] = static_cast<std::uint8_t>((value & 31U) * 255U / 31U);
                destination[3] = 255U; source += 2U; break;
            }
            case PixelFormat::indexed8:
                return false;
            }
            destination += 4U;
        }
    }
    return true;
}

template <typename ImplType>
[[nodiscard]] bool encode_default_frame(ImplType& impl,
    SDL_GPUCommandBuffer* command_buffer, SDL_GPUTexture* swapchain_texture,
    const FrameView& frame, const Viewport& viewport) noexcept {
    if (!create_texture_upload_resources(impl, frame.width, frame.height)) return false;
    auto* mapped = static_cast<std::uint8_t*>(
        SDL_MapGPUTransferBuffer(impl.device, impl.frame_transfer, true));
    if (mapped == nullptr || !copy_frame_rgba(frame, mapped)) {
        if (mapped != nullptr) SDL_UnmapGPUTransferBuffer(impl.device, impl.frame_transfer);
        return false;
    }
    SDL_UnmapGPUTransferBuffer(impl.device, impl.frame_transfer);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
    if (copy == nullptr) return false;
    const SDL_GPUTextureTransferInfo source{impl.frame_transfer, 0U, frame.width, frame.height};
    const SDL_GPUTextureRegion destination{impl.frame_texture, 0U, 0U, 0U, 0U, 0U,
                                            frame.width, frame.height, 1U};
    SDL_UploadToGPUTexture(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo target{}; target.texture = swapchain_texture;
    target.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F};
    target.load_op = SDL_GPU_LOADOP_CLEAR; target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &target, 1U, nullptr);
    if (pass == nullptr) return false;
    SDL_BindGPUGraphicsPipeline(pass, impl.present_pipeline);
    const SDL_GPUTextureSamplerBinding binding{impl.frame_texture, impl.present_sampler};
    SDL_BindGPUFragmentSamplers(pass, 0U, &binding, 1U);
    const SDL_GPUViewport gpu_viewport{static_cast<float>(viewport.x),
        static_cast<float>(viewport.y), static_cast<float>(viewport.width),
        static_cast<float>(viewport.height), 0.0F, 1.0F};
    SDL_SetGPUViewport(pass, &gpu_viewport);
    SDL_DrawGPUPrimitives(pass, 3U, 1U, 0U, 0U);
    SDL_EndGPURenderPass(pass);
    return true;
}

} // namespace

SdlVulkanPresentation::SdlVulkanPresentation(
    const SdlVulkanConfig& config) noexcept
    : config_(config) {}

SdlVulkanPresentation::~SdlVulkanPresentation() {
    shutdown();
}

HostStatus SdlVulkanPresentation::initialize() noexcept {
    if (impl_ != nullptr || config_.width == 0U || config_.height == 0U ||
        config_.target_refresh_hz == 0U || config_.allowed_frames_in_flight < 1U ||
        config_.allowed_frames_in_flight > 3U) {
        return sdl_failure(HostCode::invalid_argument);
    }

    Impl* impl = new (std::nothrow) Impl{};
    if (impl == nullptr) {
        return sdl_failure();
    }
    impl_ = impl;
    impl_->window_state = WindowState{
        config_.width, config_.height, config_.fullscreen};
    impl_->requested_fullscreen = config_.fullscreen;
    impl_->metrics.target_refresh_hz = config_.target_refresh_hz;
    impl_->metrics.allowed_frames_in_flight = config_.allowed_frames_in_flight;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        shutdown();
        return sdl_failure();
    }
    impl_->sdl_initialized = true;
    if (config_.audio_target_buffer_frames < kAudioFramesPerUpdate ||
        config_.audio_max_buffer_frames < config_.audio_target_buffer_frames ||
        !open_audio_stream(*impl_)) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }

    // LoadLibraryExW with a system-only search policy is intentional: the
    // runtime only needs the Windows Vulkan loader. No Vulkan SDK import
    // library or driver SDK path is linked.
    impl_->vulkan_loader = LoadLibraryExW(
        L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (impl_->vulkan_loader == nullptr ||
        GetProcAddress(impl_->vulkan_loader, "vkGetInstanceProcAddr") == nullptr) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }
    if (!SDL_Vulkan_LoadLibrary("vulkan-1.dll")) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }
    impl_->sdl_vulkan_loaded = true;

    const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
        (config_.hidden ? SDL_WINDOW_HIDDEN : 0U);
    impl_->window = SDL_CreateWindow(
        config_.title,
        static_cast<int>(config_.width),
        static_cast<int>(config_.height),
        flags);
    if (impl_->window == nullptr) {
        shutdown();
        return sdl_failure();
    }
    if (!apply_window_state(impl_->window, impl_->window_state)) {
        shutdown();
        return sdl_failure();
    }

    impl_->device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV,
        config_.debug_gpu,
        "vulkan");
    if (impl_->device == nullptr) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window)) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }
    impl_->window_claimed = true;
    if (!configure_presentation(*impl_, config_) ||
        !SDL_SetGPUAllowedFramesInFlight(impl_->device,
            config_.allowed_frames_in_flight)) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }

    // Validate that the checked-in SPIR-V is consumable by the selected
    // Vulkan device. The actual game presentation pipeline is deliberately
    // supplied by IFrameEncoder in a later tranche.
    SDL_GPUShaderCreateInfo vertex_info{};
    vertex_info.code_size = sizeof(shaders::kPresentVertexSpirv);
    vertex_info.code = reinterpret_cast<const Uint8*>(
        shaders::kPresentVertexSpirv);
    vertex_info.entrypoint = "VSMain";
    vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    SDL_GPUShader* vertex_shader =
        SDL_CreateGPUShader(impl_->device, &vertex_info);

    SDL_GPUShaderCreateInfo fragment_info{};
    fragment_info.code_size = sizeof(shaders::kPresentFragmentSpirv);
    fragment_info.code = reinterpret_cast<const Uint8*>(
        shaders::kPresentFragmentSpirv);
    fragment_info.entrypoint = "PSMain";
    fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    fragment_info.num_samplers = 1U;
    SDL_GPUShader* fragment_shader =
        SDL_CreateGPUShader(impl_->device, &fragment_info);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        if (vertex_shader != nullptr) SDL_ReleaseGPUShader(impl_->device, vertex_shader);
        if (fragment_shader != nullptr) SDL_ReleaseGPUShader(impl_->device, fragment_shader);
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = SDL_GetGPUSwapchainTextureFormat(
        impl_->device, impl_->window);
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1U;
    impl_->present_pipeline = SDL_CreateGPUGraphicsPipeline(
        impl_->device, &pipeline_info);
    SDL_ReleaseGPUShader(impl_->device, vertex_shader);
    SDL_ReleaseGPUShader(impl_->device, fragment_shader);

    SDL_GPUSamplerCreateInfo sampler_info{};
    const SDL_GPUFilter presentation_filter =
        config_.presentation_filter == PresentationFilter::Linear
            ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    sampler_info.min_filter = presentation_filter;
    sampler_info.mag_filter = presentation_filter;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    impl_->present_sampler = SDL_CreateGPUSampler(impl_->device, &sampler_info);
    if (impl_->present_pipeline == nullptr || impl_->present_sampler == nullptr) {
        shutdown();
        return sdl_failure(HostCode::unsupported);
    }

    impl_->initialized = true;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::submit_frame(
    const FrameSubmission& frame) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (!valid_frame(frame)) {
        return sdl_failure(HostCode::invalid_argument);
    }
    const Impl::QueuedFrame queued{frame, SDL_GetTicksNS()};
    if (!impl_->frames.try_push(queued)) {
        return sdl_failure(HostCode::queue_full);
    }
    ++impl_->metrics.frames_accepted;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::submit_audio(
    const StereoAudioBatch& audio) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (audio.interleaved_stereo == nullptr ||
        audio.sample_frames != kAudioFramesPerUpdate) {
        return sdl_failure(HostCode::invalid_argument);
    }
    if (audio.update_index != impl_->next_audio_update) {
        impl_->next_audio_update = audio.update_index + 1U;
        return sdl_failure(HostCode::sequence_error);
    }
    const int queued_bytes = SDL_GetAudioStreamQueued(impl_->audio);
    if (queued_bytes < 0) {
        if (!reopen_audio_stream(*impl_)) return sdl_failure(HostCode::device_lost);
    }
    const std::uint32_t bytes_per_frame = kAudioChannels *
        static_cast<std::uint32_t>(sizeof(std::int16_t));
    const std::uint32_t queued_frames = queued_bytes < 0 ? 0U :
        static_cast<std::uint32_t>(queued_bytes) / bytes_per_frame;
    if (impl_->audio_metrics.playback_started && queued_frames == 0U) {
        ++impl_->audio_metrics.underruns;
    }
    if (queued_frames > config_.audio_max_buffer_frames - audio.sample_frames) {
        ++impl_->audio_metrics.overruns;
        ++impl_->audio_metrics.dropped_updates;
        ++impl_->next_audio_update;
        impl_->audio_metrics.queued_frames = queued_frames;
        return HostStatus{};
    }
    const std::uint32_t byte_count = audio.sample_frames * kAudioChannels *
        static_cast<std::uint32_t>(sizeof(std::int16_t));
    if (!SDL_PutAudioStreamData(impl_->audio, audio.interleaved_stereo,
                                static_cast<int>(byte_count))) {
        if (!reopen_audio_stream(*impl_) ||
            !SDL_PutAudioStreamData(impl_->audio, audio.interleaved_stereo,
                                    static_cast<int>(byte_count))) {
            return sdl_failure(HostCode::device_lost);
        }
    }
    ++impl_->audio_metrics.submitted_updates;
    impl_->audio_metrics.submitted_frames += audio.sample_frames;
    impl_->audio_metrics.queued_frames = queued_frames + audio.sample_frames;
    impl_->audio_metrics.peak_queued_frames = std::max(
        impl_->audio_metrics.peak_queued_frames,
        impl_->audio_metrics.queued_frames);
    if (!impl_->audio_metrics.playback_started &&
        impl_->audio_metrics.queued_frames >= config_.audio_target_buffer_frames) {
        if (!SDL_ResumeAudioStreamDevice(impl_->audio)) {
            return sdl_failure(HostCode::device_lost);
        }
        impl_->audio_metrics.playback_started = true;
    }
    ++impl_->next_audio_update;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::submit_controller(
    const ControllerState& state) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    impl_->controller = state;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::request_window(
    const WindowRequest& request) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (request.state.pixel_width == 0U || request.state.pixel_height == 0U ||
        request.state.pixel_width > static_cast<std::uint32_t>(INT_MAX) ||
        request.state.pixel_height > static_cast<std::uint32_t>(INT_MAX)) {
        return sdl_failure(HostCode::invalid_argument);
    }
    if (!apply_window_state(impl_->window, request.state)) {
        return sdl_failure();
    }
    impl_->window_state = request.state;
    impl_->requested_fullscreen = request.state.fullscreen;
    return HostStatus{};
}

void SdlVulkanPresentation::mark_device_lost() noexcept {
    if (impl_ == nullptr || impl_->shutting_down || impl_->device_lost) {
        return;
    }
    impl_->device_lost = true;
    ++impl_->metrics.device_losses;
    const HostEvent event{
        HostEventKind::device_lost,
        impl_->window_state,
        HostCode::device_lost};
    if (!impl_->events.try_push(event) && !impl_->has_pending_event) {
        impl_->pending_event = event;
        impl_->has_pending_event = true;
    }
}

HostStatus SdlVulkanPresentation::recreate_swapchain() noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (!SDL_WaitForGPUIdle(impl_->device)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
    impl_->window_claimed = false;
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    impl_->window_claimed = true;
    if (!configure_presentation(*impl_, config_)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    impl_->swapchain_recreation_requested = false;
    ++impl_->metrics.swapchain_recreations;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::service() noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }

    if (impl_->has_pending_event) {
        if (!impl_->events.try_push(impl_->pending_event)) {
            return sdl_failure(HostCode::queue_full);
        }
        impl_->has_pending_event = false;
    }

    const auto publish_event = [this](const HostEvent& host_event) noexcept {
        if (impl_->events.try_push(host_event)) {
            return true;
        }
        impl_->pending_event = host_event;
        impl_->has_pending_event = true;
        return false;
    };

    SDL_Event event{};
    std::size_t event_count = 0U;
    while (event_count < kMaxEventsPerService && SDL_PollEvent(&event)) {
        ++event_count;
        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            const HostEvent quit{
                HostEventKind::quit_requested,
                impl_->window_state,
                HostCode::ok};
            (void)publish_event(quit);
            impl_->shutting_down = true;
        } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                   event.type == SDL_EVENT_WINDOW_RESIZED) {
            int width = 0;
            int height = 0;
            if (SDL_GetWindowSizeInPixels(impl_->window, &width, &height) &&
                width > 0 && height > 0) {
                impl_->window_state.pixel_width = static_cast<std::uint32_t>(width);
                impl_->window_state.pixel_height = static_cast<std::uint32_t>(height);
                const HostEvent resized{
                    HostEventKind::window_resized,
                    impl_->window_state,
                    HostCode::ok};
                if (!publish_event(resized)) {
                    break;
                }
                impl_->swapchain_recreation_requested = true;
            }
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            ++impl_->audio_metrics.focus_lost_events;
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            ++impl_->audio_metrics.focus_gained_events;
        } else if (!event.adevice.recording &&
                   (event.type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED ||
                    event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) &&
                   event.adevice.which == SDL_GetAudioStreamDevice(impl_->audio)) {
            if (!reopen_audio_stream(*impl_)) {
                return sdl_failure(HostCode::device_lost);
            }
        } else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
            impl_->window_state.fullscreen = impl_->requested_fullscreen ==
                    FullscreenMode::windowed
                ? FullscreenMode::borderless
                : impl_->requested_fullscreen;
            const HostEvent changed{HostEventKind::fullscreen_changed,
                                    impl_->window_state, HostCode::ok};
            if (!publish_event(changed)) {
                break;
            }
        } else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
            impl_->window_state.fullscreen = FullscreenMode::windowed;
            const HostEvent changed{HostEventKind::fullscreen_changed,
                                    impl_->window_state, HostCode::ok};
            if (!publish_event(changed)) {
                break;
            }
        }
    }

    if (impl_->device_lost) {
        return sdl_failure(HostCode::device_lost);
    }
    if (impl_->has_pending_event) {
        return sdl_failure(HostCode::queue_full);
    }
    if (impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (impl_->swapchain_recreation_requested) {
        const HostStatus recreated = recreate_swapchain();
        if (!recreated.success()) {
            return recreated;
        }
    }

    if (!impl_->has_pending_frame &&
        !impl_->frames.try_pop(impl_->pending_frame)) {
        return sdl_failure(HostCode::not_ready);
    }
    impl_->has_pending_frame = true;
    if (impl_->pending_frame.sequence != impl_->next_sequence) {
        impl_->next_sequence = impl_->pending_frame.sequence + 1U;
        impl_->has_pending_frame = false;
        return sdl_failure(HostCode::sequence_error);
    }

    SDL_GPUCommandBuffer* command_buffer =
        SDL_AcquireGPUCommandBuffer(impl_->device);
    if (command_buffer == nullptr) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 target_width = 0U;
    Uint32 target_height = 0U;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            command_buffer,
            impl_->window,
            &swapchain_texture,
            &target_width,
            &target_height)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    if (swapchain_texture == nullptr) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        return sdl_failure(HostCode::not_ready);
    }

    Viewport viewport{};
    const ViewportRequest viewport_request{
        target_width,
        target_height,
        impl_->pending_frame.surface.width,
        impl_->pending_frame.surface.height,
        config_.aspect,
        config_.custom_aspect,
        config_.scale};
    const HostStatus viewport_status =
        calculate_viewport(viewport_request, viewport);
    if (!viewport_status.success()) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        return viewport_status;
    }

    const PresentationContext context{
        command_buffer,
        swapchain_texture,
        target_width,
        target_height,
        viewport};
    HostStatus encode_status{};
    if (encoder_ != nullptr) {
        encode_status = encoder_->encode(
            context, impl_->pending_frame.surface);
    } else if (!encode_default_frame(*impl_, command_buffer, swapchain_texture,
                                     impl_->pending_frame.surface, viewport)) {
        encode_status = sdl_failure(HostCode::internal_error);
    }
    if (!encode_status.success()) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        return encode_status;
    }
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }

    impl_->has_pending_frame = false;
    ++impl_->next_sequence;
    return HostStatus{};
}

bool SdlVulkanPresentation::poll_event(HostEvent& event) noexcept {
    if (impl_ == nullptr) {
        return false;
    }
    if (impl_->events.try_pop(event)) {
        return true;
    }
    if (!impl_->has_pending_event) {
        return false;
    }
    event = impl_->pending_event;
    impl_->has_pending_event = false;
    return true;
}

void SdlVulkanPresentation::set_frame_encoder(IFrameEncoder* encoder) noexcept {
    encoder_ = encoder;
}

HostStatus SdlVulkanPresentation::upload_replacement(
    const ReplacementImage& replacement, std::uint64_t& handle) noexcept {
    handle = 0U;
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down ||
        replacement.width == 0U || replacement.height == 0U) {
        return sdl_failure(HostCode::not_ready);
    }
    const std::uint64_t bytes64 = static_cast<std::uint64_t>(replacement.width) *
        replacement.height * 4U;
    if (bytes64 != replacement.rgba8.size() ||
        bytes64 > std::numeric_limits<Uint32>::max()) {
        return sdl_failure(HostCode::invalid_argument);
    }
    const SDL_GPUTextureCreateInfo texture_info{SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREUSAGE_SAMPLER,
        replacement.width, replacement.height, 1U, 1U,
        SDL_GPU_SAMPLECOUNT_1, 0U};
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
    const SDL_GPUTransferBufferCreateInfo transfer_info{
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(bytes64), 0U};
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
        impl_->device, &transfer_info);
    if (texture == nullptr || transfer == nullptr) {
        if (texture != nullptr) SDL_ReleaseGPUTexture(impl_->device, texture);
        if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        return sdl_failure();
    }
    void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTexture(impl_->device, texture);
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        return sdl_failure();
    }
    std::memcpy(mapped, replacement.rgba8.data(), replacement.rgba8.size());
    SDL_UnmapGPUTransferBuffer(impl_->device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(impl_->device);
    SDL_GPUCopyPass* copy = command == nullptr ? nullptr : SDL_BeginGPUCopyPass(command);
    if (copy == nullptr) {
        if (command != nullptr) SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTexture(impl_->device, texture);
        SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
        return sdl_failure();
    }
    const SDL_GPUTextureTransferInfo source{transfer, 0U, replacement.width,
                                             replacement.height};
    const SDL_GPUTextureRegion destination{texture, 0U, 0U, 0U, 0U, 0U,
        replacement.width, replacement.height, 1U};
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    const bool submitted = SDL_SubmitGPUCommandBuffer(command);
    SDL_ReleaseGPUTransferBuffer(impl_->device, transfer);
    if (!submitted) {
        SDL_ReleaseGPUTexture(impl_->device, texture);
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    try {
        handle = impl_->next_replacement_handle++;
        impl_->replacements.push_back(Impl::ReplacementTexture{texture, handle});
    } catch (...) {
        SDL_ReleaseGPUTexture(impl_->device, texture);
        handle = 0U;
        return sdl_failure();
    }
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::release_replacement(
    std::uint64_t handle) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::not_ready);
    }
    const auto found = std::find_if(impl_->replacements.begin(),
        impl_->replacements.end(), [handle](const Impl::ReplacementTexture& item) {
            return item.handle == handle;
        });
    if (handle == 0U || found == impl_->replacements.end()) {
        return sdl_failure(HostCode::invalid_argument);
    }
    if (!SDL_WaitForGPUIdle(impl_->device)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    SDL_ReleaseGPUTexture(impl_->device, found->texture);
    impl_->replacements.erase(found);
    return HostStatus{};
}

void SdlVulkanPresentation::shutdown() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->shutting_down && !impl_->initialized &&
        impl_->vulkan_loader == nullptr) {
        delete impl_;
        impl_ = nullptr;
        return;
    }
    impl_->shutting_down = true;
    if (impl_->device != nullptr) {
        (void)SDL_WaitForGPUIdle(impl_->device);
        for (const Impl::ReplacementTexture& replacement : impl_->replacements) {
            SDL_ReleaseGPUTexture(impl_->device, replacement.texture);
        }
        impl_->replacements.clear();
        if (impl_->frame_transfer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->frame_transfer);
            impl_->frame_transfer = nullptr;
        }
        if (impl_->frame_texture != nullptr) {
            SDL_ReleaseGPUTexture(impl_->device, impl_->frame_texture);
            impl_->frame_texture = nullptr;
        }
        if (impl_->present_sampler != nullptr) {
            SDL_ReleaseGPUSampler(impl_->device, impl_->present_sampler);
            impl_->present_sampler = nullptr;
        }
        if (impl_->present_pipeline != nullptr) {
            SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->present_pipeline);
            impl_->present_pipeline = nullptr;
        }
        if (impl_->window != nullptr && impl_->window_claimed) {
            SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
            impl_->window_claimed = false;
        }
        SDL_DestroyGPUDevice(impl_->device);
        impl_->device = nullptr;
    }
    if (impl_->audio != nullptr) {
        SDL_DestroyAudioStream(impl_->audio);
        impl_->audio = nullptr;
    }
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    if (impl_->sdl_vulkan_loaded) {
        SDL_Vulkan_UnloadLibrary();
        impl_->sdl_vulkan_loaded = false;
    }
    if (impl_->vulkan_loader != nullptr) {
        FreeLibrary(impl_->vulkan_loader);
        impl_->vulkan_loader = nullptr;
    }
    if (impl_->sdl_initialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);
        impl_->sdl_initialized = false;
    }
    delete impl_;
    impl_ = nullptr;
}

bool SdlVulkanPresentation::initialized() const noexcept {
    return impl_ != nullptr && impl_->initialized;
}

bool SdlVulkanPresentation::shutting_down() const noexcept {
    return impl_ == nullptr || impl_->shutting_down;
}

WindowState SdlVulkanPresentation::window_state() const noexcept {
    return impl_ == nullptr ? WindowState{} : impl_->window_state;
}

SdlAudioMetrics SdlVulkanPresentation::audio_metrics() const noexcept {
    if (impl_ == nullptr) return SdlAudioMetrics{};
    SdlAudioMetrics metrics = impl_->audio_metrics;
    if (impl_->audio != nullptr) {
        const int bytes = SDL_GetAudioStreamQueued(impl_->audio);
        if (bytes >= 0) {
            metrics.queued_frames = static_cast<std::uint32_t>(bytes) /
                (kAudioChannels * static_cast<std::uint32_t>(sizeof(std::int16_t)));
        }
    }
    return metrics;
}

} // namespace clean_aot::host
