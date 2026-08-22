#include "clean_aot/app/settings.hpp"
#include "json.hpp"

#include <fstream>
#include <limits>

namespace clean_aot::app {
namespace {
using detail::Json;
constexpr std::string_view kSchema = "wipeout3se.runtime-settings.v2";

const Json* nested(const Json& root, std::string_view group, std::string_view key) noexcept {
    const auto* object = detail::member(root, group);
    return object == nullptr ? nullptr : detail::member(*object, key);
}
bool text(const Json& root, std::string_view group, std::string_view key, std::string& out) {
    const auto* value = nested(root, group, key); const auto* string = value == nullptr ? nullptr : value->string();
    if (string == nullptr) return false;
    out = *string;
    return true;
}
bool number(const Json& root, std::string_view group, std::string_view key, std::uint64_t& out) noexcept {
    const auto* value = nested(root, group, key); const auto* integer = value == nullptr ? nullptr : value->number();
    if (integer == nullptr) return false;
    out = *integer;
    return true;
}
bool boolean(const Json& root, std::string_view group, std::string_view key, bool& out) noexcept {
    const auto* value = nested(root, group, key); const auto* flag = value == nullptr ? nullptr : value->boolean();
    if (flag == nullptr) return false;
    out = *flag;
    return true;
}
bool write_atomic(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path; temporary += ".new";
    std::error_code error; std::filesystem::remove(temporary, error);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    output.flush(); output.close();
    std::filesystem::rename(temporary, path, error);
    if (!error) return true;
    std::filesystem::remove(path, error); error.clear(); std::filesystem::rename(temporary, path, error);
    return !error;
}
bool parse(std::string_view input, RuntimeSettings& value, std::string& error) {
    Json root; if (!detail::parse_json(input, root, error)) return false;
    const auto* schema = detail::member(root, "schema");
    if (schema == nullptr || schema->string() == nullptr || *schema->string() != kSchema) { error="settings schema mismatch"; return false; }
    std::uint64_t width=0,height=0,rate=0,latency=0,max_frames=0,deadzone=0,cards=0;
    std::string mode,policy,filter,device,aspect;
    if (!number(root,"display","width",width)||!number(root,"display","height",height)||
        !text(root,"display","mode",mode)||!text(root,"display","aspect",aspect)||
        !text(root,"display","filter",filter)||!number(root,"presentation","rate_hz",rate)||
        !text(root,"presentation","policy",policy)||!number(root,"audio","latency_frames",latency)||
        !number(root,"audio","max_queued_frames",max_frames)||!text(root,"audio","device",device)||
        !number(root,"input","deadzone_per_mille",deadzone)||!boolean(root,"input","rumble",value.rumble)||
        !boolean(root,"graphics","pgxp",value.pgxp)||!boolean(root,"graphics","true_color",value.true_color)||
        !boolean(root,"graphics","hd_assets",value.hd_assets)||!boolean(root,"saves","atomic",value.atomic_saves)||
        !number(root,"saves","card_slots",cards)) { error="settings field missing or wrong type"; return false; }
    if (width<640||width>7680||height<480||height>4320||rate!=100||latency<441||latency>8192||
        max_frames<latency||max_frames>16384||deadzone>500||cards<1||cards>2||device.empty()||device.size()>256) {
        error="settings value outside safe bounds"; return false;
    }
    if(mode=="windowed") value.fullscreen=host::FullscreenMode::windowed;
    else if(mode=="borderless") value.fullscreen=host::FullscreenMode::borderless;
    else if(mode=="exclusive") value.fullscreen=host::FullscreenMode::exclusive;
    else { error="invalid display mode"; return false; }
    if(policy=="low-latency-100hz") value.present_policy=host::SdlPresentPolicy::low_latency_100hz;
    else if(policy=="vsync") value.present_policy=host::SdlPresentPolicy::vsync;
    else if(policy=="mailbox") value.present_policy=host::SdlPresentPolicy::mailbox;
    else { error="invalid present policy"; return false; }
    if(filter=="nearest") value.presentation_filter=host::PresentationFilter::Nearest;
    else if(filter=="linear") value.presentation_filter=host::PresentationFilter::Linear;
    else { error="invalid presentation filter"; return false; }
    if(aspect!="native-4:3"&&aspect!="auto-ultrawide") { error="invalid aspect policy"; return false; }
    value.width=static_cast<std::uint32_t>(width); value.height=static_cast<std::uint32_t>(height);
    value.audio_latency_frames=static_cast<std::uint32_t>(latency); value.audio_max_frames=static_cast<std::uint32_t>(max_frames);
    value.deadzone_per_mille=static_cast<std::uint16_t>(deadzone); value.card_slots=static_cast<std::uint8_t>(cards);
    value.audio_device=std::move(device); value.ultrawide=aspect=="auto-ultrawide"; return true;
}
const char* mode(host::FullscreenMode value) { return value==host::FullscreenMode::windowed?"windowed":value==host::FullscreenMode::exclusive?"exclusive":"borderless"; }
const char* policy(host::SdlPresentPolicy value) { return value==host::SdlPresentPolicy::vsync?"vsync":value==host::SdlPresentPolicy::mailbox?"mailbox":"low-latency-100hz"; }
}

host::SdlVulkanConfig RuntimeSettings::host_config(bool hidden) const noexcept {
    host::SdlVulkanConfig result{}; result.width=width; result.height=height; result.fullscreen=fullscreen;
    result.hidden=hidden; result.presentation_filter=presentation_filter; result.present_policy=present_policy;
    if (ultrawide) {
        const auto scaled_width = static_cast<std::uint64_t>(width) * 9U;
        result.aspect = scaled_width >= static_cast<std::uint64_t>(height) * 20U
            ? host::AspectPolicy::preserve_21x9 : host::AspectPolicy::preserve_16x9;
    } else result.aspect = host::AspectPolicy::preserve_4x3;
    result.target_refresh_hz=100U; result.audio_target_buffer_frames=audio_latency_frames;
    result.audio_max_buffer_frames=audio_max_frames; return result;
}

std::string runtime_settings_json(const RuntimeSettings& s) {
    return std::string{"{\"audio\":{\"device\":\""}+s.audio_device+"\",\"latency_frames\":"+std::to_string(s.audio_latency_frames)+
        ",\"max_queued_frames\":"+std::to_string(s.audio_max_frames)+"},\"display\":{\"aspect\":\""+(s.ultrawide?"auto-ultrawide":"native-4:3")+
        "\",\"filter\":\""+(s.presentation_filter==host::PresentationFilter::Linear?"linear":"nearest")+"\",\"height\":"+std::to_string(s.height)+
        ",\"mode\":\""+mode(s.fullscreen)+"\",\"width\":"+std::to_string(s.width)+"},\"graphics\":{\"hd_assets\":"+(s.hd_assets?"true":"false")+
        ",\"pgxp\":"+(s.pgxp?"true":"false")+",\"true_color\":"+(s.true_color?"true":"false")+"},\"input\":{\"deadzone_per_mille\":"+
        std::to_string(s.deadzone_per_mille)+",\"rumble\":"+(s.rumble?"true":"false")+"},\"presentation\":{\"policy\":\""+policy(s.present_policy)+
        "\",\"rate_hz\":100},\"saves\":{\"atomic\":"+(s.atomic_saves?"true":"false")+",\"card_slots\":"+std::to_string(s.card_slots)+
        "},\"schema\":\"wipeout3se.runtime-settings.v2\"}\n";
}

SettingsResult load_runtime_settings(const std::filesystem::path& path, bool reset) noexcept {
    SettingsResult result{};
    try {
        if(reset||!std::filesystem::exists(path)) { result.status=reset?SettingsStatus::reset:SettingsStatus::created; if(!write_atomic(path,runtime_settings_json(result.value))) result.status=SettingsStatus::io_error; return result; }
        std::ifstream input(path,std::ios::binary); std::string bytes((std::istreambuf_iterator<char>(input)),{});
        if(bytes.size()>64U*1024U) { result.error="settings file exceeds limit"; return result; }
        if(!parse(bytes,result.value,result.error)) return result;
        result.status=SettingsStatus::loaded; return result;
    } catch(const std::exception& exception) { result.status=SettingsStatus::io_error; result.error=exception.what(); return result; }
}

bool apply_settings_override(RuntimeSettings& s, std::string_view assignment, std::string& error) noexcept {
    const auto separator=assignment.find('='); if(separator==std::string_view::npos){error="override must be key=value";return false;}
    const auto key=assignment.substr(0,separator), value=assignment.substr(separator+1);
    auto integer=[&](std::uint32_t& target,std::uint32_t low,std::uint32_t high){std::uint64_t parsed=0;for(char c:value){if(c<'0'||c>'9')return false;parsed=parsed*10U+static_cast<unsigned>(c-'0');if(parsed>high)return false;}if(parsed<low)return false;target=static_cast<std::uint32_t>(parsed);return true;};
    if(key=="display.width") return integer(s.width,640,7680);
    if(key=="display.height") return integer(s.height,480,4320);
    if(key=="audio.latency_frames") { auto candidate=s.audio_latency_frames; if(!integer(candidate,441,8192)||candidate>s.audio_max_frames){error="invalid audio latency";return false;}s.audio_latency_frames=candidate;return true; }
    if(key=="audio.max_queued_frames") { auto candidate=s.audio_max_frames; if(!integer(candidate,441,16384)||candidate<s.audio_latency_frames){error="invalid audio queue limit";return false;}s.audio_max_frames=candidate;return true; }
    if(key=="input.deadzone_per_mille") { std::uint32_t candidate=s.deadzone_per_mille;if(!integer(candidate,0,500)){error="invalid controller deadzone";return false;}s.deadzone_per_mille=static_cast<std::uint16_t>(candidate);return true; }
    if(key=="display.mode") { if(value=="windowed")s.fullscreen=host::FullscreenMode::windowed;else if(value=="borderless")s.fullscreen=host::FullscreenMode::borderless;else if(value=="exclusive")s.fullscreen=host::FullscreenMode::exclusive;else{error="invalid display mode";return false;}return true; }
    if(key=="display.aspect") { if(value=="auto-ultrawide")s.ultrawide=true;else if(value=="native-4:3")s.ultrawide=false;else{error="invalid aspect policy";return false;}return true; }
    if(key=="display.filter") { if(value=="linear")s.presentation_filter=host::PresentationFilter::Linear;else if(value=="nearest")s.presentation_filter=host::PresentationFilter::Nearest;else{error="invalid presentation filter";return false;}return true; }
    if(key=="presentation.policy") { if(value=="low-latency-100hz")s.present_policy=host::SdlPresentPolicy::low_latency_100hz;else if(value=="vsync")s.present_policy=host::SdlPresentPolicy::vsync;else if(value=="mailbox")s.present_policy=host::SdlPresentPolicy::mailbox;else{error="invalid present policy";return false;}return true; }
    if(key=="graphics.pgxp"||key=="graphics.true_color"||key=="graphics.hd_assets"||key=="input.rumble") {bool* target=key=="graphics.pgxp"?&s.pgxp:key=="graphics.true_color"?&s.true_color:key=="graphics.hd_assets"?&s.hd_assets:&s.rumble;if(value=="on")*target=true;else if(value=="off")*target=false;else{error="boolean override must be on/off";return false;}return true;}
    error="unknown settings override"; return false;
}
} // namespace clean_aot::app
