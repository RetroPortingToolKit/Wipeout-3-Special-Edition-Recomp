#pragma once

#include <cstdint>
#include "clean_aot/host/host.hpp"
#include "clean_aot/host/sdl_vulkan.hpp"

namespace clean_aot::host {

struct DirectVulkanMetrics final {
    std::uint64_t generation = 0U;
    std::uint64_t submissions = 0U;
    std::uint64_t completions = 0U;
    std::uint64_t last_present_id = 0U;
    std::uint64_t last_completion_ns = 0U;
    std::uint64_t intervals_at_100hz = 0U;
    std::uint64_t cadence_violations = 0U;
    std::uint64_t recreations = 0U;
    PresentCompletionSource source = PresentCompletionSource::unavailable;
    bool surface_created = false;
    bool swapchain_created = false;
    bool hidden = false;
    bool device_lost = false;
};

// Optional direct path. SDL creates only the window and VkSurfaceKHR; all
// remaining Vulkan objects are owned by this bridge.
class DirectVulkanPresentBridge final : public IPresentCompletionProbe {
public:
    DirectVulkanPresentBridge() noexcept;
    ~DirectVulkanPresentBridge() override;
    DirectVulkanPresentBridge(const DirectVulkanPresentBridge&) = delete;
    DirectVulkanPresentBridge& operator=(const DirectVulkanPresentBridge&) = delete;

    [[nodiscard]] HostStatus initialize(void* sdl_window, std::uint32_t width,
                                        std::uint32_t height, bool hidden) noexcept;
    [[nodiscard]] HostStatus present_clear(std::uint64_t present_id) noexcept;
    [[nodiscard]] HostStatus resize(std::uint32_t width,
                                    std::uint32_t height, bool hidden) noexcept;
    void shutdown() noexcept;
    [[nodiscard]] DirectVulkanMetrics metrics() const noexcept;

    [[nodiscard]] PresentCompletionSource source() const noexcept override;
    void reset(std::uint64_t generation) noexcept override;
    void submitted(std::uint64_t, std::uint64_t) noexcept override;
    [[nodiscard]] bool poll(PresentCompletion& completion) noexcept override;

public:
    struct Impl;
private:
    Impl* impl_ = nullptr;
};

} // namespace clean_aot::host
