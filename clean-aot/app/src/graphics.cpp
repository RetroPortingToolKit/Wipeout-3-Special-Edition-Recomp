#include "clean_aot/app/graphics.hpp"

#include "clean_aot/host/render_interfaces.hpp"
#include "json.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <variant>

#if defined(_WIN32)
#include <windows.h>
#include <wincodec.h>
#endif

namespace clean_aot::app {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kMaximumPngBytes = 64U * 1024U * 1024U;

void digest_bytes(std::uint64_t& digest,
                  std::span<const std::uint8_t> bytes) noexcept {
    for (const std::uint8_t value : bytes) {
        digest ^= value;
        digest *= kFnvPrime;
    }
}

[[nodiscard]] bool path_within(const std::filesystem::path& root,
                               const std::filesystem::path& child) {
    auto root_it = root.begin();
    auto child_it = child.begin();
    for (; root_it != root.end(); ++root_it, ++child_it) {
        if (child_it == child.end() || *root_it != *child_it) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool starts_with(std::string_view value,
                               std::string_view prefix) noexcept {
    return value.size() >= prefix.size() &&
        value.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] TitleAssetClass classify_path(std::string_view path) noexcept {
    if (starts_with(path, "Font/")) return TitleAssetClass::Font;
    if (starts_with(path, "Main Menu/Title Screen/") ||
        starts_with(path, "Main Menu/Screen Text/") ||
        starts_with(path, "Main Menu/Bar/")) {
        return TitleAssetClass::Banner;
    }
    return TitleAssetClass::Texture;
}

[[nodiscard]] std::optional<psx::render::ReplacementKey> parse_pack_key(
    std::string_view value) noexcept {
    const std::size_t dash = value.find('-');
    if (dash == std::string_view::npos || dash == 0U || dash > 8U ||
        value.size() <= dash + 1U || value.size() - dash - 1U > 8U) {
        return std::nullopt;
    }
    const auto hexadecimal = [](char digit) noexcept {
        return (digit >= '0' && digit <= '9') ||
               (digit >= 'a' && digit <= 'f');
    };
    if (!std::all_of(value.begin(), value.begin() +
                     static_cast<std::ptrdiff_t>(dash), hexadecimal) ||
        !std::all_of(value.begin() + static_cast<std::ptrdiff_t>(dash + 1U),
                     value.end(), hexadecimal)) {
        return std::nullopt;
    }
    std::string canonical(8U - dash, '0');
    canonical.append(value.substr(0U, dash));
    canonical.push_back('-');
    canonical.append(8U - (value.size() - dash - 1U), '0');
    canonical.append(value.substr(dash + 1U));
    return psx::render::parse_replacement_key(canonical);
}

} // namespace

std::optional<TitleAssetClass> LoadedTitleManifest::classify(
    psx::render::ReplacementKey key) const noexcept {
    const auto found = std::lower_bound(mappings.begin(), mappings.end(), key,
        [](const TitleAssetMapping& item, psx::render::ReplacementKey sought) {
            return item.key < sought;
        });
    if (found == mappings.end() || found->key != key) return std::nullopt;
    return found->asset_class;
}

std::optional<LoadedTitleManifest> load_wip3out_hd_manifest(
    const std::filesystem::path& path, std::uint64_t manifest_generation,
    std::string& error) noexcept {
    error.clear();
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "unable to open HD manifest"; return std::nullopt; }
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        detail::Json root;
        if (!detail::parse_json(text, root, error)) return std::nullopt;
        const detail::Json* schema = detail::member(root, "schema");
        const detail::Json* textures = detail::member(root, "textures");
        const detail::Json* source = detail::member(root, "source");
        if (schema == nullptr || schema->string() == nullptr ||
            *schema->string() != "wo3se.external-texture-pack.v1" ||
            textures == nullptr || textures->array() == nullptr ||
            source == nullptr || source->object() == nullptr) {
            error = "unsupported HD manifest schema"; return std::nullopt;
        }
        const detail::Json* commit = detail::member(*source, "commit");
        if (commit == nullptr || commit->string() == nullptr ||
            commit->string()->size() != 40U) {
            error = "missing source commit"; return std::nullopt;
        }
        std::vector<psx::render::ReplacementManifestEntry> entries;
        std::vector<TitleAssetMapping> mappings;
        entries.reserve(textures->array()->size());
        mappings.reserve(textures->array()->size());
        for (const detail::Json& item : *textures->array()) {
            const detail::Json* key_json = detail::member(item, "key");
            const detail::Json* file_json = detail::member(item, "file");
            const detail::Json* logical_json = detail::member(item, "logical_path");
            const detail::Json* sha_json = detail::member(item, "sha256");
            const detail::Json* png_json = detail::member(item, "png");
            if (key_json == nullptr || key_json->string() == nullptr ||
                file_json == nullptr || file_json->string() == nullptr ||
                logical_json == nullptr || logical_json->string() == nullptr ||
                sha_json == nullptr || sha_json->string() == nullptr ||
                png_json == nullptr) {
                error = "malformed HD texture entry"; return std::nullopt;
            }
            const detail::Json* width_json = detail::member(*png_json, "width");
            const detail::Json* height_json = detail::member(*png_json, "height");
            const auto key = parse_pack_key(*key_json->string());
            if (!key.has_value() || width_json == nullptr ||
                width_json->number() == nullptr || height_json == nullptr ||
                height_json->number() == nullptr || *width_json->number() == 0U ||
                *height_json->number() == 0U ||
                *width_json->number() > UINT32_MAX ||
                *height_json->number() > UINT32_MAX) {
                error = "invalid HD texture identity"; return std::nullopt;
            }
            const TitleAssetClass asset_class = classify_path(*logical_json->string());
            const auto kind = asset_class == TitleAssetClass::Font
                ? psx::render::ReplacementAssetKind::Font
                : psx::render::ReplacementAssetKind::Texture;
            entries.push_back({kind, *key, *file_json->string(),
                *sha_json->string(), static_cast<std::uint32_t>(*width_json->number()),
                static_cast<std::uint32_t>(*height_json->number())});
            mappings.push_back({*key, asset_class});
        }
        auto manifest = psx::render::ReplacementManifest::create(
            entries, manifest_generation);
        if (!manifest.has_value()) { error = "invalid HD manifest"; return std::nullopt; }
        std::sort(mappings.begin(), mappings.end(),
            [](const TitleAssetMapping& left, const TitleAssetMapping& right) {
                return left.key < right.key;
            });
        return LoadedTitleManifest{std::move(*manifest), std::move(mappings),
                                   *commit->string()};
    } catch (...) {
        error = "HD manifest allocation or I/O failure";
        return std::nullopt;
    }
}

bool NativePngDecoder::decode(std::span<const std::uint8_t> png,
                              std::uint32_t expected_width,
                              std::uint32_t expected_height,
                              std::vector<std::uint8_t>& rgba8) noexcept {
#if defined(_WIN32)
    if (png.empty() || png.size() > static_cast<std::size_t>(MAXDWORD) ||
        expected_width == 0U || expected_height == 0U) {
        return false;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        return false;
    }
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool success = false;
    try {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
        if (SUCCEEDED(hr)) hr = stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(png.data())),
            static_cast<DWORD>(png.size()));
        if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(
            stream, &GUID_ContainerFormatPng, WICDecodeMetadataCacheOnLoad,
            &decoder);
        if (SUCCEEDED(hr)) hr = decoder->GetFrame(0U, &frame);
        UINT width = 0U;
        UINT height = 0U;
        if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
        if (SUCCEEDED(hr) &&
            (width != expected_width || height != expected_height)) {
            hr = E_INVALIDARG;
        }
        if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr)) hr = converter->Initialize(
            frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom);
        const std::uint64_t byte_count =
            static_cast<std::uint64_t>(width) * height * 4U;
        if (SUCCEEDED(hr) && byte_count <= std::numeric_limits<UINT>::max()) {
            rgba8.assign(static_cast<std::size_t>(byte_count), 0U);
            hr = converter->CopyPixels(nullptr, width * 4U,
                                       static_cast<UINT>(byte_count),
                                       rgba8.data());
        } else if (SUCCEEDED(hr)) {
            hr = E_INVALIDARG;
        }
        success = SUCCEEDED(hr);
    } catch (...) {
        success = false;
    }
    if (converter != nullptr) converter->Release();
    if (frame != nullptr) frame->Release();
    if (decoder != nullptr) decoder->Release();
    if (stream != nullptr) stream->Release();
    if (factory != nullptr) factory->Release();
    if (uninitialize) CoUninitialize();
    if (!success) rgba8.clear();
    return success;
#else
    (void)png;
    (void)expected_width;
    (void)expected_height;
    rgba8.clear();
    return false;
#endif
}

ReplacementAssetRuntime::ReplacementAssetRuntime(
    psx::render::ReplacementManifest manifest,
    std::filesystem::path root,
    IPngDecoder& decoder,
    host::IReplacementUploader& uploader,
    FontManifestPolicy font_policy) noexcept
    : manifest_(std::move(manifest)), root_(std::move(root)),
      decoder_(decoder), uploader_(uploader), font_policy_(font_policy) {}

ReplacementAssetRuntime::~ReplacementAssetRuntime() { (void)invalidate_all(); }

host::HostStatus ReplacementAssetRuntime::invalidate_all() noexcept {
    for (const CachedAsset& item : cache_) {
        const host::HostStatus released = uploader_.release_replacement(item.handle);
        if (!released.success()) return released;
        ++stats_.invalidated;
    }
    cache_.clear();
    return {};
}

host::HostStatus ReplacementAssetRuntime::resolve_and_upload(
    psx::render::ReplacementAssetKind kind,
    psx::render::ReplacementKey key,
    std::uint64_t source_generation,
    std::uint64_t& handle) noexcept {
    handle = 0U;
    ++stats_.queries;
    if (kind == psx::render::ReplacementAssetKind::Font &&
        font_policy_ == FontManifestPolicy::Disabled) {
        return {host::HostCode::unsupported};
    }
    const auto identity = manifest_.resolve(kind, key, source_generation);
    if (!identity.has_value()) { ++stats_.missing; return {host::HostCode::not_ready}; }
    const auto cached = std::find_if(cache_.begin(), cache_.end(),
        [&](const CachedAsset& item) {
            return item.identity.kind == kind && item.identity.key == key;
        });
    if (cached != cache_.end() &&
        manifest_.is_current(cached->identity, source_generation)) {
        handle = cached->handle;
        ++stats_.cache_hits;
        return {};
    }
    const auto* entry = manifest_.entry(*identity, source_generation);
    if (entry == nullptr) return {host::HostCode::not_ready};
    try {
        const auto canonical_root = std::filesystem::weakly_canonical(root_);
        const auto path = std::filesystem::weakly_canonical(
            canonical_root / entry->logical_path);
        if (!path_within(canonical_root, path)) {
            return {host::HostCode::invalid_argument};
        }
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return {host::HostCode::not_ready};
        const std::streamoff length = input.tellg();
        if (length <= 0 || static_cast<std::uint64_t>(length) > kMaximumPngBytes) {
            return {host::HostCode::invalid_argument};
        }
        std::vector<std::uint8_t> png(static_cast<std::size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(png.data()), length);
        if (!input || detail::sha256(png) != entry->sha256) {
            return {host::HostCode::invalid_argument};
        }
        std::vector<std::uint8_t> rgba8;
        if (!decoder_.decode(png, entry->width, entry->height, rgba8) ||
            rgba8.size() !=
                static_cast<std::size_t>(entry->width) * entry->height * 4U) {
            return {host::HostCode::invalid_argument};
        }
        ++stats_.decoded;
        stats_.png_bytes += png.size();
        stats_.rgba_bytes += rgba8.size();
        const host::ReplacementImage decoded{
            *identity, entry->width, entry->height, rgba8};
        std::uint64_t uploaded = 0U;
        const host::HostStatus status =
            uploader_.upload_replacement(decoded, uploaded);
        if (!status.success() || uploaded == 0U) return status.success()
            ? host::HostStatus{host::HostCode::internal_error} : status;
        if (cached != cache_.end()) {
            const host::HostStatus released =
                uploader_.release_replacement(cached->handle);
            if (!released.success()) {
                (void)uploader_.release_replacement(uploaded);
                return released;
            }
            ++stats_.invalidated;
        }
        if (cached == cache_.end()) cache_.push_back(CachedAsset{*identity, uploaded});
        else *cached = CachedAsset{*identity, uploaded};
        ++stats_.uploaded;
        handle = uploaded;
        return {};
    } catch (...) {
        return {host::HostCode::internal_error};
    }
}

RuntimeGraphics::RuntimeGraphics(GraphicsConfiguration configuration)
    : configuration_(configuration) {
    configured_ = configuration_.output_width != 0U &&
        configuration_.output_height != 0U &&
        configuration_.custom_aspect_width != 0U &&
        configuration_.custom_aspect_height != 0U;
}

bool RuntimeGraphics::configured() const noexcept { return configured_; }

host::HostStatus RuntimeGraphics::present(runtime::Runtime& runtime,
                                          host::IHostBackend& host,
                                          std::uint64_t update_index) noexcept {
    if (!configuration_.runtime_scanout) return {host::HostCode::not_ready};
    try {
        std::size_t drained = 0U;
        while (auto event = runtime.gpu().pop_event()) {
            if (++drained > gpu::kEventQueueCapacity) {
                return {host::HostCode::internal_error};
            }
            if (configuration_.replacement_assets != nullptr &&
                configuration_.replacement_policy != nullptr) {
                psx::render::ReplacementAssetKind kind =
                    psx::render::ReplacementAssetKind::Texture;
                psx::render::ReplacementKey key{};
                if (configuration_.replacement_policy->identify(
                        *event, raster_.vram_generation(), kind, key)) {
                    std::uint64_t handle = 0U;
                    const host::HostStatus replacement =
                        configuration_.replacement_assets->resolve_and_upload(
                            kind, key, raster_.vram_generation(), handle);
                    if (!replacement.success() &&
                        replacement.code != host::HostCode::not_ready &&
                        replacement.code != host::HostCode::unsupported) {
                        return replacement;
                    }
                    if (replacement.success()) ++replacement_hits_;
                    else ++native_fallbacks_;
                }
            }
            if (!raster_.consume(*event).accepted()) {
                return {host::HostCode::unsupported};
            }
        }
        const gpu::DisplayTiming& display = runtime.gpu().control().display;
        const raster::ScanoutField field = !display.interlaced
            ? raster::ScanoutField::Progressive
            : (display.odd_field ? raster::ScanoutField::Odd
                                 : raster::ScanoutField::Even);
        const raster::ScanoutResult planned = raster::ScanoutMapper::plan({
            display, field, configuration_.output_width,
            configuration_.output_height, configuration_.presentation,
            configuration_.custom_aspect_width,
            configuration_.custom_aspect_height});
        if (!planned.accepted) return {host::HostCode::invalid_argument};
        last_plan_ = planned.plan;
        scanout_.assign(planned.plan.required_rgb_bytes, 0U);
        const raster::EventResult decoded = raster::ScanoutMapper::decode_rgb888(
            planned.plan, raster_.vram_words(), scanout_);
        if (!decoded.accepted()) return {host::HostCode::internal_error};
        host::FrameSubmission submission{};
        const host::HostStatus bound = host::bind_scanout_frame(
            planned.plan, scanout_, sequence_, update_index, submission);
        if (!bound.success()) return bound;
        visual_digest_ = kFnvOffset;
        digest_bytes(visual_digest_, scanout_);
        const std::array<std::uint64_t, 4U> metadata{
            raster_.vram_generation(), planned.plan.logical_width,
            planned.plan.logical_height,
            planned.plan.presentation.identity_4x3 ? 1U : 0U};
        digest_bytes(visual_digest_, std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(metadata.data()),
            sizeof(metadata)));
        const host::HostStatus submitted = host.submit_frame(submission);
        if (submitted.success()) ++sequence_;
        return submitted;
    } catch (...) {
        return {host::HostCode::internal_error};
    }
}

std::uint64_t RuntimeGraphics::visual_digest() const noexcept {
    return visual_digest_;
}

std::uint64_t RuntimeGraphics::vram_generation() const noexcept {
    return raster_.vram_generation();
}

VisualReceipt RuntimeGraphics::visual_receipt() const noexcept {
    return {configuration_.output_width, configuration_.output_height,
        last_plan_.presentation.destination_x,
        last_plan_.presentation.destination_y,
        last_plan_.presentation.destination_width,
        last_plan_.presentation.destination_height,
        last_plan_.presentation.identity_4x3, visual_digest_,
        raster_.vram_generation(), replacement_hits_, native_fallbacks_};
}

std::string VisualReceipt::canonical_json() const {
    return "{\"content\":{\"height\":" + std::to_string(content_height) +
        ",\"width\":" + std::to_string(content_width) +
        ",\"x\":" + std::to_string(content_x) + ",\"y\":" +
        std::to_string(content_y) + "},\"identity_4x3\":" +
        (identity_4x3 ? "true" : "false") + ",\"native_fallbacks\":" +
        std::to_string(native_fallbacks) + ",\"output\":{\"height\":" +
        std::to_string(output_height) + ",\"width\":" +
        std::to_string(output_width) + "},\"replacement_hits\":" +
        std::to_string(replacement_hits) + ",\"schema\":\"clean-aot.visual-receipt.v1\","
        "\"visual_digest\":" + std::to_string(visual_digest) +
        ",\"vram_generation\":" + std::to_string(vram_generation) + "}";
}

} // namespace clean_aot::app
