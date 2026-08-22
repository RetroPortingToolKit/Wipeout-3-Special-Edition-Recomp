#include "clean_aot/app/application.hpp"
#include "clean_aot/host/headless.hpp"
#include "../src/sha256.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
int failures=0;
void expect(bool condition,const char* message){if(!condition){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
std::string hash(std::span<const std::uint8_t> bytes){return clean_aot::app::detail::sha256(bytes);}
std::string hash(std::string_view value){return hash(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
void write(const std::filesystem::path& path,const std::string& text){write(path,std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size()));}

bool synthetic_region(std::uint32_t& pc,bool branch) noexcept {pc=branch?0x1008U:0x1004U;return true;}

void test_authorized_synthetic_end_to_end(){
    const auto root=std::filesystem::temp_directory_path()/"clean-aot-app-contract";
    std::filesystem::remove_all(root);std::filesystem::create_directories(root/"artifacts");
    const std::array<std::uint8_t,8U> executable{1U,2U,3U,4U,5U,6U,7U,8U};
    const std::string artifact="synthetic-native-artifact";
    const std::vector<std::uint8_t> bios(512U*1024U,0U);
    const std::string exe_hash=hash(executable);const std::string artifact_hash=hash(artifact);const std::string bios_hash=hash(bios);
    const std::string identity_json="{\"load_range\":{\"end_exclusive\":4104,\"start\":4096},\"relocation_metadata\":null,\"sha256\":\""+exe_hash+"\"}";
    const std::string identity=hash(identity_json);
    const std::string catalog="{\"address_bits\":32,\"catalog_id\":\"synthetic\",\"page_size\":4096,\"schema\":\"psxrecomp.clean-aot.catalog.v1\",\"stack_window\":{\"end_exclusive\":8192,\"start\":6144},\"variants\":[{\"artifact\":{\"id\":\"synthetic\",\"path\":\"synthetic.bin\",\"sha256\":\""+artifact_hash+"\"},\"delay_slot_owners\":[],\"dispatch_roots\":[4096],\"executed_ranges\":[{\"end_exclusive\":4104,\"start\":4096}],\"load_range\":{\"end_exclusive\":4104,\"start\":4096},\"relocation_metadata\":null,\"sha256\":\""+exe_hash+"\",\"source_media\":{\"files\":[],\"format\":\"synthetic\",\"source_id\":\"test\"}}]}";
    const std::string catalog_hash=hash(catalog);
    const std::string input_hash=hash("synthetic-catalog-input");
    const std::string setup="{\"catalog\":{\"input_sha256\":\""+input_hash+"\"},\"media\":{\"bios\":{\"sha256\":\""+bios_hash+"\",\"size\":524288}},\"schema\":\"psxrecomp.clean-aot.product-receipt.v1\",\"status\":\"setup-complete\"}";
    const std::string finalize="{\"catalog_input_sha256\":\""+input_hash+"\",\"catalog_path\":\"catalog.json\",\"catalog_sha256\":\""+catalog_hash+"\",\"runtime_policy\":{\"unknown_variant_behavior\":\"reject-before-dispatch\"},\"schema\":\"psxrecomp.clean-aot.product-finalize-receipt.v1\",\"status\":\"catalog-finalized\"}";
    const std::string selection="{\"artifact\":{\"id\":\"synthetic\",\"path\":\"synthetic.bin\",\"sha256\":\""+artifact_hash+"\",\"size\":"+std::to_string(artifact.size())+"},\"binding\":{\"dispatch_roots\":[4096],\"executed_ranges\":[{\"end_exclusive\":4104,\"start\":4096}],\"load_range\":{\"end_exclusive\":4104,\"start\":4096}},\"catalog_sha256\":\""+catalog_hash+"\",\"schema\":\"psxrecomp.clean-aot.selection.v1\",\"status\":\"selected\",\"variant_identity_sha256\":\""+identity+"\"}";
    write(root/"setup.json",setup);write(root/"finalize.json",finalize);write(root/"catalog.json",catalog);write(root/"selection.json",selection);write(root/"executable.bin",executable);write(root/"artifacts"/"synthetic.bin",artifact);write(root/"bios.bin",bios);
    const clean_aot::app::LaunchInputs inputs{root/"setup.json",root/"finalize.json",root/"catalog.json",root/"selection.json",root/"executable.bin",root/"artifacts",root/"bios.bin"};
    const std::array entries{clean_aot::runtime::GeneratedRegionEntry{0x1000U,synthetic_region}};
    const std::array<std::uint32_t,1U> roots{0x1000U};
    const clean_aot::runtime::GeneratedExecutableBinding binding{identity,exe_hash,0x1000U,0x1008U,roots,entries};
    auto authorization=clean_aot::app::authorize_launch(inputs,binding);expect(authorization.authorized(),"finalized measured inputs authorize synthetic binding");
    const std::string wrong_executable_hash(64U,'f');
    const clean_aot::runtime::GeneratedExecutableBinding wrong_binding{identity,wrong_executable_hash,0x1000U,0x1008U,roots,entries};
    expect(!clean_aot::app::authorize_launch(inputs,wrong_binding).authorized(),"compiled executable identity mismatch fails closed");
    clean_aot::host::HeadlessBackend host;clean_aot::app::Application app(host);
    expect(app.initialize(std::move(authorization),binding,{1280U,720U,clean_aot::host::FullscreenMode::windowed})==clean_aot::app::AppCode::Ok,"authenticated shell initializes runtime and host");
    std::array<std::uint8_t,4U> pixels{10U,20U,30U,255U};std::array<std::int16_t,882U> audio{};
    clean_aot::app::Update update{};update.pc=0x1000U;update.retirement_cycle=100U;update.frame={0U,0U,{pixels.data(),4U,1U,1U,4U,clean_aot::host::PixelFormat::rgba8_unorm}};update.audio={0U,audio.data(),441U};update.controller.sample_index=0U;
    expect(app.run_update(update)==clean_aot::app::AppCode::Ok,"generated synthetic region runs through runtime and host");expect(update.pc==0x1004U,"generated region changed PC");expect(host.stats().frames_presented==1U&&host.stats().audio_batches_consumed==1U,"frame and audio reached host exactly once");
    write(root/"executable.bin",std::string("tampered"));expect(!clean_aot::app::authorize_launch(inputs,binding).authorized(),"tampered executable fails closed before startup");
    write(root/"executable.bin",executable);
    write(root/"finalize.json",finalize+"{}");expect(!clean_aot::app::authorize_launch(inputs,binding).authorized(),"non-canonical trailing finalize data fails closed");
    app.shutdown();std::filesystem::remove_all(root);
}
} // namespace
int main(){expect(hash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 implementation matches known vector");test_authorized_synthetic_end_to_end();if(failures!=0){std::cerr<<failures<<" app tests failed\n";return 1;}std::cout<<"clean_aot_app_tests: all checks passed\n";return 0;}
