#include "clean_aot/app/graphics.hpp"
#include "clean_aot/host/headless.hpp"
#include "../src/sha256.hpp"

#include <array>
#include <cstdlib>
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
class SolidRedDecoder final : public clean_aot::app::IPngDecoder {
public:
    bool decode(std::span<const std::uint8_t>, std::uint32_t width,
                std::uint32_t height,
                std::vector<std::uint8_t>& rgba8) noexcept override {
        rgba8.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
        for (std::size_t i = 0U; i < rgba8.size(); i += 4U) {
            rgba8[i] = 255U; rgba8[i + 3U] = 255U;
        }
        return true;
    }
};
class FakeUploader final : public clean_aot::host::IReplacementUploader {
public:
    clean_aot::host::HostStatus upload_replacement(
        const clean_aot::host::ReplacementImage& replacement,
        std::uint64_t& handle) noexcept override {
        ++calls; last_rgba.assign(replacement.rgba8.begin(),replacement.rgba8.end());
        handle = 100U + calls; return {};
    }
    clean_aot::host::HostStatus release_replacement(
        std::uint64_t handle) noexcept override {
        if (handle == 0U) return {clean_aot::host::HostCode::invalid_argument};
        ++releases; last_released = handle; return {};
    }
    std::uint64_t calls = 0U;
    std::uint64_t releases = 0U;
    std::uint64_t last_released = 0U;
    std::vector<std::uint8_t> last_rgba{};
};
class MissingPolicy final : public clean_aot::app::IReplacementKeyPolicy {
public:
    bool identify(const clean_aot::gpu::Event&,
                  std::span<const clean_aot::raster::VramWord>, std::uint64_t,
                  clean_aot::app::ReplacementMatch& match) noexcept override {
        match.kind = psx::render::ReplacementAssetKind::Texture;
        match.key = {0xdeadbeefU, 0x12345678U};
        match.source_generation = 1U;
        return true;
    }
    void observe(const clean_aot::gpu::Event&,
                 std::span<const clean_aot::raster::VramWord>,
                 std::uint64_t) noexcept override {}
    std::uint64_t invalidation_serial() const noexcept override { return 0U; }
};

void test_beetle_upload_key_derivation_and_sampling() {
    using namespace clean_aot;
    const psx::render::ReplacementKey expected{0x8043be1bU, 0xd03041bdU};
    const std::array mappings{app::TitleAssetMapping{
        expected, app::TitleAssetClass::Banner}};
    app::Wip3outUploadKeyPolicy policy(mappings);
    raster::ReferenceRasterizer raster;
    gpu::VramUploadEvent upload{};
    upload.destination = {0U, 0U}; upload.extent = {4U, 1U};
    upload.pixel_count = 4U; upload.padded_word_count = 2U;
    upload.data_words = {0xabcd1234U, 0xffff0000U};
    expect(raster.consume(upload).accepted(), "key-vector upload applies");
    policy.observe(upload, raster.vram_words(), raster.vram_generation());
    gpu::VramUploadEvent palette{};
    palette.destination = {16U, 0U}; palette.extent = {16U, 1U};
    palette.pixel_count = 16U; palette.padded_word_count = 8U;
    for (std::uint32_t value = 0U; value < 16U; value += 2U) {
        palette.data_words.push_back(value | ((value + 1U) << 16U));
    }
    expect(raster.consume(palette).accepted(), "key-vector CLUT applies");
    policy.observe(palette, raster.vram_words(), raster.vram_generation());
    gpu::RectangleEvent draw{};
    draw.textured = true; draw.raw_texture = true;
    draw.texture_coordinate = gpu::TextureCoordinate{};
    draw.clut = gpu::ClutAttribute{0U, 16U, 0U};
    draw.width = 16U; draw.height = 1U; draw.origin = {0U, 0, 10};
    draw.state.draw_mode.depth = gpu::TextureDepth::Indexed4;
    draw.state.draw_area.left = 0U; draw.state.draw_area.top = 0U;
    draw.state.draw_area.right = 1023U; draw.state.draw_area.bottom = 511U;
    app::ReplacementMatch match{};
    expect(policy.identify(draw, raster.vram_words(), raster.vram_generation(),
                           match) && match.key == expected &&
           match.source_generation != 0U &&
           policy.stats().upload_bytes_hashed == 40U &&
           policy.stats().palette_bytes_hashed == 32U,
           "stock Beetle CRC32 upload and CLUT vectors identify manifest key");
    app::ReplacementMatch cached_match{};
    bool memoized = true;
    for (std::size_t query = 0U; query < 100U; ++query) {
        memoized = memoized && policy.identify(
            draw, raster.vram_words(), raster.vram_generation(), cached_match) &&
            cached_match.key == expected;
    }
    expect(memoized && policy.stats().palette_hashes == 1U &&
           policy.stats().palette_cache_hits == 100U &&
           policy.stats().upload_bytes_hashed == 40U,
           "100 unchanged draws perform no repeated upload or CLUT hashing");
    std::vector<std::uint8_t> red(16U * 4U, 0U);
    for (std::size_t i = 0U; i < 16U; ++i) {
        red[i * 4U] = 255U; red[i * 4U + 3U] = 255U;
    }
    const raster::ReplacementTexture replacement{
        7U, match.source, match.depth, 16U, 1U, red};
    expect(raster.consume(draw, &replacement).accepted() &&
           (raster.read_vram(0U, 10U) & 0x7fffU) == 0x001fU,
           "resolved replacement handle drives primitive texel sampling");
    gpu::VramFillEvent overwrite{};
    overwrite.origin = {0U, 0U}; overwrite.extent = {4U, 1U};
    overwrite.color = {0U, 0U, 0U, 0U};
    expect(raster.consume(overwrite).accepted(), "overlap invalidation applies");
    policy.observe(overwrite, raster.vram_words(), raster.vram_generation());
    expect(!policy.identify(draw, raster.vram_words(), raster.vram_generation(),
                            match) && policy.stats().invalidated_uploads == 1U,
           "overlapping VRAM mutation invalidates producer identity");

    policy.observe(upload, raster.vram_words(), raster.vram_generation());
    gpu::LineEvent line{};
    line.vertices = {{0, 0}, {3, 0}};
    line.state.draw_area.right = 1023U;
    line.state.draw_area.bottom = 511U;
    policy.observe(line, raster.vram_words(), raster.vram_generation());
    expect(!policy.identify(draw, raster.vram_words(), raster.vram_generation(),
                            match) && policy.stats().invalidated_uploads == 2U,
           "line destination invalidates an overlapping texture upload");

    policy.observe(upload, raster.vram_words(), raster.vram_generation());
    gpu::VramFillEvent wrapped{};
    wrapped.origin = {1023U, 0U}; wrapped.extent = {2U, 1U};
    policy.observe(wrapped, raster.vram_words(), raster.vram_generation());
    expect(!policy.identify(draw, raster.vram_words(), raster.vram_generation(),
                            match) && policy.stats().invalidated_uploads == 4U,
           "wrapped VRAM mutation conservatively retires producer identities");
}

void test_live_replacement_pipeline() {
    using namespace clean_aot;
    const psx::render::ReplacementKey key{0x8043be1bU, 0xd03041bdU};
    const auto root = std::filesystem::temp_directory_path() /
        "clean-aot-live-replacement";
    std::filesystem::remove_all(root); std::filesystem::create_directories(root);
    const std::array<std::uint8_t, 4U> bytes{1U, 2U, 3U, 4U};
    { std::ofstream file(root / "replacement.png", std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), 4); }
    const std::array entries{psx::render::ReplacementManifestEntry{
        psx::render::ReplacementAssetKind::Texture, key, "replacement.png",
        app::detail::sha256(bytes), 16U, 1U}};
    auto manifest = psx::render::ReplacementManifest::create(entries, 1U);
    SolidRedDecoder decoder; host::HeadlessBackend host;
    app::ReplacementAssetRuntime assets(std::move(*manifest), root, decoder, host);
    const std::array mappings{app::TitleAssetMapping{
        key, app::TitleAssetClass::Banner}};
    app::Wip3outUploadKeyPolicy policy(mappings);
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    runtime::Configuration config{}; config.bios = bios;
    runtime::Runtime runtime(config); auto& gpu = runtime.gpu();
    const auto send = [&](std::uint32_t word) {
        expect(gpu.write_gp0(word).accepted, "live replacement GP0 word accepts");
    };
    expect(gpu.write_gp1(0x03000000U).accepted &&
           gpu.write_gp1(0x05000000U).accepted &&
           gpu.write_gp1(0x06c00200U).accepted &&
           gpu.write_gp1(0x07040010U).accepted &&
           gpu.write_gp1(0x08000001U).accepted,
           "live replacement display configures");
    send(0xa0000000U); send(0U); send((1U << 16U) | 4U);
    send(0xabcd1234U); send(0xffff0000U);
    send(0xa0000000U); send(16U); send((1U << 16U) | 16U);
    for (std::uint32_t value = 0U; value < 16U; value += 2U) {
        send(value | ((value + 1U) << 16U));
    }
    send(0xe1000000U); send(0xe3000000U);
    send(0xe4000000U | (511U << 10U) | 1023U);
    send(0x65000000U); send(10U << 16U); send(1U << 16U);
    send((1U << 16U) | 16U);
    app::RuntimeGraphics graphics({true, 3840U, 2160U,
        raster::PresentationMode::Identity4x3, 4U, 3U,
        app::FontManifestPolicy::ManifestOnly, &assets, &policy});
    expect(graphics.present(runtime, host, 0U).success() && host.service().success() &&
           graphics.visual_receipt().replacement_hits == 1U &&
           assets.stats().uploaded == 1U && policy.stats().identified == 1U,
           "live GPU queue hashes, resolves, samples, and presents replacement");
    expect(host.stats().frame_digest == 0x333c8703b0e2e1e8ULL,
           "live replacement frame digest remains deterministic");
    std::cout << "replacement frame digest: 0x" << std::hex
              << host.stats().frame_digest << std::dec << '\n';
    send(0x02000000U); send(0U); send((1U << 16U) | 4U);
    expect(graphics.present(runtime, host, 1U).success() && host.service().success() &&
           host.stats().replacements_released == 1U &&
           assets.stats().invalidated == 1U,
           "live overlapping VRAM write immediately retires stale GPU handle");
    std::filesystem::remove_all(root);
}
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
    const auto receipt = graphics.visual_receipt();
    std::cout << "4K visual receipt: " << receipt.canonical_json() << '\n';
    expect(receipt.output_width == 3840U && receipt.output_height == 2160U &&
           receipt.content_x == 480U && receipt.content_width == 2880U &&
           receipt.identity_4x3 &&
           receipt.canonical_json().find("clean-aot.visual-receipt.v1") !=
               std::string::npos,
           "live 4K identity receipt records exact pillarboxed content");
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
    expect(uploader.releases == 1U && uploader.last_released == first &&
           assets.stats().invalidated == 1U,
           "generation change retires stale GPU handle");
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
void test_missing_asset_native_fallback() {
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    clean_aot::runtime::Configuration config{}; config.bios = bios;
    clean_aot::runtime::Runtime runtime(config); auto& gpu = runtime.gpu();
    expect(gpu.write_gp1(0x03000000U).accepted &&
           gpu.write_gp1(0x05000000U).accepted &&
           gpu.write_gp1(0x06c00200U).accepted &&
           gpu.write_gp1(0x07040010U).accepted &&
           gpu.write_gp1(0x08000001U).accepted,
           "fallback display configures");
    expect(gpu.write_gp0(0x02ff00ffU).accepted && gpu.write_gp0(0U).accepted &&
           gpu.write_gp0((240U << 16U) | 320U).accepted,
           "fallback native fill packet queues");
    const std::array entries{psx::render::ReplacementManifestEntry{
        psx::render::ReplacementAssetKind::Texture, {1U, 2U}, "unused.png",
        std::string(64U, '0'), 1U, 1U}};
    auto manifest = psx::render::ReplacementManifest::create(entries, 1U);
    FakePngDecoder decoder; FakeUploader uploader; MissingPolicy policy;
    clean_aot::app::ReplacementAssetRuntime assets(
        std::move(*manifest), std::filesystem::temp_directory_path(), decoder,
        uploader);
    clean_aot::host::HeadlessBackend host;
    clean_aot::app::RuntimeGraphics graphics({true, 3840U, 2160U,
        clean_aot::raster::PresentationMode::Identity4x3, 4U, 3U,
        clean_aot::app::FontManifestPolicy::ManifestOnly, &assets, &policy});
    expect(graphics.present(runtime, host, 0U).success() &&
           graphics.visual_digest() == 0xa0c4b5214903b3ddULL &&
           graphics.visual_receipt().native_fallbacks == 1U &&
           uploader.calls == 0U,
           "missing HD asset preserves byte-exact native 4:3 output");
}
void test_ultrawide_visual_receipt() {
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    clean_aot::runtime::Configuration config{}; config.bios = bios;
    clean_aot::runtime::Runtime runtime(config); auto& gpu = runtime.gpu();
    expect(gpu.write_gp1(0x03000000U).accepted &&
           gpu.write_gp1(0x05000000U).accepted &&
           gpu.write_gp1(0x06c00200U).accepted &&
           gpu.write_gp1(0x07040010U).accepted &&
           gpu.write_gp1(0x08000001U).accepted &&
           gpu.write_gp0(0x02ff00ffU).accepted && gpu.write_gp0(0U).accepted &&
           gpu.write_gp0((240U << 16U) | 320U).accepted,
           "ultrawide visual packet configures");
    clean_aot::host::HeadlessBackend host;
    clean_aot::app::RuntimeGraphics graphics({true, 5120U, 2160U,
        clean_aot::raster::PresentationMode::Generic21x9, 21U, 9U,
        clean_aot::app::FontManifestPolicy::ManifestOnly});
    expect(graphics.present(runtime, host, 0U).success(),
           "5120x2160 frame reaches live presentation boundary");
    const auto receipt = graphics.visual_receipt();
    std::cout << "ultrawide visual receipt: " << receipt.canonical_json() << '\n';
    expect(receipt.output_width == 5120U && receipt.output_height == 2160U &&
           receipt.content_x == 40U && receipt.content_y == 0U &&
           receipt.content_width == 5040U && receipt.content_height == 2160U &&
           !receipt.identity_4x3,
           "21:9 receipt records deterministic 4K ultrawide viewport");
}
void test_title_manifest_and_receipts() {
    const auto repository = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path().parent_path();
    std::string error;
    auto loaded = clean_aot::app::load_wip3out_hd_manifest(
        repository / "assets" / "manifests" / "wip3out-hd-textures.json",
        1U, error);
    if (!loaded.has_value()) {
        expect(false, "permitted pack manifest loads for native decode");
        return;
    }
    expect(loaded.has_value() && loaded->manifest.size() == 292U,
           "permitted HD manifest maps every present asset");
    const auto font = psx::render::parse_replacement_key("3dc173e1-3da5d410");
    const auto banner = psx::render::parse_replacement_key("11b5d8d9-41d69974");
    expect(font.has_value() && loaded.has_value() &&
           loaded->classify(*font) == clean_aot::app::TitleAssetClass::Font,
           "font manifest key maps through title policy");
    expect(banner.has_value() && loaded.has_value() &&
           loaded->classify(*banner) == clean_aot::app::TitleAssetClass::Banner,
           "menu bar manifest key maps as banner");
}
void test_permitted_pack_when_available() {
    const char* root_value = std::getenv("CLEAN_AOT_HD_ASSET_ROOT");
    if (root_value == nullptr || *root_value == '\0') return;
    const auto repository = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path().parent_path();
    std::string error;
    auto loaded = clean_aot::app::load_wip3out_hd_manifest(
        repository / "assets" / "manifests" / "wip3out-hd-textures.json",
        1U, error);
    if (!loaded.has_value()) {
        expect(false, "permitted pack manifest loads for native decode");
        return;
    }
    clean_aot::app::NativePngDecoder decoder;
    clean_aot::host::HeadlessBackend uploader;
    clean_aot::app::ReplacementAssetRuntime assets(
        std::move(loaded->manifest), root_value, decoder, uploader);
    const auto key = psx::render::parse_replacement_key("11b5d8d9-41d69974");
    std::uint64_t handle = 0U;
    expect(key.has_value() && assets.resolve_and_upload(
        psx::render::ReplacementAssetKind::Texture, *key, 1U, handle).success() &&
        handle != 0U && assets.stats().decoded == 1U &&
        assets.stats().rgba_bytes == 256U * 112U * 4U,
        "permitted pack PNG decodes and crosses the real upload boundary");
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
    test_beetle_upload_key_derivation_and_sampling();
    test_live_replacement_pipeline();
    test_runtime_scanout(); test_replacement_runtime(); test_native_png_decode();
    test_title_manifest_and_receipts(); test_missing_asset_native_fallback();
    test_ultrawide_visual_receipt();
    test_permitted_pack_when_available();
    if (failures != 0) return 1;
    std::cout << "clean_aot_app_graphics_tests: all checks passed\n"; return 0;
}
