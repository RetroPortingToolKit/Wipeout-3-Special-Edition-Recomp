#pragma once

#include "clean_aot/app/authorization.hpp"
#include "clean_aot/host/host.hpp"
#include "clean_aot/runtime/runtime.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace clean_aot::app {

enum class AppCode : std::uint8_t {
    Ok,
    NotReady,
    AuthorizationRejected,
    RuntimeRejected,
    DispatchRejected,
    HostRejected,
    Quit,
};

struct Update final {
    std::uint32_t pc = 0U;
    bool branch_taken = false;
    host::FrameSubmission frame{};
    host::StereoAudioBatch audio{};
    host::ControllerState controller{};
    std::uint64_t retirement_cycle = 0U;
};

class Application final {
public:
    explicit Application(host::IHostBackend& host) noexcept;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] AppCode initialize(
        Authorization authorization,
        const runtime::GeneratedExecutableBinding& binding,
        host::WindowState initial_window) noexcept;
    [[nodiscard]] AppCode run_update(Update& update) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] const Authorization& authorization() const noexcept;

private:
    host::IHostBackend& host_;
    Authorization authorization_{};
    std::unique_ptr<runtime::Runtime> runtime_{};
    bool initialized_ = false;
};

} // namespace clean_aot::app
