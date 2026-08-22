#pragma once

#include "clean_aot/host/host.hpp"
#include "clean_aot/host/render_interfaces.hpp"
#include "clean_aot/raster/raster.hpp"
#include "clean_aot/runtime/runtime.hpp"
#include "psx_render/replacement_manifest.h"

#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace clean_aot::app {

class ReplacementAssetRuntime;

enum class TitleAssetClass : std::uint8_t {
    Texture,
    Font,
    Banner,
};

struct ReplacementMatch final {
    psx::render::ReplacementAssetKind kind =
        psx::render::ReplacementAssetKind::Texture;
    TitleAssetClass asset_class = TitleAssetClass::Texture;
    psx::render::ReplacementKey key{};
    raster::VramRect source{};
    gpu::TextureDepth depth = gpu::TextureDepth::Indexed4;
    std::uint64_t source_generation = 0U;
};

class IReplacementKeyPolicy {
public:
    virtual ~IReplacementKeyPolicy() = default;
    // Title policy owns source-key derivation. Generic graphics supplies only
    // the immutable packet and current VRAM generation.
    [[nodiscard]] virtual bool identify(
        const gpu::Event& event,
        std::span<const raster::VramWord> vram,
        std::uint64_t source_generation,
        ReplacementMatch& match) noexcept = 0;
    virtual void observe(const gpu::Event& event,
                         std::span<const raster::VramWord> vram,
                         std::uint64_t source_generation) noexcept = 0;
    virtual void observe_precise_draw(raster::VramRect destination) noexcept = 0;
    [[nodiscard]] virtual std::uint64_t invalidation_serial() const noexcept = 0;
};

struct PolygonProvenanceView final {
    std::span<const psx::render::VertexCandidate> candidates{};
    const psx::render::ProvenanceState* state = nullptr;
};

class IPolygonProvenanceProvider {
public:
    virtual ~IPolygonProvenanceProvider() = default;
    [[nodiscard]] virtual bool resolve(
        const gpu::PolygonEvent& event,
        PolygonProvenanceView& view) noexcept = 0;
};

enum class FontManifestPolicy : std::uint8_t {
    Disabled,
    ManifestOnly,
};

struct TitleAssetMapping final {
    psx::render::ReplacementKey key{};
    TitleAssetClass asset_class = TitleAssetClass::Texture;
};

struct LoadedTitleManifest final {
    psx::render::ReplacementManifest manifest{};
    std::vector<TitleAssetMapping> mappings{};
    std::string source_commit{};

    [[nodiscard]] std::optional<TitleAssetClass> classify(
        psx::render::ReplacementKey key) const noexcept;
};

class Wip3outUploadKeyPolicy final : public IReplacementKeyPolicy {
public:
    explicit Wip3outUploadKeyPolicy(
        std::span<const TitleAssetMapping> mappings);
    ~Wip3outUploadKeyPolicy() override;
    [[nodiscard]] bool identify(
        const gpu::Event& event,
        std::span<const raster::VramWord> vram,
        std::uint64_t source_generation,
        ReplacementMatch& match) noexcept override;
    void observe(const gpu::Event& event,
                 std::span<const raster::VramWord> vram,
                 std::uint64_t source_generation) noexcept override;
    void observe_precise_draw(raster::VramRect destination) noexcept override;

    struct Stats final {
        std::uint64_t uploads_hashed = 0U;
        std::uint64_t upload_bytes_hashed = 0U;
        std::uint64_t palette_hashes = 0U;
        std::uint64_t palette_cache_hits = 0U;
        std::uint64_t palette_bytes_hashed = 0U;
        std::uint64_t draw_queries = 0U;
        std::uint64_t identified = 0U;
        std::uint64_t ambiguous_fallbacks = 0U;
        std::uint64_t invalidated_uploads = 0U;
    };
    [[nodiscard]] const Stats& stats() const noexcept;
    [[nodiscard]] std::uint64_t invalidation_serial() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::optional<LoadedTitleManifest> load_wip3out_hd_manifest(
    const std::filesystem::path& path,
    std::uint64_t manifest_generation,
    std::string& error) noexcept;

struct GraphicsConfiguration final {
    bool runtime_scanout = false;
    std::uint32_t output_width = 3840U;
    std::uint32_t output_height = 2160U;
    raster::PresentationMode presentation = raster::PresentationMode::Identity4x3;
    std::uint32_t custom_aspect_width = 4U;
    std::uint32_t custom_aspect_height = 3U;
    FontManifestPolicy font_policy = FontManifestPolicy::ManifestOnly;
    ReplacementAssetRuntime* replacement_assets = nullptr;
    IReplacementKeyPolicy* replacement_policy = nullptr;
    bool pgxp_enabled = false;
    IPolygonProvenanceProvider* provenance_provider = nullptr;
    psx::render::ProjectionPolicy projection =
        psx::render::ProjectionPolicy::PreserveNative4x3;
    psx::render::CullingPolicy culling =
        psx::render::CullingPolicy::NativeHardware;
    raster::TextureFilterMode texture_filter =
        raster::TextureFilterMode::NearestNative;
    raster::PresentationResolveMode presentation_resolve =
        raster::PresentationResolveMode::Native;
    std::uint64_t maximum_resolved_pixels = 5120ULL * 2160ULL;
    bool high_resolution_ui = true;
    std::uint64_t maximum_ui_pixels_per_frame = 5120ULL * 2160ULL;
    raster::DisplayPostprocessMode display_postprocess =
        raster::DisplayPostprocessMode::None;
    std::uint64_t maximum_postprocess_pixels = 5120ULL * 2160ULL;
};

class IPngDecoder {
public:
    virtual ~IPngDecoder() = default;
    [[nodiscard]] virtual bool decode(
        std::span<const std::uint8_t> png,
        std::uint32_t expected_width,
        std::uint32_t expected_height,
        std::vector<std::uint8_t>& rgba8) noexcept = 0;
};

// The shipping Windows implementation uses WIC and always emits tightly
// packed RGBA8. Other platforms fail closed until they provide a native
// decoder with the same contract.
class NativePngDecoder final : public IPngDecoder {
public:
    [[nodiscard]] bool decode(
        std::span<const std::uint8_t> png,
        std::uint32_t expected_width,
        std::uint32_t expected_height,
        std::vector<std::uint8_t>& rgba8) noexcept override;
};

class ReplacementAssetRuntime final {
public:
    ReplacementAssetRuntime(psx::render::ReplacementManifest manifest,
                            std::filesystem::path root,
                            IPngDecoder& decoder,
                            host::IReplacementUploader& uploader,
                            FontManifestPolicy font_policy =
                                FontManifestPolicy::ManifestOnly) noexcept;
    ~ReplacementAssetRuntime();

    [[nodiscard]] host::HostStatus resolve_and_upload(
        psx::render::ReplacementAssetKind kind,
        psx::render::ReplacementKey key,
        std::uint64_t source_generation,
        std::uint64_t& handle) noexcept;

    struct Stats final {
        std::uint64_t queries = 0U;
        std::uint64_t cache_hits = 0U;
        std::uint64_t missing = 0U;
        std::uint64_t decoded = 0U;
        std::uint64_t uploaded = 0U;
        std::uint64_t invalidated = 0U;
        std::uint64_t png_bytes = 0U;
        std::uint64_t rgba_bytes = 0U;
    };
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] host::HostStatus invalidate_all() noexcept;
    [[nodiscard]] bool sampling_texture(
        std::uint64_t handle, const ReplacementMatch& match,
        raster::ReplacementTexture& texture) const noexcept;

private:
    psx::render::ReplacementManifest manifest_;
    std::filesystem::path root_;
    IPngDecoder& decoder_;
    host::IReplacementUploader& uploader_;
    FontManifestPolicy font_policy_ = FontManifestPolicy::ManifestOnly;
    struct CachedAsset final {
        psx::render::ReplacementIdentity identity{};
        std::uint64_t handle = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<std::uint8_t> rgba8{};
    };
    std::vector<CachedAsset> cache_{};
    Stats stats_{};
};

struct VisualReceipt final {
    std::uint32_t output_width = 0U;
    std::uint32_t output_height = 0U;
    std::uint32_t content_x = 0U;
    std::uint32_t content_y = 0U;
    std::uint32_t content_width = 0U;
    std::uint32_t content_height = 0U;
    bool identity_4x3 = true;
    std::uint64_t visual_digest = 0U;
    std::uint64_t vram_generation = 0U;
    std::uint64_t replacement_hits = 0U;
    std::uint64_t native_fallbacks = 0U;
    [[nodiscard]] std::string canonical_json() const;
};

class RuntimeGraphics final {
public:
    explicit RuntimeGraphics(GraphicsConfiguration configuration);
    [[nodiscard]] bool configured() const noexcept;
    [[nodiscard]] host::HostStatus present(runtime::Runtime& runtime,
                                           host::IHostBackend& host,
                                           std::uint64_t update_index) noexcept;
    [[nodiscard]] std::uint64_t visual_digest() const noexcept;
    [[nodiscard]] std::uint64_t vram_generation() const noexcept;
    [[nodiscard]] VisualReceipt visual_receipt() const noexcept;
    [[nodiscard]] std::uint64_t precise_primitives() const noexcept;
    [[nodiscard]] std::uint64_t precise_fallbacks() const noexcept;
    [[nodiscard]] std::uint64_t geometry_digest() const noexcept;
    [[nodiscard]] const raster::PreciseRasterStats& precise_raster_stats()
        const noexcept;
    [[nodiscard]] const raster::UpscaleStats& upscale_stats() const noexcept;
    [[nodiscard]] std::uint64_t upscale_fallbacks() const noexcept;
    struct UiStats final {
        std::uint64_t candidates = 0U;
        std::uint64_t composited = 0U;
        std::uint64_t native_fallbacks = 0U;
        std::uint64_t pixels_tested = 0U;
        std::uint64_t pixels_written = 0U;
        std::uint64_t texture_fetches = 0U;
    };
    [[nodiscard]] const UiStats& ui_stats() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> resolved_pixels() const noexcept;
    [[nodiscard]] std::uint32_t resolved_width() const noexcept;
    [[nodiscard]] std::uint32_t resolved_height() const noexcept;
    [[nodiscard]] const raster::DisplayPostprocessStats&
        display_postprocess_stats() const noexcept;
    [[nodiscard]] std::uint64_t display_postprocess_fallbacks() const noexcept;

private:
    GraphicsConfiguration configuration_{};
    raster::ReferenceRasterizer raster_{};
    std::vector<std::uint8_t> scanout_{};
    std::vector<std::uint8_t> resolved_scanout_{};
    std::vector<std::uint8_t> postprocessed_scanout_{};
    std::uint64_t sequence_ = 0U;
    std::uint64_t visual_digest_ = 0U;
    bool configured_ = false;
    raster::ScanoutPlan last_plan_{};
    std::uint64_t replacement_hits_ = 0U;
    std::uint64_t native_fallbacks_ = 0U;
    std::uint64_t policy_invalidation_serial_ = 0U;
    psx::render::ProjectionPlan projection_plan_{};
    std::uint64_t precise_primitives_ = 0U;
    std::uint64_t precise_fallbacks_ = 0U;
    std::uint64_t geometry_digest_ = 0U;
    raster::UpscaleStats upscale_stats_{};
    std::uint64_t upscale_fallbacks_ = 0U;
    struct UiDraw final {
        gpu::RectangleEvent event{};
        ReplacementMatch match{};
        std::uint64_t handle = 0U;
    };
    std::vector<UiDraw> ui_draws_{};
    UiStats ui_stats_{};
    std::uint32_t resolved_width_ = 0U;
    std::uint32_t resolved_height_ = 0U;
    raster::DisplayPostprocessStats display_postprocess_stats_{};
    std::uint64_t display_postprocess_fallbacks_ = 0U;
    bool using_postprocessed_scanout_ = false;
};

} // namespace clean_aot::app
