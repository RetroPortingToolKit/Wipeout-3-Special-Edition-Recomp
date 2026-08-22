#include "clean_aot/app/application.hpp"

namespace clean_aot::app {

Application::Application(host::IHostBackend& host) noexcept : host_(host) {}
Application::~Application() { shutdown(); }

AppCode Application::initialize(Authorization authorization,
                                const runtime::GeneratedExecutableBinding& binding,
                                host::WindowState initial_window,
                                GraphicsConfiguration graphics) noexcept {
    if(initialized_)return AppCode::NotReady;
    if(!authorization.authorized())return AppCode::AuthorizationRejected;
    runtime::Configuration configuration{};
    configuration.bios=authorization.bios;
    configuration.initial_vblank_rate=machine::VBlankRate::Hz100;
    configuration.cpu_multiplier_numerator=1U;
    configuration.cpu_multiplier_denominator=1U;
    auto runtime=std::make_unique<runtime::Runtime>(configuration);
    if(!runtime->configured())return AppCode::RuntimeRejected;
    if(runtime->load_authenticated_executable(
           authorization.variant_identity_sha256,
           authorization.executable,
           authorization.selected_entry_pc,
           binding)!=runtime::DispatchStatus::Bound)
        return AppCode::DispatchRejected;
    auto runtime_graphics=std::make_unique<RuntimeGraphics>(graphics);
    if(!runtime_graphics->configured())return AppCode::RuntimeRejected;
    if(!host_.request_window(host::WindowRequest{initial_window}).success())return AppCode::HostRejected;
    authorization_=std::move(authorization);runtime_=std::move(runtime);
    graphics_=std::move(runtime_graphics);initialized_=true;return AppCode::Ok;
}

AppCode Application::run_update(Update& update) noexcept {
    if(!initialized_||runtime_==nullptr)return AppCode::NotReady;
    host::HostEvent event{};
    while(host_.poll_event(event))if(event.kind==host::HostEventKind::quit_requested||event.kind==host::HostEventKind::device_lost)return event.kind==host::HostEventKind::quit_requested?AppCode::Quit:AppCode::HostRejected;
    if(update.pc!=runtime_->selected_pc()||runtime_->dispatch_selected(update.branch_taken)!=runtime::DispatchStatus::Executed)return AppCode::DispatchRejected;
    update.pc=runtime_->selected_pc();
    if(!runtime_->advance_to_retirement_cycle(update.retirement_cycle).ok())return AppCode::RuntimeRejected;
    const host::HostStatus frame_status = graphics_->present(*runtime_,host_,update.frame.update_index);
    if((frame_status.code==host::HostCode::not_ready&&!host_.submit_frame(update.frame).success())||
       (frame_status.code!=host::HostCode::not_ready&&!frame_status.success())||
       !host_.submit_audio(update.audio).success()||!host_.submit_controller(update.controller).success())return AppCode::HostRejected;
    const host::HostStatus serviced=host_.service();
    return serviced.success()?AppCode::Ok:AppCode::HostRejected;
}

void Application::shutdown() noexcept { if(initialized_&&!host_.shutting_down())host_.shutdown();graphics_.reset();runtime_.reset();initialized_=false; }
bool Application::initialized() const noexcept { return initialized_; }
const Authorization& Application::authorization() const noexcept { return authorization_; }

} // namespace clean_aot::app
