#include "clean_aot/app/sdl_platform.hpp"
#include "clean_aot/app/controller_input.hpp"

#include <array>
#include <new>
#include <SDL3/SDL.h>

namespace clean_aot::app {
host::SdlVulkanConfig make_live_graphics_config(
    const char* title, const GraphicsConfiguration& graphics,
    host::FullscreenMode fullscreen) noexcept {
    host::AspectPolicy aspect = host::AspectPolicy::preserve_4x3;
    switch (graphics.presentation) {
    case raster::PresentationMode::Identity4x3: aspect = host::AspectPolicy::preserve_4x3; break;
    case raster::PresentationMode::Generic16x9: aspect = host::AspectPolicy::preserve_16x9; break;
    case raster::PresentationMode::Generic21x9: aspect = host::AspectPolicy::preserve_21x9; break;
    case raster::PresentationMode::Generic32x9: aspect = host::AspectPolicy::preserve_32x9; break;
    case raster::PresentationMode::Custom: aspect = host::AspectPolicy::custom; break;
    }
    return host::SdlVulkanConfig{title, graphics.output_width,
        graphics.output_height, fullscreen, aspect,
        {graphics.custom_aspect_width, graphics.custom_aspect_height},
        host::ScaleMode::fractional, false, false,
        graphics.presentation_resolve == raster::PresentationResolveMode::EdgeAware
            ? host::PresentationFilter::Linear
            : host::PresentationFilter::Nearest};
}
struct SdlPlatform::Impl final { SDL_Window* window=nullptr; SDL_AudioStream* audio=nullptr; SDL_Gamepad* gamepad=nullptr; std::uint64_t sample=0U; PlatformConfiguration config{}; AudioHealth health{}; };

namespace {

void open_first_gamepad(SDL_Gamepad*& gamepad) noexcept {
    if (gamepad != nullptr) return;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr && count > 0) gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

std::uint32_t keyboard_buttons(const bool* keys) noexcept {
    if (keys == nullptr) return 0U;
    const auto pressed = [keys](SDL_Scancode key) { return keys[key]; };
    std::uint32_t result = 0U;
    const auto add = [&result](bool down, PsButton button) {
        if (down) result |= static_cast<std::uint16_t>(button);
    };
    add(pressed(SDL_SCANCODE_UP) || pressed(SDL_SCANCODE_W), PsButton::Up);
    add(pressed(SDL_SCANCODE_DOWN) || pressed(SDL_SCANCODE_S), PsButton::Down);
    add(pressed(SDL_SCANCODE_LEFT) || pressed(SDL_SCANCODE_A), PsButton::Left);
    add(pressed(SDL_SCANCODE_RIGHT) || pressed(SDL_SCANCODE_D), PsButton::Right);
    add(pressed(SDL_SCANCODE_X), PsButton::Cross);
    add(pressed(SDL_SCANCODE_C), PsButton::Circle);
    add(pressed(SDL_SCANCODE_Z), PsButton::Square);
    add(pressed(SDL_SCANCODE_V), PsButton::Triangle);
    add(pressed(SDL_SCANCODE_Q), PsButton::L1);
    add(pressed(SDL_SCANCODE_E), PsButton::R1);
    add(pressed(SDL_SCANCODE_1), PsButton::L2);
    add(pressed(SDL_SCANCODE_3), PsButton::R2);
    add(pressed(SDL_SCANCODE_RETURN), PsButton::Start);
    add(pressed(SDL_SCANCODE_BACKSPACE), PsButton::Select);
    return result;
}

} // namespace
SdlPlatform::~SdlPlatform(){shutdown();}

bool SdlPlatform::initialize(const char* title,std::uint32_t width,std::uint32_t height) noexcept {
    PlatformConfiguration configuration{}; configuration.width=width; configuration.height=height;
    return initialize(title,configuration);
}

bool SdlPlatform::initialize(const char* title,PlatformConfiguration configuration) noexcept {
    const std::uint32_t flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|
        (configuration.borderless?SDL_WINDOW_BORDERLESS:0U);
    if(configuration.audio_buffer_frames<441U||configuration.max_queued_audio_frames<configuration.audio_buffer_frames)return false;
    const std::uint32_t width=configuration.width;const std::uint32_t height=configuration.height;
    if(impl_!=nullptr||title==nullptr||width==0U||height==0U)return false;
    Impl* impl=new(std::nothrow)Impl{};if(impl==nullptr)return false;impl_=impl;impl_->config=configuration;
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD)){shutdown();return false;}
    impl_->window=SDL_CreateWindow(title,static_cast<int>(width),static_cast<int>(height),flags);
    SDL_AudioSpec spec{SDL_AUDIO_S16,2,static_cast<int>(host::kAudioSampleRateHz)};
    impl_->audio=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr);
    if(impl_->window==nullptr||impl_->audio==nullptr||!SDL_ResumeAudioStreamDevice(impl_->audio)){shutdown();return false;}
    open_first_gamepad(impl_->gamepad);
    return true;
}

bool SdlPlatform::pump(host::ControllerState& state,bool& quit) noexcept {
    if(impl_==nullptr) {
        return false;
    }
    quit=false;
    SDL_Event event{};
    std::size_t count=0U;
    while(count<64U&&SDL_PollEvent(&event)){++count;if(event.type==SDL_EVENT_QUIT||event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED)quit=true;
        if(event.type==SDL_EVENT_GAMEPAD_ADDED&&impl_->gamepad==nullptr)impl_->gamepad=SDL_OpenGamepad(event.gdevice.which);
        if(event.type==SDL_EVENT_GAMEPAD_REMOVED&&impl_->gamepad!=nullptr&&SDL_GetGamepadID(impl_->gamepad)==event.gdevice.which){SDL_CloseGamepad(impl_->gamepad);impl_->gamepad=nullptr;open_first_gamepad(impl_->gamepad);}}
    state={};state.sample_index=impl_->sample++;
    const bool focused=(SDL_GetWindowFlags(impl_->window)&SDL_WINDOW_INPUT_FOCUS)!=0U;
    if(!focused)return true;
    state.buttons=keyboard_buttons(SDL_GetKeyboardState(nullptr));
    if(state.buttons!=0U)state.connected_mask|=2U;
    if(impl_->gamepad!=nullptr){state.connected_mask|=1U;
        const std::array<bool,kMappedGamepadButtonCount> pressed{
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_SOUTH),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_EAST),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_WEST),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_NORTH),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_BACK),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_START),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_LEFT_STICK),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_RIGHT_STICK),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_DPAD_UP),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_DPAD_DOWN),
            SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_DPAD_LEFT),SDL_GetGamepadButton(impl_->gamepad,SDL_GAMEPAD_BUTTON_DPAD_RIGHT)};
        state.buttons|=map_gamepad_buttons(pressed);
        const auto left=apply_radial_deadzone(SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_LEFTX),SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_LEFTY),impl_->config.controller_deadzone);
        const auto right=apply_radial_deadzone(SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_RIGHTX),SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_RIGHTY),impl_->config.controller_deadzone);
        state.axes[0]=left.first;state.axes[1]=left.second;state.axes[2]=right.first;state.axes[3]=right.second;
        state.axes[4]=SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_LEFT_TRIGGER);state.axes[5]=SDL_GetGamepadAxis(impl_->gamepad,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        if(state.axes[4]>16384)state.buttons|=static_cast<std::uint16_t>(PsButton::L2);
        if(state.axes[5]>16384)state.buttons|=static_cast<std::uint16_t>(PsButton::R2);
    }
    return true;
}

bool SdlPlatform::queue_audio(const host::StereoAudioBatch& audio) noexcept {
    if(impl_==nullptr||impl_->audio==nullptr||audio.interleaved_stereo==nullptr||audio.sample_frames!=host::kAudioFramesPerUpdate)return false;
    const int queued=SDL_GetAudioStreamQueued(impl_->audio);if(queued<0)return false;
    const auto queued_frames=static_cast<std::uint32_t>(queued)/
        static_cast<std::uint32_t>(host::kAudioChannels*sizeof(std::int16_t));impl_->health.queued_frames=queued_frames;
    if(queued_frames==0U&&impl_->sample>1U)++impl_->health.underruns;
    if(queued_frames+audio.sample_frames>impl_->config.max_queued_audio_frames){++impl_->health.overflows;return true;}
    const int bytes=static_cast<int>(audio.sample_frames*host::kAudioChannels*sizeof(std::int16_t));return SDL_PutAudioStreamData(impl_->audio,audio.interleaved_stereo,bytes);
}

AudioHealth SdlPlatform::audio_health() const noexcept{return impl_==nullptr?AudioHealth{}:impl_->health;}

void SdlPlatform::shutdown() noexcept {if(impl_==nullptr)return;if(impl_->gamepad!=nullptr)SDL_CloseGamepad(impl_->gamepad);if(impl_->audio!=nullptr)SDL_DestroyAudioStream(impl_->audio);if(impl_->window!=nullptr)SDL_DestroyWindow(impl_->window);SDL_QuitSubSystem(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD);delete impl_;impl_=nullptr;}
} // namespace clean_aot::app
