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
class UiStripeDecoder final : public clean_aot::app::IPngDecoder {
public:
    bool decode(std::span<const std::uint8_t>, std::uint32_t width,
                std::uint32_t height,
                std::vector<std::uint8_t>& rgba8) noexcept override {
        try {
            rgba8.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
            for (std::uint32_t y = 0U; y < height; ++y) {
                for (std::uint32_t x = 0U; x < width; ++x) {
                    const std::size_t offset =
                        (static_cast<std::size_t>(y) * width + x) * 4U;
                    const std::uint8_t value = (x & 1U) == 0U ? 255U : 0U;
                    rgba8[offset] = value;
                    rgba8[offset + 1U] = value;
                    rgba8[offset + 2U] = value;
                    rgba8[offset + 3U] = 255U;
                }
            }
            return true;
        } catch (...) { return false; }
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
    void observe_precise_draw(clean_aot::raster::VramRect) noexcept override {}
    std::uint64_t invalidation_serial() const noexcept override { return 0U; }
};

class LiveProvenance final : public clean_aot::app::IPolygonProvenanceProvider {
public:
    bool resolve(const clean_aot::gpu::PolygonEvent& event,
                 clean_aot::app::PolygonProvenanceView& view) noexcept override {
        try {
            if (event.vertex_count != 3U) return false;
            for (std::size_t i = 0U; i < 3U; ++i) {
                const psx::render::NativePosition native{
                    event.vertices[i].x, event.vertices[i].y};
                const auto gte = state.write_gte(
                    static_cast<psx::render::GteRegister>(i), native,
                    {static_cast<double>(native.x) + 0.25,
                     static_cast<double>(native.y) + 0.5,
                     2.0 + static_cast<double>(i), 1.0 + static_cast<double>(i)});
                const std::uint32_t raw = static_cast<std::uint16_t>(native.x) |
                    (static_cast<std::uint32_t>(static_cast<std::uint16_t>(native.y)) << 16U);
                if (!state.store_memory(0x100U + static_cast<std::uint32_t>(i) * 4U,
                                        raw, gte.candidate().enhancement)
                         .carried_precise()) return false;
                candidates[i] = state.load_memory(
                    0x100U + static_cast<std::uint32_t>(i) * 4U)->candidate();
            }
            view = {candidates, &state};
            return true;
        } catch (...) { return false; }
    }
    psx::render::ProvenanceState state{};
    std::array<psx::render::VertexCandidate, 3U> candidates{};
};
class RejectProvenance final : public clean_aot::app::IPolygonProvenanceProvider {
public:
    bool resolve(const clean_aot::gpu::PolygonEvent&,
                 clean_aot::app::PolygonProvenanceView&) noexcept override {
        return false;
    }
};
class IdentityProvenance final : public clean_aot::app::IPolygonProvenanceProvider {
public:
    bool resolve(const clean_aot::gpu::PolygonEvent& event,
                 clean_aot::app::PolygonProvenanceView& view) noexcept override {
        try {
            if (event.vertex_count < 3U || event.vertex_count > 4U) return false;
            for (std::size_t i = 0U; i < event.vertex_count; ++i) {
                const psx::render::NativePosition native{
                    event.vertices[i].x, event.vertices[i].y};
                const auto gte = state.write_gte(
                    static_cast<psx::render::GteRegister>(i % 3U), native,
                    {static_cast<double>(native.x), static_cast<double>(native.y),
                     1.0, 1.0});
                const std::uint32_t address =
                    0x400U + static_cast<std::uint32_t>(i) * 4U;
                const std::uint32_t raw = static_cast<std::uint16_t>(native.x) |
                    (static_cast<std::uint32_t>(
                         static_cast<std::uint16_t>(native.y)) << 16U);
                if (!state.store_memory(address, raw, gte.candidate().enhancement)
                         .carried_precise()) return false;
                candidates[i] = state.load_memory(address)->candidate();
            }
            view = {std::span<const psx::render::VertexCandidate>(
                        candidates.data(), event.vertex_count), &state};
            return true;
        } catch (...) { return false; }
    }
    psx::render::ProvenanceState state{};
    std::array<psx::render::VertexCandidate, 4U> candidates{};
};

void test_live_pgxp_projection_pipeline() {
    using namespace clean_aot;
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    runtime::Configuration runtime_config{}; runtime_config.bios = bios;
    runtime::Runtime runtime(runtime_config);
    auto& gpu = runtime.gpu();
    expect(gpu.write_gp1(0x03000000U).accepted &&
           gpu.write_gp1(0x05000000U).accepted &&
           gpu.write_gp1(0x06c00200U).accepted &&
           gpu.write_gp1(0x07040010U).accepted &&
           gpu.write_gp1(0x08000001U).accepted,
           "PGXP live display configures");
    const auto send = [&](std::uint32_t word) {
        expect(gpu.write_gp0(word).accepted, "PGXP live packet accepts");
    };
    send(0xe3000000U); send(0xe4000000U | (511U << 10U) | 1023U);
    send(0xa0000000U); send(64U); send((2U << 16U) | 2U);
    send(0x03e0001fU); send(0x7fff7c00U);
    send(0x25ffffffU);
    send((20U << 16U) | 20U);
    send(0U);
    send((20U << 16U) | 220U);
    send(0x01010001U);
    send((180U << 16U) | 20U);
    send(0x00000100U);

    LiveProvenance provenance;
    app::GraphicsConfiguration graphics_config{};
    graphics_config.runtime_scanout = true;
    graphics_config.output_width = 5120U;
    graphics_config.output_height = 2160U;
    graphics_config.presentation = raster::PresentationMode::Generic21x9;
    graphics_config.custom_aspect_width = 21U;
    graphics_config.custom_aspect_height = 9U;
    graphics_config.pgxp_enabled = true;
    graphics_config.provenance_provider = &provenance;
    graphics_config.projection = psx::render::ProjectionPolicy::ExpandHorizontal;
    graphics_config.culling = psx::render::CullingPolicy::MatchExpandedProjection;
    graphics_config.texture_filter =
        raster::TextureFilterMode::BilinearTransparencySafe;
    app::RuntimeGraphics graphics(graphics_config);
    host::HeadlessBackend host;
    expect(graphics.configured() && graphics.present(runtime, host, 0U).success(),
           "live PGXP ultrawide frame presents");
    std::cout << "PGXP geometry digest: 0x" << std::hex
              << graphics.geometry_digest() << std::dec << '\n';
    expect(graphics.precise_primitives() == 1U &&
           graphics.precise_fallbacks() == 0U &&
           graphics.geometry_digest() == 0x55e1f82e49a637d6ULL,
           "GTE/RAM/GPU provenance drives deterministic projected geometry");
    const auto& filter_stats = graphics.precise_raster_stats();
    expect(filter_stats.bilinear_fragments != 0U &&
           filter_stats.bilinear_texture_fetches <=
               filter_stats.precise_fragments * 4U,
           "live ultrawide filtering remains within four fetches per fragment");
    expect(graphics.visual_receipt().content_width == 5040U &&
           graphics.visual_receipt().content_height == 2160U,
           "projected geometry reaches the 5K ultrawide presentation plan");

    const auto queue_native_frame = [](runtime::Runtime& target) {
        auto& frontend = target.gpu();
        (void)frontend.write_gp1(0x03000000U);
        (void)frontend.write_gp1(0x05000000U);
        (void)frontend.write_gp1(0x06c00200U);
        (void)frontend.write_gp1(0x07040010U);
        (void)frontend.write_gp1(0x08000001U);
        (void)frontend.write_gp0(0xe3000000U);
        (void)frontend.write_gp0(0xe4000000U | (511U << 10U) | 1023U);
        (void)frontend.write_gp0(0x200000ffU);
        (void)frontend.write_gp0((20U << 16U) | 20U);
        (void)frontend.write_gp0((20U << 16U) | 220U);
        (void)frontend.write_gp0((180U << 16U) | 20U);
    };
    runtime::Runtime rejected_runtime(runtime_config);
    runtime::Runtime native_runtime(runtime_config);
    queue_native_frame(rejected_runtime); queue_native_frame(native_runtime);
    RejectProvenance reject;
    app::GraphicsConfiguration fallback_config{};
    fallback_config.runtime_scanout = true;
    fallback_config.pgxp_enabled = true;
    fallback_config.provenance_provider = &reject;
    fallback_config.precise_color =
        raster::PreciseColorMode::TrueColorDisplay;
    fallback_config.maximum_true_color_scanout_pixels = 1U;
    app::RuntimeGraphics fallback(fallback_config);
    app::GraphicsConfiguration native_config{};
    native_config.runtime_scanout = true;
    app::RuntimeGraphics native(native_config);
    host::HeadlessBackend fallback_host, native_host;
    expect(fallback.present(rejected_runtime, fallback_host, 0U).success() &&
           native.present(native_runtime, native_host, 0U).success() &&
           fallback.precise_fallbacks() == 1U &&
           fallback.true_color_fallbacks() == 1U &&
           fallback.visual_digest() == native.visual_digest(),
           "missing provenance and true-color budget preserve exact native frame");
}

void test_live_gp0_primitive_compatibility_matrix() {
    using namespace clean_aot;
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    runtime::Configuration runtime_config{};
    runtime_config.bios = bios;
    runtime::Runtime runtime(runtime_config);
    auto& gpu = runtime.gpu();
    expect(gpu.write_gp1(0x03000000U).accepted &&
           gpu.write_gp1(0x05000000U).accepted &&
           gpu.write_gp1(0x06c00200U).accepted &&
           gpu.write_gp1(0x07040010U).accepted &&
           gpu.write_gp1(0x08000001U).accepted,
           "GP0 compatibility display configures");
    const auto send = [&](std::uint32_t word) {
        expect(gpu.write_gp0(word).accepted,
               "GP0 compatibility matrix packet accepts");
    };
    const auto vertex = [](std::uint16_t x, std::uint16_t y) {
        return static_cast<std::uint32_t>(x) |
            (static_cast<std::uint32_t>(y) << 16U);
    };
    send(0xe3000000U);
    send(0xe4000000U | (239U << 10U) | 319U);
    send(0x02000040U); send(0U); send((240U << 16U) | 320U);
    send(0x02ffffffU); send(64U); send((16U << 16U) | 16U);
    send(0xe1000101U);
    send(0xe2000001U);

    send(0x200000ffU); send(vertex(20U, 20U));
    send(vertex(50U, 20U)); send(vertex(20U, 50U));
    send(0x2a00ff00U); send(vertex(60U, 20U));
    send(vertex(90U, 20U)); send(vertex(60U, 50U)); send(vertex(90U, 50U));
    send(0x300000ffU); send(vertex(100U, 20U));
    send(0x0000ff00U); send(vertex(130U, 20U));
    send(0x00ff0000U); send(vertex(100U, 50U));
    send(0x25ffffffU); send(vertex(140U, 20U)); send(0U);
    send(vertex(170U, 20U)); send(0x01010001U);
    send(vertex(140U, 50U)); send(0x00000100U);
    send(0x3dffffffU); send(vertex(180U, 20U)); send(0U);
    send(0x00ffffffU); send(vertex(210U, 20U)); send(0x01010001U);
    send(0x00ffffffU); send(vertex(180U, 50U)); send(0x00000100U);
    send(0x00ffffffU); send(vertex(210U, 50U)); send(0x00000101U);

    send(0x4000ffffU); send(vertex(20U, 70U)); send(vertex(80U, 70U));
    send(0x500000ffU); send(vertex(20U, 75U));
    send(0x00ff0000U); send(vertex(80U, 75U));
    send(0x4800ffffU); send(vertex(20U, 80U)); send(vertex(50U, 90U));
    send(vertex(80U, 80U)); send(0x50005000U);
    send(0x580000ffU); send(vertex(20U, 95U));
    send(0x0000ff00U); send(vertex(50U, 105U));
    send(0x00ff0000U); send(vertex(80U, 95U)); send(0x50005000U);

    send(0x6000ffffU); send(vertex(100U, 70U)); send((12U << 16U) | 20U);
    send(0x65ffffffU); send(vertex(130U, 70U)); send(0U);
    send((8U << 16U) | 8U);
    send(0x680000ffU); send(vertex(150U, 70U));
    send(0x7000ff00U); send(vertex(160U, 70U));
    send(0x780000ffU); send(vertex(180U, 70U));

    send(0xe5001802U);
    send(0xe6000003U);
    send(0x600000ffU); send(vertex(220U, 80U)); send((8U << 16U) | 8U);
    send(0x6000ff00U); send(vertex(220U, 80U)); send((8U << 16U) | 8U);

    IdentityProvenance provenance;
    app::GraphicsConfiguration configuration{};
    configuration.runtime_scanout = true;
    configuration.pgxp_enabled = true;
    configuration.provenance_provider = &provenance;
    configuration.precise_color = raster::PreciseColorMode::TrueColorDisplay;
    configuration.presentation_resolve =
        raster::PresentationResolveMode::EdgeAware;
    app::RuntimeGraphics graphics(configuration);
    host::HeadlessBackend host;
    expect(graphics.present(runtime, host, 0U).success() &&
           host.service().success() && gpu.event_count() == 0U,
           "complete GP0 primitive matrix reaches live presentation");
    expect(graphics.precise_primitives() == 5U &&
           graphics.precise_fallbacks() == 0U &&
           graphics.precise_raster_stats().precise_fragments != 0U &&
           graphics.precise_raster_stats().true_color_fragments != 0U &&
           graphics.precise_raster_stats().feedback_fallbacks != 0U &&
           graphics.precise_raster_stats().true_color_scanout_tests ==
               320U * 240U &&
           graphics.precise_raster_stats().true_color_scanout_pixels <=
               320U * 240U &&
           graphics.upscale_stats().output_pixels == 2880ULL * 2160ULL &&
           graphics.upscale_stats().source_pixel_reads ==
               graphics.upscale_stats().output_pixels * 5U &&
           graphics.visual_digest() == 0xfd39004a4be2d9d1ULL,
           "all polygon matrix modes reach precise raster without drops");
    std::cout << "GP0 matrix precise/fallback: " << graphics.precise_primitives()
              << '/' << graphics.precise_fallbacks() << '\n';
    std::cout << "GP0 compatibility matrix digest: 0x" << std::hex
              << graphics.visual_digest() << std::dec << '\n';
}

void test_identity_bound_gpu_trace_replay() {
    using namespace clean_aot;
    const app::GpuTraceIdentity identity{
        std::string(64U, 'a'), std::string(64U, 'b'), 17U, 1U};
    gpu::FrontEnd source{};
    const auto event = [&](std::initializer_list<std::uint32_t> words) {
        for (std::uint32_t word : words) {
            expect(source.write_gp0(word).accepted,
                   "synthetic trace GP0 command accepts");
        }
        expect(source.event_count() == 1U,
               "synthetic trace command emits exactly one event");
        return *source.pop_event();
    };
    const gpu::Event draw_area = event({
        0xe4000000U | (239U << 10U) | 319U});
    const gpu::Event fill = event({
        0x02000040U, 0U, (240U << 16U) | 320U});
    const gpu::Event polygon = event({
        0x200000ffU, (20U << 16U) | 20U,
        (20U << 16U) | 80U, (80U << 16U) | 20U});
    const gpu::Event line = event({
        0x4000ffffU, (100U << 16U) | 20U,
        (100U << 16U) | 80U});
    const auto precise = [](double x, double y) {
        return raster::PreciseVertex{
            x, y, 1.0, 1.0, 0.0, 0.0, 0xff0000ffU, 1U, 1U};
    };
    const std::array<raster::PreciseVertex, 3U> vertices{
        precise(20.0, 20.0), precise(80.0, 20.0), precise(20.0, 80.0)};
    app::GpuTraceRecorder recorder(identity);
    expect(recorder.append(draw_area) == app::GpuTraceCode::Success &&
           recorder.append(fill) == app::GpuTraceCode::Success &&
           recorder.append(polygon, vertices) == app::GpuTraceCode::Success &&
           recorder.append(line) == app::GpuTraceCode::Success,
           "synthetic live trace records every command and provenance tuple");
    auto trace = recorder.finish(4U);
    expect(trace.has_value(), "complete trace seals with declared command count");
    const app::GpuReplayReceipt replay = app::GpuTraceReplay::replay(
        *trace, identity, raster::PreciseCullingPolicy::NativeHardware,
        raster::TextureFilterMode::NearestNative,
        raster::PreciseColorMode::TrueColorDisplay);
    expect(replay.success() && replay.commands_replayed == 4U &&
           replay.polygon_commands == 1U && replay.precise_commands == 1U &&
           replay.native_only_commands == 3U &&
           replay.native_vram_digest == replay.enhanced_vram_digest &&
           replay.command_digest == 5872361417715498967ULL &&
           replay.native_vram_digest == 12133522140254745647ULL &&
           replay.native_work_units == 80462U &&
           replay.enhanced_work_units == 80462U &&
           replay.precise_stats.precise_fragments == 1830U,
           "offline trace replay produces native/enhanced digest and coverage evidence");
    const std::string receipt = replay.canonical_json();
    expect(receipt == app::GpuTraceReplay::replay(
               *trace, identity, raster::PreciseCullingPolicy::NativeHardware,
               raster::TextureFilterMode::NearestNative,
               raster::PreciseColorMode::TrueColorDisplay).canonical_json() &&
           receipt.find("clean-aot.gpu-replay-receipt.v1") != std::string::npos &&
           receipt.find("gp0_words") == std::string::npos &&
           receipt.find("precise_vertices") == std::string::npos,
           "trace receipt is deterministic and contains no command/frame payload");

    auto wrong_identity = identity;
    wrong_identity.frame_index = 18U;
    expect(app::GpuTraceReplay::replay(*trace, wrong_identity).code ==
               app::GpuTraceCode::IdentityMismatch,
           "trace replay rejects a mismatched frame identity");
    auto dropped = *trace;
    dropped.commands.pop_back();
    expect(app::GpuTraceReplay::replay(dropped, identity).code ==
               app::GpuTraceCode::DroppedCommand,
           "trace replay rejects a dropped command before rasterization");
    auto stale = *trace;
    stale.identity.initial_vram_generation = 2U;
    auto stale_identity = identity;
    stale_identity.initial_vram_generation = 2U;
    expect(app::GpuTraceReplay::replay(stale, stale_identity).code ==
               app::GpuTraceCode::StaleVramGeneration,
           "trace replay rejects a stale initial VRAM generation");

    app::GpuTraceRecorder unsupported(identity);
    expect(unsupported.append(draw_area) == app::GpuTraceCode::Success &&
           unsupported.append(polygon) == app::GpuTraceCode::Success,
           "native-only polygon trace captures for explicit replay rejection");
    auto unsupported_trace = unsupported.finish(2U);
    expect(unsupported_trace.has_value() &&
           app::GpuTraceReplay::replay(*unsupported_trace, identity).code ==
               app::GpuTraceCode::UnsupportedPrimitive,
           "enhanced replay rejects a polygon lacking provenance");
    auto stale_vertices = vertices;
    stale_vertices[0].storage_generation = 0U;
    app::GpuTraceRecorder stale_provenance(identity);
    expect(stale_provenance.append(polygon, stale_vertices) ==
               app::GpuTraceCode::StaleVramGeneration,
           "trace capture rejects stale provenance generations");
    std::cout << "GPU replay receipt: " << receipt << '\n';
}

void test_precise_raster_perspective_interpolation() {
    using namespace clean_aot;
    raster::ReferenceRasterizer raster;
    gpu::PolygonEvent event{};
    event.vertex_count = 3U;
    event.gouraud = true;
    event.vertices[0] = {0U, 0, 0};
    event.vertices[1] = {0U, 20, 0};
    event.vertices[2] = {0U, 0, 20};
    event.state.draw_area.right = 1023U;
    event.state.draw_area.bottom = 511U;
    const std::array<raster::PreciseVertex, 3U> vertices{
        raster::PreciseVertex{0.0, 0.0, 1.0, 1.0, 0.0, 0.0,
                              0xff0000ffU, 1U, 1U},
        raster::PreciseVertex{20.0, 0.0, 1.0, 2.0, 0.0, 0.0,
                              0xff00ff00U, 2U, 2U},
        raster::PreciseVertex{0.0, 20.0, 1.0, 4.0, 0.0, 0.0,
                              0xffff0000U, 3U, 3U},
    };
    expect(raster.consume_precise_polygon(event, vertices).accepted() &&
           raster.read_vram(5U, 5U) == 0x08b7U,
           "precise raster uses clip-W perspective interpolation per pixel");
    auto invalid = vertices;
    invalid[1].w = -2.0;
    const std::uint64_t digest = raster.digest();
    expect(!raster.consume_precise_polygon(event, invalid).accepted() &&
           raster.digest() == digest,
           "mixed-sign clip-W fails closed without touching native VRAM");
    raster.reset();
    event.vertices[1] = {0U, 1024, 0};
    expect(raster.consume_precise_polygon(event, vertices).status ==
               raster::EventStatus::Ignored &&
           raster.read_vram(5U, 5U) == 0U,
           "native culling policy retains the hardware edge limit");
    expect(raster.consume_precise_polygon(
               event, vertices, nullptr,
               raster::PreciseCullingPolicy::ProjectedBounds).accepted() &&
           raster.read_vram(5U, 5U) == 0x08b7U,
           "expanded culling policy evaluates projected precise bounds");
}

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
void test_live_hd_font_and_banner_compositor() {
    using namespace clean_aot;
    const psx::render::ReplacementKey key{0x8043be1bU, 0xd03041bdU};
    const auto run = [&](app::TitleAssetClass asset_class,
                         psx::render::ReplacementAssetKind kind,
                         raster::PresentationMode presentation,
                         std::uint32_t output_width,
                         std::uint32_t expected_width,
                         const char* label) {
        const auto root = std::filesystem::temp_directory_path() /
            (asset_class == app::TitleAssetClass::Font
                 ? "clean-aot-hd-font" : "clean-aot-hd-banner");
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        const std::array<std::uint8_t, 4U> bytes{7U, 8U, 9U, 10U};
        { std::ofstream file(root / "ui.png", std::ios::binary);
          file.write(reinterpret_cast<const char*>(bytes.data()), 4); }
        const std::array entries{psx::render::ReplacementManifestEntry{
            kind, key, "ui.png", app::detail::sha256(bytes), 64U, 4U}};
        auto manifest = psx::render::ReplacementManifest::create(entries, 9U);
        UiStripeDecoder decoder;
        host::HeadlessBackend host;
        app::ReplacementAssetRuntime assets(
            std::move(*manifest), root, decoder, host);
        const std::array mappings{app::TitleAssetMapping{key, asset_class}};
        app::Wip3outUploadKeyPolicy policy(mappings);
        std::vector<std::uint8_t> bios(512U * 1024U, 0U);
        runtime::Configuration runtime_config{};
        runtime_config.bios = bios;
        runtime::Runtime runtime(runtime_config);
        auto& gpu = runtime.gpu();
        const auto send = [&](std::uint32_t word) {
            expect(gpu.write_gp0(word).accepted, label);
        };
        expect(gpu.write_gp1(0x03000000U).accepted &&
               gpu.write_gp1(0x05000000U).accepted &&
               gpu.write_gp1(0x06c00200U).accepted &&
               gpu.write_gp1(0x07040010U).accepted &&
               gpu.write_gp1(0x08000001U).accepted, label);
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
        app::GraphicsConfiguration configuration{};
        configuration.runtime_scanout = true;
        configuration.output_width = output_width;
        configuration.output_height = 2160U;
        configuration.presentation = presentation;
        configuration.custom_aspect_width = presentation ==
                raster::PresentationMode::Generic21x9 ? 21U : 4U;
        configuration.custom_aspect_height = presentation ==
                raster::PresentationMode::Generic21x9 ? 9U : 3U;
        configuration.replacement_assets = &assets;
        configuration.replacement_policy = &policy;
        configuration.presentation_resolve =
            raster::PresentationResolveMode::EdgeAware;
        configuration.high_resolution_ui = true;
        configuration.maximum_ui_pixels_per_frame = 4096U;
        app::RuntimeGraphics graphics(configuration);
        expect(graphics.present(runtime, host, 0U).success() &&
               host.service().success(), label);
        const auto& stats = graphics.ui_stats();
        expect(graphics.resolved_width() == expected_width &&
               graphics.resolved_height() == 2160U &&
               stats.candidates == 1U && stats.composited == 1U &&
               stats.native_fallbacks == 0U &&
               stats.pixels_tested <= 4096U &&
               stats.texture_fetches == stats.pixels_tested &&
               stats.pixels_written == stats.pixels_tested, label);
        const auto pixels = graphics.resolved_pixels();
        const std::uint32_t scale = expected_width / 320U;
        const std::size_t white =
            (static_cast<std::size_t>(10U * 9U + 4U) * expected_width) * 3U;
        const std::size_t black = white + ((scale + 3U) / 4U) * 3U;
        expect(!pixels.empty() && pixels[white] == 255U &&
               pixels[white + 1U] == 255U && pixels[white + 2U] == 255U &&
               pixels[black] == 0U && pixels[black + 1U] == 0U &&
               pixels[black + 2U] == 0U, label);
        expect(stats.texture_fetches <= 4096U, label);
        std::cout << label << " digest: 0x" << std::hex
                  << graphics.visual_digest() << std::dec << '\n';
        send(0x65000000U); send(10U << 16U); send(1U << 16U);
        send((1U << 16U) | 16U);
        send(0x02000000U); send(0U); send((1U << 16U) | 4U);
        expect(graphics.present(runtime, host, 1U).success() &&
               host.service().success() &&
               graphics.ui_stats().candidates == 2U &&
               graphics.ui_stats().composited == 1U &&
               graphics.ui_stats().native_fallbacks == 1U &&
               assets.stats().invalidated == 1U &&
               host.stats().replacements_released == 1U,
               "same-frame producer mutation blocks stale HD UI sampling");
        std::filesystem::remove_all(root);
    };
    run(app::TitleAssetClass::Font,
        psx::render::ReplacementAssetKind::Font,
        raster::PresentationMode::Identity4x3, 3840U, 2880U,
        "4K HD font remains legible within fixed sampling budget");
    run(app::TitleAssetClass::Banner,
        psx::render::ReplacementAssetKind::Texture,
        raster::PresentationMode::Generic21x9, 5120U, 5040U,
        "ultrawide HD banner remains legible within fixed sampling budget");
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
void test_4k_edge_aware_runtime_resolve() {
    const auto prepare = [](clean_aot::runtime::Runtime& runtime) {
        auto& gpu = runtime.gpu();
        (void)gpu.write_gp1(0x03000000U);
        (void)gpu.write_gp1(0x05000000U);
        (void)gpu.write_gp1(0x06c00200U);
        (void)gpu.write_gp1(0x07040010U);
        (void)gpu.write_gp1(0x08000001U);
        (void)gpu.write_gp0(0x02ff00ffU);
        (void)gpu.write_gp0(0U);
        (void)gpu.write_gp0((240U << 16U) | 320U);
    };
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    clean_aot::runtime::Configuration runtime_config{};
    runtime_config.bios = bios;
    clean_aot::runtime::Runtime enhanced_runtime(runtime_config);
    prepare(enhanced_runtime);
    clean_aot::app::GraphicsConfiguration enhanced_config{};
    enhanced_config.runtime_scanout = true;
    enhanced_config.presentation_resolve =
        clean_aot::raster::PresentationResolveMode::EdgeAware;
    clean_aot::app::RuntimeGraphics enhanced(enhanced_config);
    clean_aot::host::HeadlessBackend enhanced_host;
    expect(enhanced.present(enhanced_runtime, enhanced_host, 0U).success(),
           "4K edge-aware runtime resolve presents");
    const auto& stats = enhanced.upscale_stats();
    constexpr std::uint64_t output_pixels = 2880ULL * 2160ULL;
    expect(stats.source_pixels == 320U * 240U &&
           stats.output_pixels == output_pixels &&
           stats.source_pixel_reads == output_pixels * 5U &&
           stats.color_comparisons <= output_pixels * 3U &&
           stats.bytes_written == output_pixels * 3U &&
           enhanced.upscale_fallbacks() == 0U,
           "4K resolve obeys fixed memory/comparison budgets");
    std::cout << "4K edge resolve digest: 0x" << std::hex
              << enhanced.visual_digest() << std::dec << '\n';
    expect(enhanced.visual_digest() == 0x552f7b477c3b979cULL,
           "4K edge-aware visual digest changed");

    clean_aot::runtime::Runtime fallback_runtime(runtime_config);
    prepare(fallback_runtime);
    auto fallback_config = enhanced_config;
    fallback_config.maximum_resolved_pixels = 1U;
    clean_aot::app::RuntimeGraphics fallback(fallback_config);
    clean_aot::host::HeadlessBackend fallback_host;
    expect(fallback.present(fallback_runtime, fallback_host, 0U).success() &&
           fallback.upscale_fallbacks() == 1U &&
           fallback.visual_digest() == 0xa0c4b5214903b3ddULL,
           "bounded allocation policy falls back to exact native scanout");
}
void test_live_24bit_chroma_postprocess() {
    using namespace clean_aot;
    const auto prepare = [](runtime::Runtime& runtime) {
        auto& gpu = runtime.gpu();
        (void)gpu.write_gp1(0x03000000U);
        (void)gpu.write_gp1(0x05000000U);
        (void)gpu.write_gp1(0x06c00200U);
        (void)gpu.write_gp1(0x07040010U);
        (void)gpu.write_gp1(0x08000011U);
        (void)gpu.write_gp0(0xa0000000U);
        (void)gpu.write_gp0(0U);
        (void)gpu.write_gp0((1U << 16U) | 3U);
        (void)gpu.write_gp0(0x8c646464U);
        (void)gpu.write_gp0(0x00006450U);
    };
    std::vector<std::uint8_t> bios(512U * 1024U, 0U);
    runtime::Configuration runtime_config{};
    runtime_config.bios = bios;
    const auto run = [&](std::uint32_t output_width,
                         raster::PresentationMode presentation,
                         std::uint32_t expected_width,
                         std::uint64_t expected_digest,
                         const char* label) {
        runtime::Runtime runtime(runtime_config);
        prepare(runtime);
        app::GraphicsConfiguration configuration{};
        configuration.runtime_scanout = true;
        configuration.output_width = output_width;
        configuration.output_height = 2160U;
        configuration.presentation = presentation;
        configuration.presentation_resolve =
            raster::PresentationResolveMode::EdgeAware;
        configuration.display_postprocess =
            raster::DisplayPostprocessMode::ChromaSmooth24Bit;
        app::RuntimeGraphics graphics(configuration);
        host::HeadlessBackend host;
        expect(graphics.present(runtime, host, 0U).success() &&
               host.service().success(), label);
        const auto& stats = graphics.display_postprocess_stats();
        const std::uint64_t pixels =
            static_cast<std::uint64_t>(expected_width) * 2160U;
        expect(graphics.resolved_width() == expected_width &&
               graphics.resolved_height() == 2160U &&
               stats.pixels == pixels &&
               stats.source_pixel_reads == pixels * 3U &&
               stats.luma_comparisons == pixels * 2U &&
               stats.bytes_written == pixels * 3U &&
               stats.chroma_filtered_pixels <= pixels &&
               graphics.display_postprocess_fallbacks() == 0U &&
               graphics.visual_digest() == expected_digest,
               label);
        std::cout << label << " digest: 0x" << std::hex
                  << graphics.visual_digest() << std::dec << '\n';
    };
    run(3840U, raster::PresentationMode::Identity4x3, 2880U,
        0x1a8fedaa6afeafebULL,
        "4K packed-24 chroma postprocess stays within fixed work bounds");
    run(5120U, raster::PresentationMode::Generic21x9, 5040U,
        0xe6959cf9bfad2d73ULL,
        "ultrawide packed-24 chroma postprocess stays within fixed work bounds");

    runtime::Runtime fallback_runtime(runtime_config);
    runtime::Runtime control_runtime(runtime_config);
    prepare(fallback_runtime);
    prepare(control_runtime);
    app::GraphicsConfiguration fallback_configuration{};
    fallback_configuration.runtime_scanout = true;
    fallback_configuration.output_width = 640U;
    fallback_configuration.output_height = 480U;
    fallback_configuration.presentation_resolve =
        raster::PresentationResolveMode::EdgeAware;
    fallback_configuration.display_postprocess =
        raster::DisplayPostprocessMode::ChromaSmooth24Bit;
    fallback_configuration.maximum_postprocess_pixels = 1U;
    auto control_configuration = fallback_configuration;
    control_configuration.display_postprocess =
        raster::DisplayPostprocessMode::None;
    app::RuntimeGraphics fallback(fallback_configuration);
    app::RuntimeGraphics control(control_configuration);
    host::HeadlessBackend fallback_host;
    host::HeadlessBackend control_host;
    expect(fallback.present(fallback_runtime, fallback_host, 0U).success() &&
           control.present(control_runtime, control_host, 0U).success() &&
           fallback.display_postprocess_fallbacks() == 1U &&
           fallback.visual_digest() == control.visual_digest(),
           "postprocess budget failure preserves exact resolved native frame");
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
    test_live_pgxp_projection_pipeline();
    test_live_gp0_primitive_compatibility_matrix();
    test_identity_bound_gpu_trace_replay();
    test_precise_raster_perspective_interpolation();
    test_beetle_upload_key_derivation_and_sampling();
    test_live_replacement_pipeline();
    test_live_hd_font_and_banner_compositor();
    test_runtime_scanout(); test_4k_edge_aware_runtime_resolve();
    test_live_24bit_chroma_postprocess();
    test_replacement_runtime(); test_native_png_decode();
    test_title_manifest_and_receipts(); test_missing_asset_native_fallback();
    test_ultrawide_visual_receipt();
    test_permitted_pack_when_available();
    if (failures != 0) return 1;
    std::cout << "clean_aot_app_graphics_tests: all checks passed\n"; return 0;
}
