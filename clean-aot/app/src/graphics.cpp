#include "clean_aot/app/graphics.hpp"

#include "clean_aot/host/render_interfaces.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <fstream>
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

} // namespace

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
    IReplacementUploader& uploader,
    FontManifestPolicy font_policy) noexcept
    : manifest_(std::move(manifest)), root_(std::move(root)),
      decoder_(decoder), uploader_(uploader), font_policy_(font_policy) {}

host::HostStatus ReplacementAssetRuntime::resolve_and_upload(
    psx::render::ReplacementAssetKind kind,
    psx::render::ReplacementKey key,
    std::uint64_t source_generation,
    std::uint64_t& handle) noexcept {
    handle = 0U;
    if (kind == psx::render::ReplacementAssetKind::Font &&
        font_policy_ == FontManifestPolicy::Disabled) {
        return {host::HostCode::unsupported};
    }
    const auto identity = manifest_.resolve(kind, key, source_generation);
    if (!identity.has_value()) return {host::HostCode::not_ready};
    const auto cached = std::find_if(cache_.begin(), cache_.end(),
        [&](const CachedAsset& item) {
            return item.identity.kind == kind && item.identity.key == key;
        });
    if (cached != cache_.end() &&
        manifest_.is_current(cached->identity, source_generation)) {
        handle = cached->handle;
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
        DecodedReplacement decoded{*identity, entry->width, entry->height, {}};
        if (!decoder_.decode(png, entry->width, entry->height, decoded.rgba8) ||
            decoded.rgba8.size() !=
                static_cast<std::size_t>(entry->width) * entry->height * 4U) {
            return {host::HostCode::invalid_argument};
        }
        std::uint64_t uploaded = 0U;
        const host::HostStatus status = uploader_.upload(decoded, uploaded);
        if (!status.success() || uploaded == 0U) return status.success()
            ? host::HostStatus{host::HostCode::internal_error} : status;
        if (cached == cache_.end()) cache_.push_back(CachedAsset{*identity, uploaded});
        else *cached = CachedAsset{*identity, uploaded};
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

} // namespace clean_aot::app
