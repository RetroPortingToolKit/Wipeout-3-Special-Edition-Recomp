#include "clean_aot/host/direct_vulkan_bridge.hpp"

#if !defined(_WIN32)
#error "Direct Vulkan bridge is Windows-only"
#endif

#include <new>
#include <vector>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <VkBootstrap.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace clean_aot::host {

struct DirectVulkanPresentBridge::Impl final {
    SDL_Window* window = nullptr;
    vkb::Instance instance{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device{};
    vkb::DispatchTable dispatch{};
    vkb::Swapchain swapchain{};
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<VkImage> images{};
    PFN_vkWaitForPresentKHR wait_for_present = nullptr;
    PFN_vkGetPastPresentationTimingGOOGLE get_past_timing = nullptr;
    DirectVulkanMetrics metrics{};
    PresentCompletion pending{};
    bool has_pending = false;
    bool initialized = false;
    std::uint64_t next_present_id = 1U;
    std::uint64_t generation_completions = 0U;
};

namespace {

[[nodiscard]] HostStatus failure(HostCode code = HostCode::unsupported) noexcept {
    return HostStatus{code};
}

void record_completion(DirectVulkanPresentBridge::Impl& impl,
                       std::uint64_t id, std::uint64_t timestamp) noexcept {
    if (timestamp == 0U || (impl.generation_completions != 0U &&
        timestamp <= impl.metrics.last_completion_ns)) {
        impl.metrics.source=PresentCompletionSource::fault; return;
    }
    if (impl.generation_completions != 0U) {
        const std::uint64_t interval=timestamp-impl.metrics.last_completion_ns;
        const std::uint64_t error=interval>10'000'000U
            ? interval-10'000'000U : 10'000'000U-interval;
        if (error<=1'000'000U) ++impl.metrics.intervals_at_100hz;
        else ++impl.metrics.cadence_violations;
    }
    impl.pending={impl.metrics.generation,id,timestamp}; impl.has_pending=true;
    ++impl.metrics.completions; impl.metrics.last_present_id=id;
    ++impl.generation_completions;
    impl.metrics.last_completion_ns=timestamp;
}

void destroy_swapchain(DirectVulkanPresentBridge::Impl& impl) noexcept {
    if (impl.device.device != VK_NULL_HANDLE) (void)impl.dispatch.deviceWaitIdle();
    if (impl.fence != VK_NULL_HANDLE) impl.dispatch.destroyFence(impl.fence, nullptr);
    if (impl.acquired != VK_NULL_HANDLE) impl.dispatch.destroySemaphore(impl.acquired, nullptr);
    if (impl.rendered != VK_NULL_HANDLE) impl.dispatch.destroySemaphore(impl.rendered, nullptr);
    if (impl.command_pool != VK_NULL_HANDLE) impl.dispatch.destroyCommandPool(impl.command_pool, nullptr);
    impl.fence = VK_NULL_HANDLE; impl.acquired = VK_NULL_HANDLE;
    impl.rendered = VK_NULL_HANDLE; impl.command_pool = VK_NULL_HANDLE;
    impl.command = VK_NULL_HANDLE; impl.images.clear();
    if (impl.swapchain.swapchain != VK_NULL_HANDLE) vkb::destroy_swapchain(impl.swapchain);
    impl.swapchain = {}; impl.metrics.swapchain_created = false;
}

[[nodiscard]] bool create_swapchain(DirectVulkanPresentBridge::Impl& impl,
                                    std::uint32_t width, std::uint32_t height) noexcept {
    auto built = vkb::SwapchainBuilder{impl.device}
        .set_desired_extent(width, height)
        .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build();
    if (!built) return false;
    impl.swapchain = built.value();
    auto images = impl.swapchain.get_images();
    if (!images) return false;
    impl.images = images.value();
    auto graphics_index = impl.device.get_queue_index(vkb::QueueType::graphics);
    auto graphics = impl.device.get_queue(vkb::QueueType::graphics);
    auto present = impl.device.get_queue(vkb::QueueType::present);
    if (!graphics_index || !graphics || !present) return false;
    impl.graphics_queue = graphics.value(); impl.present_queue = present.value();
    VkCommandPoolCreateInfo pool{}; pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphics_index.value();
    if (impl.dispatch.createCommandPool(&pool, nullptr, &impl.command_pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo alloc{}; alloc.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = impl.command_pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1U;
    if (impl.dispatch.allocateCommandBuffers(&alloc, &impl.command) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo semaphore{}; semaphore.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence{}; fence.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (impl.dispatch.createSemaphore(&semaphore, nullptr, &impl.acquired) != VK_SUCCESS ||
        impl.dispatch.createSemaphore(&semaphore, nullptr, &impl.rendered) != VK_SUCCESS ||
        impl.dispatch.createFence(&fence, nullptr, &impl.fence) != VK_SUCCESS) return false;
    impl.metrics.swapchain_created = true;
    return true;
}

} // namespace

DirectVulkanPresentBridge::DirectVulkanPresentBridge() noexcept = default;
DirectVulkanPresentBridge::~DirectVulkanPresentBridge() { shutdown(); }

HostStatus DirectVulkanPresentBridge::initialize(void* window, std::uint32_t width,
                                                  std::uint32_t height, bool hidden) noexcept {
    if (impl_ != nullptr || window == nullptr || width == 0U || height == 0U) return failure(HostCode::invalid_argument);
    impl_ = new (std::nothrow) Impl{}; if (impl_ == nullptr) return failure(HostCode::internal_error);
    impl_->window = static_cast<SDL_Window*>(window); impl_->metrics.hidden = hidden;
    Uint32 extension_count = 0U;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    if (extensions == nullptr) { shutdown(); return failure(); }
    vkb::InstanceBuilder builder{};
    for (Uint32 i = 0U; i < extension_count; ++i) builder.enable_extension(extensions[i]);
    auto instance = builder.set_app_name("clean-aot direct present bridge").require_api_version(1, 1, 0).build();
    if (!instance) { shutdown(); return failure(); }
    impl_->instance = instance.value();
    if (!SDL_Vulkan_CreateSurface(impl_->window, impl_->instance.instance, nullptr, &impl_->surface)) {
        shutdown(); return failure();
    }
    impl_->metrics.surface_created = true;
    vkb::PhysicalDeviceSelector selector{impl_->instance};
    selector.set_surface(impl_->surface);
    auto physical = selector.select();
    if (!physical) { shutdown(); return failure(); }
    const bool wait_supported = physical->is_extension_present(VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
        physical->is_extension_present(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    const bool timing_supported = physical->is_extension_present(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    if (wait_supported) {
        physical->enable_extension_if_present(VK_KHR_PRESENT_ID_EXTENSION_NAME);
        physical->enable_extension_if_present(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
    }
    if (timing_supported) physical->enable_extension_if_present(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
    VkPhysicalDevicePresentIdFeaturesKHR id_features{}; id_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
    VkPhysicalDevicePresentWaitFeaturesKHR wait_features{}; wait_features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
    id_features.presentId = wait_supported; wait_features.presentWait = wait_supported;
    vkb::DeviceBuilder device_builder{physical.value()};
    if (wait_supported) { device_builder.add_pNext(&id_features); device_builder.add_pNext(&wait_features); }
    auto device = device_builder.build();
    if (!device) { shutdown(); return failure(); }
    impl_->device = device.value(); impl_->dispatch = impl_->device.make_table();
    impl_->wait_for_present = wait_supported ? reinterpret_cast<PFN_vkWaitForPresentKHR>(
        impl_->device.fp_vkGetDeviceProcAddr(impl_->device.device, "vkWaitForPresentKHR")) : nullptr;
    impl_->get_past_timing = timing_supported ? reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
        impl_->device.fp_vkGetDeviceProcAddr(impl_->device.device, "vkGetPastPresentationTimingGOOGLE")) : nullptr;
    impl_->metrics.source = impl_->wait_for_present != nullptr ? PresentCompletionSource::present_wait
        : impl_->get_past_timing != nullptr ? PresentCompletionSource::display_timing
        : PresentCompletionSource::unavailable;
    impl_->metrics.generation = 1U; impl_->initialized = true;
    if (!hidden && !create_swapchain(*impl_, width, height)) { shutdown(); return failure(); }
    return HostStatus{};
}

HostStatus DirectVulkanPresentBridge::present_clear(std::uint64_t present_id) noexcept {
    if (impl_ == nullptr || !impl_->initialized || !impl_->metrics.swapchain_created) return failure(HostCode::not_ready);
    if (present_id != impl_->next_present_id) return failure(HostCode::sequence_error);
    const auto device_lost = [this]() noexcept {
        impl_->metrics.device_lost=true; return failure(HostCode::device_lost);
    };
    if (impl_->dispatch.waitForFences(1U, &impl_->fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS ||
        impl_->dispatch.resetFences(1U, &impl_->fence) != VK_SUCCESS) return device_lost();
    std::uint32_t image_index = 0U;
    VkResult result = impl_->dispatch.acquireNextImageKHR(impl_->swapchain.swapchain, UINT64_MAX,
        impl_->acquired, VK_NULL_HANDLE, &image_index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return device_lost();
    (void)impl_->dispatch.resetCommandPool(impl_->command_pool, 0U);
    VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    (void)impl_->dispatch.beginCommandBuffer(impl_->command, &begin);
    VkImageMemoryBarrier barrier{}; barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0U; barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = impl_->images[image_index]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0U,1U,0U,1U};
    impl_->dispatch.cmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &barrier);
    const float phase = static_cast<float>(present_id % 256U) / 255.0F;
    const VkClearColorValue color{{phase, 0.0F, 1.0F - phase, 1.0F}};
    impl_->dispatch.cmdClearColorImage(impl_->command, barrier.image, barrier.newLayout,
        &color, 1U, &barrier.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0U;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    impl_->dispatch.cmdPipelineBarrier(impl_->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &barrier);
    if (impl_->dispatch.endCommandBuffer(impl_->command) != VK_SUCCESS) return device_lost();
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.waitSemaphoreCount=1U;
    submit.pWaitSemaphores=&impl_->acquired; submit.pWaitDstStageMask=&wait_stage;
    submit.commandBufferCount=1U; submit.pCommandBuffers=&impl_->command;
    submit.signalSemaphoreCount=1U; submit.pSignalSemaphores=&impl_->rendered;
    if (impl_->dispatch.queueSubmit(impl_->graphics_queue,1U,&submit,impl_->fence)!=VK_SUCCESS) return device_lost();
    VkPresentIdKHR id{}; id.sType=VK_STRUCTURE_TYPE_PRESENT_ID_KHR; id.swapchainCount=1U; id.pPresentIds=&present_id;
    VkPresentTimeGOOGLE desired_time{};
    desired_time.presentID=static_cast<std::uint32_t>(present_id);
    VkPresentTimesInfoGOOGLE times{}; times.sType=VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
    times.swapchainCount=1U; times.pTimes=&desired_time;
    VkPresentInfoKHR present{}; present.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = impl_->wait_for_present != nullptr ? static_cast<const void*>(&id)
        : impl_->get_past_timing != nullptr && present_id <= UINT32_MAX
            ? static_cast<const void*>(&times) : nullptr;
    present.waitSemaphoreCount=1U; present.pWaitSemaphores=&impl_->rendered;
    present.swapchainCount=1U; present.pSwapchains=&impl_->swapchain.swapchain; present.pImageIndices=&image_index;
    result=impl_->dispatch.queuePresentKHR(impl_->present_queue,&present);
    if (result!=VK_SUCCESS && result!=VK_SUBOPTIMAL_KHR) return device_lost();
    ++impl_->metrics.submissions;
    ++impl_->next_present_id;
    if (impl_->wait_for_present != nullptr && impl_->wait_for_present(impl_->device.device,
            impl_->swapchain.swapchain,present_id,100'000'000U)==VK_SUCCESS) {
        record_completion(*impl_,present_id,SDL_GetTicksNS());
    } else if (impl_->get_past_timing != nullptr && present_id <= UINT32_MAX) {
        (void)impl_->dispatch.queueWaitIdle(impl_->present_queue);
        std::uint32_t count=0U;
        if (impl_->get_past_timing(impl_->device.device,impl_->swapchain.swapchain,
                &count,nullptr)==VK_SUCCESS && count!=0U) {
            std::vector<VkPastPresentationTimingGOOGLE> timings(count);
            if (impl_->get_past_timing(impl_->device.device,impl_->swapchain.swapchain,
                    &count,timings.data())==VK_SUCCESS) {
                for (const auto& timing : timings) {
                    if (timing.presentID==static_cast<std::uint32_t>(present_id) &&
                        timing.actualPresentTime!=0U) {
                        record_completion(*impl_,present_id,timing.actualPresentTime); break;
                    }
                }
            }
        }
    }
    return HostStatus{};
}

HostStatus DirectVulkanPresentBridge::resize(std::uint32_t width, std::uint32_t height, bool hidden) noexcept {
    if (impl_==nullptr || !impl_->initialized || width==0U || height==0U) return failure(HostCode::invalid_argument);
    destroy_swapchain(*impl_); ++impl_->metrics.generation; ++impl_->metrics.recreations;
    impl_->metrics.hidden=hidden; impl_->has_pending=false;
    impl_->next_present_id=1U;
    impl_->generation_completions=0U; impl_->metrics.last_completion_ns=0U;
    if (!hidden && !create_swapchain(*impl_,width,height)) {
        impl_->metrics.device_lost=true; return failure(HostCode::device_lost);
    }
    return HostStatus{};
}

void DirectVulkanPresentBridge::shutdown() noexcept {
    if (impl_==nullptr) return;
    destroy_swapchain(*impl_);
    if (impl_->device.device!=VK_NULL_HANDLE) vkb::destroy_device(impl_->device);
    if (impl_->surface!=VK_NULL_HANDLE) SDL_Vulkan_DestroySurface(impl_->instance.instance,impl_->surface,nullptr);
    if (impl_->instance.instance!=VK_NULL_HANDLE) vkb::destroy_instance(impl_->instance);
    delete impl_; impl_=nullptr;
}
DirectVulkanMetrics DirectVulkanPresentBridge::metrics() const noexcept { return impl_?impl_->metrics:DirectVulkanMetrics{}; }
PresentCompletionSource DirectVulkanPresentBridge::source() const noexcept { return impl_?impl_->metrics.source:PresentCompletionSource::unavailable; }
void DirectVulkanPresentBridge::reset(std::uint64_t) noexcept {}
void DirectVulkanPresentBridge::submitted(std::uint64_t,std::uint64_t) noexcept {}
bool DirectVulkanPresentBridge::poll(PresentCompletion& completion) noexcept {
    if (impl_==nullptr || !impl_->has_pending) return false;
    completion=impl_->pending; impl_->has_pending=false; return true;
}

} // namespace clean_aot::host
