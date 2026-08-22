#include "clean_aot/app/graphics.hpp"
#include "clean_aot/host/headless.hpp"
#include "../src/sha256.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
class FakePngDecoder final : public clean_aot::app::IPngDecoder {
public:
    bool decode(std::span<const std::uint8_t> png, std::uint32_t width,
                std::uint32_t height,
                std::vector<std::uint8_t>& rgba8) noexcept override {
        ++calls;
        if (png.size() != 4U || width != 1U || height != 1U) return false;
        rgba8 = {png[0], png[1], png[2], png[3]}; return true;
    }
    std::uint32_t calls = 0U;
};
class FakeUploader final : public clean_aot::host::IReplacementUploader {
public:
    clean_aot::host::HostStatus upload_replacement(
        const clean_aot::host::ReplacementImage& replacement,
        std::uint64_t& handle) noexcept override {
        ++calls; last_rgba.assign(replacement.rgba8.begin(),replacement.rgba8.end());
        handle = 100U + calls; return {};
    }
    std::uint64_t calls = 0U;
    std::vector<std::uint8_t> last_rgba{};
};
void test_runtime_scanout() {
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    clean_aot::runtime::Configuration config{}; config.bios = bios;
    clean_aot::runtime::Runtime runtime(config); auto& gpu = runtime.gpu();
    expect(gpu.write_gp1(0x03000000U).accepted, "display enables");
    expect(gpu.write_gp1(0x05000000U).accepted, "display origin programs");
    expect(gpu.write_gp1(0x06c00200U).accepted, "horizontal range programs");
    expect(gpu.write_gp1(0x07040010U).accepted, "vertical range programs");
    expect(gpu.write_gp1(0x08000001U).accepted, "320-pixel mode programs");
    expect(gpu.write_gp0(0x02ff00ffU).accepted && gpu.write_gp0(0U).accepted &&
           gpu.write_gp0((240U << 16U) | 320U).accepted,
           "native fill packet queues");
    clean_aot::host::HeadlessBackend host;
    clean_aot::app::RuntimeGraphics graphics({true, 3840U, 2160U,
        clean_aot::raster::PresentationMode::Identity4x3, 4U, 3U,
        clean_aot::app::FontManifestPolicy::ManifestOnly});
    expect(graphics.present(runtime, host, 0U).success(),
           "runtime GPU queue drains into 4K scanout");
    expect(gpu.event_count() == 0U && graphics.vram_generation() == 2U,
           "packet drains once with generation tracking");
    expect(host.service().success() && host.stats().frames_presented == 1U,
           "scanout reaches host backend");
    std::cout << "runtime visual digest: 0x" << std::hex
              << graphics.visual_digest() << std::dec << '\n';
    expect(graphics.visual_digest() == 0xa0c4b5214903b3ddULL,
           "runtime visual digest remains deterministic");
}
void test_replacement_runtime() {
    const auto root = std::filesystem::temp_directory_path() /
        "clean-aot-replacement-runtime";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root / "textures");
    const std::array<std::uint8_t, 4U> bytes{1U, 2U, 3U, 4U};
    { std::ofstream file(root / "textures" / "pixel.png", std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size())); }
    const psx::render::ReplacementKey key{0x12345678U, 0x90abcdefU};
    const std::array entries{psx::render::ReplacementManifestEntry{
        psx::render::ReplacementAssetKind::Texture, key, "textures/pixel.png",
        clean_aot::app::detail::sha256(bytes), 1U, 1U}};
    auto manifest = psx::render::ReplacementManifest::create(entries, 7U);
    expect(manifest.has_value(), "replacement manifest configures");
    FakePngDecoder decoder; FakeUploader uploader;
    clean_aot::app::ReplacementAssetRuntime assets(std::move(*manifest), root, decoder, uploader);
    std::uint64_t first = 0U, cached = 0U, regenerated = 0U;
    expect(assets.resolve_and_upload(psx::render::ReplacementAssetKind::Texture,
        key, 11U, first).success() && first == 101U, "verified PNG decodes and uploads");
    expect(assets.resolve_and_upload(psx::render::ReplacementAssetKind::Texture,
        key, 11U, cached).success() && cached == first && uploader.calls == 1U,
        "current identity reuses upload");
    expect(assets.resolve_and_upload(psx::render::ReplacementAssetKind::Texture,
        key, 12U, regenerated).success() && regenerated == 102U && decoder.calls == 2U,
        "generation change invalidates upload");
    expect(uploader.last_rgba == std::vector<std::uint8_t>({1U, 2U, 3U, 4U}),
           "decoded RGBA crosses upload boundary exactly");
    auto disabled_manifest = psx::render::ReplacementManifest::create(entries, 8U);
    clean_aot::app::ReplacementAssetRuntime fonts_disabled(
        std::move(*disabled_manifest), root, decoder, uploader,
        clean_aot::app::FontManifestPolicy::Disabled);
    std::uint64_t font_handle = 0U;
    expect(fonts_disabled.resolve_and_upload(psx::render::ReplacementAssetKind::Font,
        key, 1U, font_handle).code == clean_aot::host::HostCode::unsupported,
        "font replacement remains an explicit title-policy seam");
    std::filesystem::remove_all(root);
}
void test_native_png_decode() {
#if defined(_WIN32)
    const std::array<std::uint8_t, 70U> png{
        0x89U,0x50U,0x4eU,0x47U,0x0dU,0x0aU,0x1aU,0x0aU,0x00U,0x00U,
        0x00U,0x0dU,0x49U,0x48U,0x44U,0x52U,0x00U,0x00U,0x00U,0x01U,
        0x00U,0x00U,0x00U,0x01U,0x08U,0x06U,0x00U,0x00U,0x00U,0x1fU,
        0x15U,0xc4U,0x89U,0x00U,0x00U,0x00U,0x0dU,0x49U,0x44U,0x41U,
        0x54U,0x78U,0x9cU,0x63U,0x10U,0x54U,0x32U,0x76U,0x01U,0x00U,
        0x01U,0x59U,0x00U,0xabU,0x13U,0x2aU,0x25U,0xabU,0x00U,0x00U,
        0x00U,0x00U,0x49U,0x45U,0x4eU,0x44U,0xaeU,0x42U,0x60U,0x82U};
    clean_aot::app::NativePngDecoder decoder;
    std::vector<std::uint8_t> rgba;
    expect(decoder.decode(png, 1U, 1U, rgba) &&
           rgba == std::vector<std::uint8_t>({0x11U,0x22U,0x33U,0x44U}),
           "native WIC PNG boundary emits exact RGBA8");
    expect(!decoder.decode(png, 2U, 1U, rgba),
           "native PNG boundary rejects manifest dimension mismatch");
#endif
}
} // namespace
int main() {
    test_runtime_scanout(); test_replacement_runtime(); test_native_png_decode();
    if (failures != 0) return 1;
    std::cout << "clean_aot_app_graphics_tests: all checks passed\n"; return 0;
}
