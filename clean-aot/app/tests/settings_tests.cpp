#include "clean_aot/app/settings.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures=0;
void expect(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<'\n';++failures;}}
}
int main(){
    using namespace clean_aot::app;
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path{"Wipeout3SE-設定-test"};
    std::error_code error;std::filesystem::remove_all(root,error);
    const auto path=root/"構成.json";
    auto created=load_runtime_settings(path);
    expect(created.status==SettingsStatus::created&&std::filesystem::exists(path),"Unicode first-run creates defaults");
    auto loaded=load_runtime_settings(path);
    expect(loaded.status==SettingsStatus::loaded&&loaded.value.width==3840U&&loaded.value.audio_latency_frames==882U,"defaults reload and validate");
    std::string override_error;
    expect(apply_settings_override(loaded.value,"display.width=5120",override_error)&&loaded.value.width==5120U,"headless width override applies");
    expect(loaded.value.host_config(true).aspect==clean_aot::host::AspectPolicy::preserve_21x9,
           "ultrawide setting reaches the host viewport");
    expect(apply_settings_override(loaded.value,"input.deadzone_per_mille=200",override_error)&&loaded.value.deadzone_per_mille==200U,
           "controller deadzone override applies");
    expect(!apply_settings_override(loaded.value,"audio.max_queued_frames=441",override_error),
           "audio queue cannot be reduced below latency");
    expect(!apply_settings_override(loaded.value,"presentation.rate_hz=60",override_error),"timing override is unavailable");
    {std::ofstream malformed(path,std::ios::trunc);malformed<<"{\"schema\":\"wipeout3se.runtime-settings.v2\",\"display\":{}}";}
    expect(load_runtime_settings(path).status==SettingsStatus::invalid,"invalid config fails closed");
    auto reset=load_runtime_settings(path,true);
    expect(reset.status==SettingsStatus::reset&&load_runtime_settings(path).status==SettingsStatus::loaded,"safe reset atomically restores defaults");
    auto unicode=runtime_settings_json(RuntimeSettings{});
    const auto marker=unicode.find("\"default\"");unicode.replace(marker,9,"\"音声デバイス\"");
    {std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<unicode;}
    loaded=load_runtime_settings(path);
    expect(loaded.status==SettingsStatus::loaded&&loaded.value.audio_device=="音声デバイス","UTF-8 audio device survives");
    std::filesystem::remove_all(root,error);
    if(failures==0)std::cout<<"runtime settings tests: pass\n";
    return failures==0?0:1;
}
