#include "clean_aot/app/settings.hpp"
#include "clean_aot/app/graphics.hpp"

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
    expect(loaded.value.pgxp_verified_near_plane &&
           apply_settings_override(loaded.value,"graphics.pgxp_verified_near_plane=off",override_error) &&
           !loaded.value.pgxp_verified_near_plane,
           "validated PGXP near-plane policy can be disabled explicitly");
    GraphicsConfiguration graphics{};
    loaded.value.apply_graphics_settings(graphics);
    expect(graphics.pgxp_enabled &&
           graphics.pgxp_near_plane == PgxpNearPlanePolicy::NativeFallback,
           "validated disabled near-plane setting reaches live graphics policy");
    RuntimeSettings remaster{};
    remaster.width = 5120U;
    remaster.height = 2160U;
    remaster.apply_graphics_settings(graphics);
    clean_aot::host::SdlPresentationMetrics timing{};
    timing.completion_source = clean_aot::host::PresentCompletionSource::present_wait;
    timing.display_presentations_confirmed = 100U;
    timing.completion_intervals_at_target = 99U;
    const auto matrix = remaster.graphics_feature_receipt(graphics, &timing);
    std::cout << matrix.canonical_json() << '\n';
    expect(matrix.pgxp_vertex_projection_perspective &&
           matrix.widescreen_projection_culling && matrix.true_color &&
           matrix.scaled_dither_resolve && matrix.transparency_safe_filtering &&
           matrix.scanout_24bit_chroma && matrix.hd_textures && matrix.hd_fonts &&
           matrix.complete_gp0_fallback && matrix.exact_native_fallback &&
           matrix.vulkan_4k_ultrawide && matrix.physical_present_timing_verified,
           "remaster settings configure every live GooseStation-class feature");
    expect(matrix.canonical_json().find(
               "\"schema\":\"clean-aot.graphics-feature-matrix.v1\"") !=
               std::string::npos &&
           matrix.canonical_json().find("\"physical_present_timing\":\"verified\"") !=
               std::string::npos,
           "feature matrix emits deterministic verified timing receipt");
    timing.completion_source = clean_aot::host::PresentCompletionSource::unavailable;
    const auto unavailable = remaster.graphics_feature_receipt(graphics, &timing);
    expect(!unavailable.physical_present_timing_verified &&
           !unavailable.physical_present_timing_available &&
           unavailable.canonical_json().find(
               "\"physical_present_timing\":\"unavailable\"") != std::string::npos,
           "feature receipt never claims physical completion when unavailable");

    RuntimeSettings native{};
    native.pgxp = false; native.true_color = false; native.hd_assets = false;
    native.ultrawide = false;
    native.apply_graphics_settings(graphics);
    expect(graphics.presentation == clean_aot::raster::PresentationMode::Identity4x3 &&
           graphics.projection == psx::render::ProjectionPolicy::PreserveNative4x3 &&
           graphics.culling == psx::render::CullingPolicy::NativeHardware &&
           graphics.texture_filter == clean_aot::raster::TextureFilterMode::NearestNative &&
           graphics.precise_color == clean_aot::raster::PreciseColorMode::NativeRgb555 &&
           graphics.presentation_resolve == clean_aot::raster::PresentationResolveMode::Native &&
           graphics.display_postprocess == clean_aot::raster::DisplayPostprocessMode::None,
           "disabled enhancements restore exact native graphics policy");
    auto legacy_json = runtime_settings_json(RuntimeSettings{});
    const auto near_field = legacy_json.find(",\"pgxp_verified_near_plane\":true");
    legacy_json.erase(near_field, std::string_view{",\"pgxp_verified_near_plane\":true"}.size());
    {std::ofstream legacy(path,std::ios::trunc);legacy<<legacy_json;}
    expect(load_runtime_settings(path).status == SettingsStatus::loaded,
           "v2 settings without the additive near-plane field retain safe defaults");
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
