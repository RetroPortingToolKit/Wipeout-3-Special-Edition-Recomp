#pragma once

#include "clean_aot/host/host.hpp"
#include "clean_aot/raster/raster.hpp"
#include "clean_aot/runtime/runtime.hpp"
#include "psx_render/replacement_manifest.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace clean_aot::app {

class ReplacementAssetRuntime;

class IReplacementKeyPolicy {
public:
    virtual ~IReplacementKeyPolicy() = default;
    // Title policy owns source-key derivation. Generic graphics supplies only
    // the immutable packet and current VRAM generation.
    [[nodiscard]] virtual bool identify(
        const gpu::Event& event,
        std::uint64_t source_generation,
        psx::render::ReplacementAssetKind& kind,
        psx::render::ReplacementKey& key) noexcept = 0;
};

enum class FontManifestPolicy : std::uint8_t {
    Disabled,
    ManifestOnly,
};

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
};

struct DecodedReplacement final {
    psx::render::ReplacementIdentity identity{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8{};
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

class IReplacementUploader {
public:
    virtual ~IReplacementUploader() = default;
    [[nodiscard]] virtual host::HostStatus upload(
        const DecodedReplacement& replacement,
        std::uint64_t& handle) noexcept = 0;
};

class ReplacementAssetRuntime final {
public:
    ReplacementAssetRuntime(psx::render::ReplacementManifest manifest,
                            std::filesystem::path root,
                            IPngDecoder& decoder,
                            IReplacementUploader& uploader,
                            FontManifestPolicy font_policy =
                                FontManifestPolicy::ManifestOnly) noexcept;

    [[nodiscard]] host::HostStatus resolve_and_upload(
        psx::render::ReplacementAssetKind kind,
        psx::render::ReplacementKey key,
        std::uint64_t source_generation,
        std::uint64_t& handle) noexcept;

private:
    psx::render::ReplacementManifest manifest_;
    std::filesystem::path root_;
    IPngDecoder& decoder_;
    IReplacementUploader& uploader_;
    FontManifestPolicy font_policy_ = FontManifestPolicy::ManifestOnly;
    struct CachedAsset final {
        psx::render::ReplacementIdentity identity{};
        std::uint64_t handle = 0U;
    };
    std::vector<CachedAsset> cache_{};
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

private:
    GraphicsConfiguration configuration_{};
    raster::ReferenceRasterizer raster_{};
    std::vector<std::uint8_t> scanout_{};
    std::uint64_t sequence_ = 0U;
    std::uint64_t visual_digest_ = 0U;
    bool configured_ = false;
};

} // namespace clean_aot::app
