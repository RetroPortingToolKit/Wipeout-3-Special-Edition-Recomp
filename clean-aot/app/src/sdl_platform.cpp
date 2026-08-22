#include "clean_aot/app/sdl_platform.hpp"

#include <new>
#include <SDL3/SDL.h>

namespace clean_aot::app {
struct SdlPlatform::Impl final { SDL_Window* window=nullptr; SDL_AudioStream* audio=nullptr; SDL_Gamepad* gamepad=nullptr; std::uint64_t sample=0U; PlatformConfiguration config{}; AudioHealth health{}; };
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
    int count=0;SDL_JoystickID* ids=SDL_GetGamepads(&count);if(ids!=nullptr&&count>0)impl_->gamepad=SDL_OpenGamepad(ids[0]);SDL_free(ids);
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
        if(event.type==SDL_EVENT_GAMEPAD_REMOVED&&impl_->gamepad!=nullptr&&SDL_GetGamepadID(impl_->gamepad)==event.gdevice.which){SDL_CloseGamepad(impl_->gamepad);impl_->gamepad=nullptr;}}
    state={};state.sample_index=impl_->sample++;
    if(impl_->gamepad!=nullptr){state.connected_mask=1U;
        for(int button=0;button<static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT)&&button<32;++button)if(SDL_GetGamepadButton(impl_->gamepad,static_cast<SDL_GamepadButton>(button)))state.buttons|=std::uint32_t{1U}<<static_cast<unsigned>(button);
        for(int axis=0;axis<static_cast<int>(SDL_GAMEPAD_AXIS_COUNT)&&axis<static_cast<int>(state.axes.size());++axis){const auto value=SDL_GetGamepadAxis(impl_->gamepad,static_cast<SDL_GamepadAxis>(axis));state.axes[static_cast<std::size_t>(axis)]=(value>-impl_->config.controller_deadzone&&value<impl_->config.controller_deadzone)?0:value;}
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
