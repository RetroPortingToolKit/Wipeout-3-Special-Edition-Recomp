#include "clean_aot/host/sdl_vulkan.hpp"

#if !defined(_WIN32)
#error "The SDL3/Vulkan presentation backend is Windows-only"
#endif

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

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
        state.pixel_height == 0U) {
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
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    HMODULE vulkan_loader = nullptr;
    FixedSpscQueue<FrameSubmission, kFrameQueueDepth> frames{};
    FixedSpscQueue<HostEvent, kEventQueueDepth> events{};
    FrameSubmission pending_frame{};
    WindowState window_state{};
    bool has_pending_frame = false;
    bool sdl_initialized = false;
    bool initialized = false;
    bool shutting_down = false;
    bool device_lost = false;
    bool swapchain_recreation_requested = false;
    bool event_overflow = false;
    std::uint64_t next_sequence = 0U;
};

SdlVulkanPresentation::SdlVulkanPresentation(
    const SdlVulkanConfig& config) noexcept
    : config_(config) {}

SdlVulkanPresentation::~SdlVulkanPresentation() {
    shutdown();
}

HostStatus SdlVulkanPresentation::initialize() noexcept {
    if (impl_ != nullptr || config_.width == 0U || config_.height == 0U) {
        return sdl_failure(HostCode::invalid_argument);
    }

    Impl* impl = new (std::nothrow) Impl{};
    if (impl == nullptr) {
        return sdl_failure();
    }
    impl_ = impl;
    impl_->window_state = WindowState{
        config_.width, config_.height, FullscreenMode::windowed};

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        shutdown();
        return sdl_failure();
    }
    impl_->sdl_initialized = true;

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

    const SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
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
    if (impl_->device == nullptr ||
        !SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window) ||
        !SDL_SetGPUAllowedFramesInFlight(impl_->device, 2U)) {
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
    SDL_GPUShader* fragment_shader =
        SDL_CreateGPUShader(impl_->device, &fragment_info);
    if (vertex_shader != nullptr) {
        SDL_ReleaseGPUShader(impl_->device, vertex_shader);
    }
    if (fragment_shader != nullptr) {
        SDL_ReleaseGPUShader(impl_->device, fragment_shader);
    }
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
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
    if (!impl_->frames.try_push(frame)) {
        return sdl_failure(HostCode::queue_full);
    }
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::request_window(
    const WindowRequest& request) noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }
    if (request.state.pixel_width == 0U || request.state.pixel_height == 0U) {
        return sdl_failure(HostCode::unsupported);
    }
    if (!apply_window_state(impl_->window, request.state)) {
        return sdl_failure();
    }
    impl_->window_state = request.state;
    return HostStatus{};
}

void SdlVulkanPresentation::mark_device_lost() noexcept {
    if (impl_ == nullptr || impl_->shutting_down || impl_->device_lost) {
        return;
    }
    impl_->device_lost = true;
    const HostEvent event{
        HostEventKind::device_lost,
        impl_->window_state,
        HostCode::device_lost};
    if (!impl_->events.try_push(event)) {
        impl_->event_overflow = true;
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
    if (!SDL_ClaimWindowForGPUDevice(impl_->device, impl_->window)) {
        mark_device_lost();
        return sdl_failure(HostCode::device_lost);
    }
    impl_->swapchain_recreation_requested = false;
    impl_->device_lost = false;
    return HostStatus{};
}

HostStatus SdlVulkanPresentation::service() noexcept {
    if (impl_ == nullptr || !impl_->initialized || impl_->shutting_down) {
        return sdl_failure(HostCode::shutting_down);
    }

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
            if (!impl_->events.try_push(quit)) {
                return sdl_failure(HostCode::queue_full);
            }
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
                if (!impl_->events.try_push(resized)) {
                    return sdl_failure(HostCode::queue_full);
                }
                impl_->swapchain_recreation_requested = true;
            }
        } else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
            impl_->window_state.fullscreen = FullscreenMode::borderless;
        } else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
            impl_->window_state.fullscreen = FullscreenMode::windowed;
        }
    }

    if (impl_->device_lost) {
        return sdl_failure(HostCode::device_lost);
    }
    if (impl_->event_overflow) {
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
        {},
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
    } else {
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain_texture;
        target.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass =
            SDL_BeginGPURenderPass(command_buffer, &target, 1U, nullptr);
        if (pass == nullptr) {
            SDL_CancelGPUCommandBuffer(command_buffer);
            mark_device_lost();
            return sdl_failure(HostCode::device_lost);
        }
        SDL_EndGPURenderPass(pass);
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
    if (impl_ == nullptr || !impl_->events.try_pop(event)) {
        return false;
    }
    impl_->event_overflow = false;
    return true;
}

void SdlVulkanPresentation::set_frame_encoder(IFrameEncoder* encoder) noexcept {
    encoder_ = encoder;
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
        if (impl_->window != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(impl_->device, impl_->window);
        }
        SDL_DestroyGPUDevice(impl_->device);
        impl_->device = nullptr;
    }
    if (impl_->window != nullptr) {
        SDL_DestroyWindow(impl_->window);
        impl_->window = nullptr;
    }
    if (impl_->vulkan_loader != nullptr) {
        SDL_Vulkan_UnloadLibrary();
        FreeLibrary(impl_->vulkan_loader);
        impl_->vulkan_loader = nullptr;
    }
    if (impl_->sdl_initialized) {
        SDL_Quit();
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

} // namespace clean_aot::host
