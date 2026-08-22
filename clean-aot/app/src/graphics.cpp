#include "clean_aot/app/graphics.hpp"

#include "clean_aot/host/render_interfaces.hpp"
#include "json.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
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
constexpr std::size_t kMaximumTraceCommands = gpu::kEventQueueCapacity;

void digest_bytes(std::uint64_t& digest,
                  std::span<const std::uint8_t> bytes) noexcept;

[[nodiscard]] bool trace_sha256(std::string_view value) noexcept {
    return value.size() == 64U && std::all_of(
        value.begin(), value.end(), [](char byte) noexcept {
            return (byte >= '0' && byte <= '9') ||
                   (byte >= 'a' && byte <= 'f');
        });
}

[[nodiscard]] std::vector<std::uint32_t> trace_words(
    const gpu::Event& event) {
    return std::visit([](const auto& item) -> std::vector<std::uint32_t> {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, gpu::PolygonEvent> ||
                      std::is_same_v<Item, gpu::RectangleEvent>) {
            if (item.raw_word_count == 0U ||
                item.raw_word_count > item.raw_words.size()) return {};
            return {item.raw_words.begin(),
                    item.raw_words.begin() + item.raw_word_count};
        } else if constexpr (std::is_same_v<Item, gpu::LineEvent>) {
            return item.raw_words;
        } else if constexpr (std::is_same_v<Item, gpu::VramUploadEvent>) {
            std::vector<std::uint32_t> words(
                item.raw_words.begin(), item.raw_words.end());
            words.insert(words.end(), item.data_words.begin(), item.data_words.end());
            return words;
        } else if constexpr (std::is_same_v<Item, gpu::VramDownloadEvent>) {
            return {};
        } else if constexpr (requires { item.raw_words; }) {
            return {item.raw_words.begin(), item.raw_words.end()};
        } else {
            return {item.raw_word};
        }
    }, event);
}

void trace_mix_u64(std::uint64_t& digest, std::uint64_t value) noexcept {
    const std::array<std::uint64_t, 1U> values{value};
    digest_bytes(digest, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(values.data()), sizeof(value)));
}

[[nodiscard]] std::uint64_t trace_digest(
    const GpuCommandTrace& trace) noexcept {
    std::uint64_t digest = kFnvOffset;
    trace_mix_u64(digest, trace.declared_command_count);
    for (const GpuTraceCommand& command : trace.commands) {
        trace_mix_u64(digest, command.sequence);
        trace_mix_u64(digest, command.before_vram_generation);
        trace_mix_u64(digest, command.after_vram_generation);
        trace_mix_u64(digest, command.gp0_words.size());
        for (std::uint32_t word : command.gp0_words) trace_mix_u64(digest, word);
        trace_mix_u64(digest, command.precise_vertices.size());
        for (const raster::PreciseVertex& vertex : command.precise_vertices) {
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.x));
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.y));
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.z));
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.w));
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.u));
            trace_mix_u64(digest, std::bit_cast<std::uint64_t>(vertex.v));
            trace_mix_u64(digest, vertex.color_rgba);
            trace_mix_u64(digest, vertex.storage_generation);
            trace_mix_u64(digest, vertex.origin_generation);
        }
    }
    return digest;
}

[[nodiscard]] std::optional<gpu::Event> trace_decode(
    gpu::FrontEnd& frontend, std::span<const std::uint32_t> words) noexcept {
    if (words.empty() || frontend.event_count() != 0U) return std::nullopt;
    for (std::uint32_t word : words) {
        if (!frontend.write_gp0(word).accepted) return std::nullopt;
    }
    if (frontend.event_count() != 1U) return std::nullopt;
    std::optional<gpu::Event> event = frontend.pop_event();
    if (!event.has_value() || frontend.event_count() != 0U) return std::nullopt;
    return event;
}

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

[[nodiscard]] std::uint32_t crc32_words(
    std::span<const raster::VramWord> words, raster::VramRect rect) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::uint32_t y = 0U; y < rect.height; ++y) {
        for (std::uint32_t x = 0U; x < rect.width; ++x) {
            const std::uint32_t vx = (static_cast<std::uint32_t>(rect.x) + x) %
                raster::kVramWidth;
            const std::uint32_t vy = (static_cast<std::uint32_t>(rect.y) + y) %
                raster::kVramHeight;
            const std::uint16_t word = words[static_cast<std::size_t>(vy) *
                raster::kVramWidth + vx];
            for (const std::uint8_t byte : {
                     static_cast<std::uint8_t>(word & 0xffU),
                     static_cast<std::uint8_t>(word >> 8U)}) {
                crc ^= byte;
                for (unsigned bit = 0U; bit < 8U; ++bit) {
                    crc = (crc >> 1U) ^
                        (0xedb88320U & (0U - (crc & 1U)));
                }
            }
        }
    }
    return crc ^ 0xffffffffU;
}

[[nodiscard]] bool intersects(raster::VramRect left,
                              raster::VramRect right) noexcept {
    return static_cast<std::uint32_t>(left.x) + left.width > right.x &&
        static_cast<std::uint32_t>(right.x) + right.width > left.x &&
        static_cast<std::uint32_t>(left.y) + left.height > right.y &&
        static_cast<std::uint32_t>(right.y) + right.height > left.y;
}

[[nodiscard]] bool contains(raster::VramRect outer,
                            raster::VramRect inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
        static_cast<std::uint32_t>(inner.x) + inner.width <=
            static_cast<std::uint32_t>(outer.x) + outer.width &&
        static_cast<std::uint32_t>(inner.y) + inner.height <=
            static_cast<std::uint32_t>(outer.y) + outer.height;
}

[[nodiscard]] bool wraps_vram(raster::VramRect rect) noexcept {
    return static_cast<std::uint32_t>(rect.x) + rect.width > raster::kVramWidth ||
        static_cast<std::uint32_t>(rect.y) + rect.height > raster::kVramHeight;
}

[[nodiscard]] std::optional<raster::VramRect> draw_destination(
    const gpu::Event& event) noexcept {
    const gpu::RenderStateSnapshot* state = nullptr;
    std::int32_t left = 0, top = 0, right = -1, bottom = -1;
    if (const auto* polygon = std::get_if<gpu::PolygonEvent>(&event)) {
        if (polygon->vertex_count == 0U) return std::nullopt;
        state = &polygon->state;
        left = right = polygon->vertices[0].x;
        top = bottom = polygon->vertices[0].y;
        for (std::size_t i = 1U; i < polygon->vertex_count; ++i) {
            left = std::min(left, static_cast<std::int32_t>(polygon->vertices[i].x));
            right = std::max(right, static_cast<std::int32_t>(polygon->vertices[i].x));
            top = std::min(top, static_cast<std::int32_t>(polygon->vertices[i].y));
            bottom = std::max(bottom, static_cast<std::int32_t>(polygon->vertices[i].y));
        }
    } else if (const auto* rectangle = std::get_if<gpu::RectangleEvent>(&event)) {
        if (rectangle->width == 0U || rectangle->height == 0U) return std::nullopt;
        state = &rectangle->state;
        left = rectangle->origin.x; top = rectangle->origin.y;
        right = left + rectangle->width - 1; bottom = top + rectangle->height - 1;
    } else if (const auto* line = std::get_if<gpu::LineEvent>(&event)) {
        if (line->vertices.empty()) return std::nullopt;
        state = &line->state;
        left = right = line->vertices[0].x;
        top = bottom = line->vertices[0].y;
        for (std::size_t i = 1U; i < line->vertices.size(); ++i) {
            left = std::min(left, static_cast<std::int32_t>(line->vertices[i].x));
            right = std::max(right, static_cast<std::int32_t>(line->vertices[i].x));
            top = std::min(top, static_cast<std::int32_t>(line->vertices[i].y));
            bottom = std::max(bottom, static_cast<std::int32_t>(line->vertices[i].y));
        }
    } else {
        return std::nullopt;
    }
    left += state->draw_offset.x; right += state->draw_offset.x;
    top += state->draw_offset.y; bottom += state->draw_offset.y;
    left = std::max(left, static_cast<std::int32_t>(state->draw_area.left));
    top = std::max(top, static_cast<std::int32_t>(state->draw_area.top));
    right = std::min(right, static_cast<std::int32_t>(state->draw_area.right));
    bottom = std::min(bottom, static_cast<std::int32_t>(state->draw_area.bottom));
    left = std::max(left, 0); top = std::max(top, 0);
    right = std::min(right, static_cast<std::int32_t>(raster::kVramWidth - 1U));
    bottom = std::min(bottom, static_cast<std::int32_t>(raster::kVramHeight - 1U));
    if (right < left || bottom < top) return std::nullopt;
    return raster::VramRect{static_cast<std::uint16_t>(left),
        static_cast<std::uint16_t>(top),
        static_cast<std::uint16_t>(right - left + 1),
        static_cast<std::uint16_t>(bottom - top + 1)};
}

} // namespace

struct Wip3outUploadKeyPolicy::Impl final {
    struct Upload final {
        raster::VramRect rect{};
        std::uint32_t hash = 0U;
        std::uint64_t generation = 0U;
    };
    std::vector<TitleAssetMapping> mappings{};
    std::vector<Upload> uploads{};
    struct PaletteHash final {
        raster::VramRect rect{};
        std::uint32_t hash = 0U;
    };
    std::vector<PaletteHash> palettes{};
    Stats stats{};
    std::uint64_t invalidation_serial = 0U;

    [[nodiscard]] std::optional<TitleAssetClass> classify(
        psx::render::ReplacementKey key) const noexcept {
        const auto found = std::lower_bound(mappings.begin(), mappings.end(), key,
            [](const TitleAssetMapping& item, psx::render::ReplacementKey sought) {
                return item.key < sought;
            });
        if (found == mappings.end() || found->key != key) return std::nullopt;
        return found->asset_class;
    }

    void invalidate(raster::VramRect rect) noexcept {
        const auto first = std::remove_if(uploads.begin(), uploads.end(),
            [&](const Upload& upload) { return intersects(upload.rect, rect); });
        const std::uint64_t removed =
            static_cast<std::uint64_t>(uploads.end() - first);
        stats.invalidated_uploads += removed;
        if (removed != 0U && invalidation_serial !=
                std::numeric_limits<std::uint64_t>::max()) {
            ++invalidation_serial;
        }
        uploads.erase(first, uploads.end());
        const auto palette_first = std::remove_if(
            palettes.begin(), palettes.end(),
            [&](const PaletteHash& palette) {
                return intersects(palette.rect, rect);
            });
        palettes.erase(palette_first, palettes.end());
    }


    void invalidate_all() noexcept {
        const std::uint64_t removed = static_cast<std::uint64_t>(uploads.size());
        stats.invalidated_uploads += removed;
        if (removed != 0U && invalidation_serial !=
                std::numeric_limits<std::uint64_t>::max()) {
            ++invalidation_serial;
        }
        uploads.clear();
        palettes.clear();
    }

    [[nodiscard]] std::uint32_t palette_hash(
        raster::VramRect rect,
        std::span<const raster::VramWord> vram) {
        const auto found = std::find_if(palettes.begin(), palettes.end(),
            [&](const PaletteHash& item) {
                return item.rect.x == rect.x && item.rect.y == rect.y &&
                    item.rect.width == rect.width && item.rect.height == rect.height;
            });
        if (found != palettes.end()) {
            ++stats.palette_cache_hits;
            return found->hash;
        }
        const std::uint32_t hash = crc32_words(vram, rect);
        try {
            palettes.push_back({rect, hash});
        } catch (...) {
            return hash;
        }
        ++stats.palette_hashes;
        stats.palette_bytes_hashed +=
            static_cast<std::uint64_t>(rect.width) * rect.height * 2U;
        return hash;
    }
};

Wip3outUploadKeyPolicy::Wip3outUploadKeyPolicy(
    std::span<const TitleAssetMapping> mappings)
    : impl_(std::make_unique<Impl>()) {
    impl_->mappings.assign(mappings.begin(), mappings.end());
    std::sort(impl_->mappings.begin(), impl_->mappings.end(),
        [](const TitleAssetMapping& left, const TitleAssetMapping& right) {
            return left.key < right.key;
        });
}

Wip3outUploadKeyPolicy::~Wip3outUploadKeyPolicy() = default;

const Wip3outUploadKeyPolicy::Stats& Wip3outUploadKeyPolicy::stats() const noexcept {
    return impl_->stats;
}

std::uint64_t Wip3outUploadKeyPolicy::invalidation_serial() const noexcept {
    return impl_->invalidation_serial;
}

void Wip3outUploadKeyPolicy::observe(
    const gpu::Event& event, std::span<const raster::VramWord> vram,
    std::uint64_t source_generation) noexcept {
    try {
        if (const auto* upload = std::get_if<gpu::VramUploadEvent>(&event)) {
            const raster::VramRect rect{upload->destination.x,
                upload->destination.y, upload->extent.width, upload->extent.height};
            if (wraps_vram(rect)) {
                impl_->invalidate_all();
                return;
            }
            impl_->invalidate(rect);
            impl_->uploads.push_back({rect, crc32_words(vram, rect), source_generation});
            ++impl_->stats.uploads_hashed;
            impl_->stats.upload_bytes_hashed +=
                static_cast<std::uint64_t>(rect.width) * rect.height * 2U;
        } else if (const auto* fill = std::get_if<gpu::VramFillEvent>(&event)) {
            const raster::VramRect rect{fill->origin.x, fill->origin.y,
                                        fill->extent.width, fill->extent.height};
            if (wraps_vram(rect)) impl_->invalidate_all();
            else impl_->invalidate(rect);
        } else if (const auto* copy = std::get_if<gpu::VramCopyEvent>(&event)) {
            const raster::VramRect rect{copy->destination.x, copy->destination.y,
                                        copy->extent.width, copy->extent.height};
            if (wraps_vram(rect)) impl_->invalidate_all();
            else impl_->invalidate(rect);
        } else if (const auto destination = draw_destination(event);
                   destination.has_value()) {
            impl_->invalidate(*destination);
        }
    } catch (...) {
        impl_->invalidate_all();
    }
}

void Wip3outUploadKeyPolicy::observe_precise_draw(
    raster::VramRect destination) noexcept {
    impl_->invalidate(destination);
}

bool Wip3outUploadKeyPolicy::identify(
    const gpu::Event& event, std::span<const raster::VramWord> vram,
    std::uint64_t source_generation, ReplacementMatch& match) noexcept {
    (void)source_generation;
    ++impl_->stats.draw_queries;
    const gpu::RenderStateSnapshot* state = nullptr;
    const gpu::ClutAttribute* clut = nullptr;
    std::uint32_t min_u = 0U, max_u = 0U, min_v = 0U, max_v = 0U;
    if (const auto* polygon = std::get_if<gpu::PolygonEvent>(&event)) {
        if (!polygon->textured || polygon->state.draw_mode.texture_disable ||
            polygon->vertex_count == 0U) return false;
        state = &polygon->state;
        if (polygon->clut.has_value()) clut = &*polygon->clut;
        min_u = max_u = polygon->texture_coordinates[0].u;
        min_v = max_v = polygon->texture_coordinates[0].v;
        for (std::size_t i = 1U; i < polygon->vertex_count; ++i) {
            min_u = std::min(min_u, static_cast<std::uint32_t>(polygon->texture_coordinates[i].u));
            max_u = std::max(max_u, static_cast<std::uint32_t>(polygon->texture_coordinates[i].u));
            min_v = std::min(min_v, static_cast<std::uint32_t>(polygon->texture_coordinates[i].v));
            max_v = std::max(max_v, static_cast<std::uint32_t>(polygon->texture_coordinates[i].v));
        }
    } else if (const auto* rectangle = std::get_if<gpu::RectangleEvent>(&event)) {
        if (!rectangle->textured || rectangle->state.draw_mode.texture_disable ||
            !rectangle->texture_coordinate.has_value() || rectangle->width == 0U ||
            rectangle->height == 0U || rectangle->state.draw_mode.rectangle_flip_x ||
            rectangle->state.draw_mode.rectangle_flip_y) return false;
        state = &rectangle->state;
        if (rectangle->clut.has_value()) clut = &*rectangle->clut;
        min_u = rectangle->texture_coordinate->u;
        min_v = rectangle->texture_coordinate->v;
        max_u = min_u + rectangle->width - 1U;
        max_v = min_v + rectangle->height - 1U;
        if (max_u > 255U || max_v > 255U) return false;
    } else {
        return false;
    }
    if (state->texture_window.mask_x != 0U || state->texture_window.mask_y != 0U ||
        state->texture_window.offset_x != 0U || state->texture_window.offset_y != 0U) {
        ++impl_->stats.ambiguous_fallbacks; return false;
    }
    const std::uint32_t pixels_per_word =
        state->draw_mode.depth == gpu::TextureDepth::Indexed4 ? 4U :
        state->draw_mode.depth == gpu::TextureDepth::Indexed8 ? 2U : 1U;
    const std::uint32_t page_x = static_cast<std::uint32_t>(state->draw_mode.x_base) * 64U;
    const std::uint32_t page_y = state->draw_mode.y_base;
    const raster::VramRect used{
        static_cast<std::uint16_t>(page_x + min_u / pixels_per_word),
        static_cast<std::uint16_t>(page_y + min_v),
        static_cast<std::uint16_t>(max_u / pixels_per_word - min_u / pixels_per_word + 1U),
        static_cast<std::uint16_t>(max_v - min_v + 1U)};
    const Impl::Upload* selected = nullptr;
    for (const Impl::Upload& upload : impl_->uploads) {
        if (!contains(upload.rect, used)) continue;
        if (selected != nullptr && selected->hash != upload.hash) {
            ++impl_->stats.ambiguous_fallbacks; return false;
        }
        selected = &upload;
    }
    if (selected == nullptr) return false;
    std::uint32_t palette_hash = 0U;
    if (state->draw_mode.depth != gpu::TextureDepth::Direct15) {
        if (clut == nullptr) return false;
        const std::uint16_t entries = state->draw_mode.depth ==
            gpu::TextureDepth::Indexed8 ? 256U : 16U;
        palette_hash = impl_->palette_hash(
            {clut->x, clut->y, entries, 1U}, vram);
    }
    const psx::render::ReplacementKey key{selected->hash, palette_hash};
    const auto asset_class = impl_->classify(key);
    if (!asset_class.has_value()) return false;
    match = {asset_class == TitleAssetClass::Font
            ? psx::render::ReplacementAssetKind::Font
            : psx::render::ReplacementAssetKind::Texture,
        *asset_class, key, selected->rect, state->draw_mode.depth,
        selected->generation};
    ++impl_->stats.identified;
    return true;
}

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
        CachedAsset resident{*identity, uploaded, entry->width, entry->height,
                             std::move(rgba8)};
        if (cached == cache_.end()) cache_.push_back(std::move(resident));
        else *cached = std::move(resident);
        ++stats_.uploaded;
        handle = uploaded;
        return {};
    } catch (...) {
        return {host::HostCode::internal_error};
    }
}

bool ReplacementAssetRuntime::sampling_texture(
    std::uint64_t handle, const ReplacementMatch& match,
    raster::ReplacementTexture& texture) const noexcept {
    const auto found = std::find_if(cache_.begin(), cache_.end(),
        [handle](const CachedAsset& item) { return item.handle == handle; });
    if (handle == 0U || found == cache_.end() ||
        !manifest_.is_current(found->identity, match.source_generation)) {
        return false;
    }
    texture = {handle, match.source, match.depth, found->width, found->height,
               found->rgba8};
    return true;
}

RuntimeGraphics::RuntimeGraphics(GraphicsConfiguration configuration)
    : configuration_(configuration) {
    configured_ = configuration_.output_width != 0U &&
        configuration_.output_height != 0U &&
        configuration_.custom_aspect_width != 0U &&
        configuration_.custom_aspect_height != 0U &&
        (configuration_.presentation_resolve ==
             raster::PresentationResolveMode::Native ||
         configuration_.presentation_resolve ==
             raster::PresentationResolveMode::EdgeAware) &&
        (configuration_.presentation_resolve ==
             raster::PresentationResolveMode::Native ||
         configuration_.maximum_resolved_pixels != 0U) &&
        (!configuration_.high_resolution_ui ||
         configuration_.maximum_ui_pixels_per_frame != 0U) &&
        (configuration_.display_postprocess ==
             raster::DisplayPostprocessMode::None ||
         configuration_.display_postprocess ==
             raster::DisplayPostprocessMode::ChromaSmooth24Bit) &&
        (configuration_.display_postprocess ==
             raster::DisplayPostprocessMode::None ||
         configuration_.maximum_postprocess_pixels != 0U) &&
        (configuration_.precise_color == raster::PreciseColorMode::NativeRgb555 ||
         configuration_.precise_color ==
             raster::PreciseColorMode::TrueColorDisplay) &&
        (configuration_.precise_color == raster::PreciseColorMode::NativeRgb555 ||
         configuration_.maximum_true_color_scanout_pixels != 0U);
    const psx::render::ProjectionResult projection =
        psx::render::ProjectionPolicyPlanner::plan({
            configuration_.projection, configuration_.culling,
            configuration_.custom_aspect_width,
            configuration_.custom_aspect_height});
    if (!projection.accepted ||
        (configuration_.projection == psx::render::ProjectionPolicy::ExpandHorizontal &&
         !configuration_.pgxp_enabled)) {
        configured_ = false;
    } else {
        projection_plan_ = projection.plan;
    }
    positive_w_clipping_active_ =
        configuration_.pgxp_enabled &&
        configuration_.pgxp_near_plane ==
            PgxpNearPlanePolicy::ClipWhenGtePositiveWVerified &&
        configuration_.provenance_provider != nullptr &&
        configuration_.provenance_provider->projection_contract()
            .supports_positive_w_clipping();
    if (configuration_.replacement_policy != nullptr) {
        policy_invalidation_serial_ =
            configuration_.replacement_policy->invalidation_serial();
    }
}

bool RuntimeGraphics::configured() const noexcept { return configured_; }

host::HostStatus RuntimeGraphics::present(runtime::Runtime& runtime,
                                          host::IHostBackend& host,
                                          std::uint64_t update_index) noexcept {
    if (!configuration_.runtime_scanout) return {host::HostCode::not_ready};
    try {
        ui_draws_.clear();
        resolved_width_ = 0U;
        resolved_height_ = 0U;
        using_postprocessed_scanout_ = false;
        std::size_t drained = 0U;
        while (auto event = runtime.gpu().pop_event()) {
            if (++drained > gpu::kEventQueueCapacity) {
                return {host::HostCode::internal_error};
            }
            raster::ReplacementTexture sampling{};
            bool use_replacement = false;
            if (configuration_.replacement_assets != nullptr &&
                configuration_.replacement_policy != nullptr) {
                ReplacementMatch match{};
                if (configuration_.replacement_policy->identify(
                        *event, raster_.vram_words(),
                        raster_.vram_generation(), match)) {
                    std::uint64_t handle = 0U;
                    const host::HostStatus replacement =
                        configuration_.replacement_assets->resolve_and_upload(
                            match.kind, match.key, match.source_generation, handle);
                    if (!replacement.success() &&
                        replacement.code != host::HostCode::not_ready &&
                        replacement.code != host::HostCode::unsupported) {
                        return replacement;
                    }
                    if (replacement.success() &&
                        configuration_.replacement_assets->sampling_texture(
                            handle, match, sampling)) {
                        ++replacement_hits_;
                        use_replacement = true;
                        if (configuration_.high_resolution_ui &&
                            match.asset_class != TitleAssetClass::Texture) {
                            ++ui_stats_.candidates;
                            const auto* rectangle =
                                std::get_if<gpu::RectangleEvent>(&*event);
                            if (rectangle != nullptr && rectangle->textured &&
                                rectangle->texture_coordinate.has_value() &&
                                !rectangle->semi_transparent &&
                                !rectangle->state.mask.check_mask_bit &&
                                !rectangle->state.mask.set_mask_bit &&
                                !rectangle->state.draw_mode.rectangle_flip_x &&
                                !rectangle->state.draw_mode.rectangle_flip_y &&
                                !rectangle->state.draw_mode.texture_disable) {
                                try {
                                    ui_draws_.push_back({*rectangle, match, handle});
                                } catch (...) {
                                    ++ui_stats_.native_fallbacks;
                                }
                            } else {
                                ++ui_stats_.native_fallbacks;
                            }
                        }
                    }
                    else ++native_fallbacks_;
                }
            }
            bool consumed_precise = false;
            std::optional<raster::VramRect> precise_destination{};
            if (configuration_.pgxp_enabled &&
                configuration_.provenance_provider != nullptr) {
                if (const auto* polygon = std::get_if<gpu::PolygonEvent>(&*event)) {
                    PolygonProvenanceView provenance{};
                    if (configuration_.provenance_provider->resolve(
                            *polygon, provenance)) {
                        std::array<host::RenderVertex, 6U> host_vertices{};
                        std::array<std::uint16_t, 6U> indices{};
                        const host::GeometryBridgeResult geometry =
                            host::build_gpu_polygon_geometry(
                                *polygon, provenance.candidates, provenance.state,
                                projection_plan_, host_vertices, indices);
                        if (geometry.status.success()) {
                            std::array<raster::PreciseVertex, 6U> precise{};
                            for (std::uint32_t i = 0U;
                                 i < geometry.geometry.vertex_count; ++i) {
                                const host::RenderVertex& source = host_vertices[i];
                                precise[i] = {source.x + polygon->state.draw_offset.x,
                                    source.y + polygon->state.draw_offset.y,
                                    source.z, source.w, source.u, source.v,
                                    source.color_rgba, source.storage_generation,
                                    source.origin_generation};
                            }
                            const auto precise_span = std::span<const raster::PreciseVertex>(
                                precise.data(), geometry.geometry.vertex_count);
                            const raster::EventResult rendered =
                                raster_.consume_precise_polygon(
                                    *polygon, precise_span,
                                    use_replacement ? &sampling : nullptr,
                                    projection_plan_.culling ==
                                            psx::render::CullingPolicy::NativeHardware
                                        ? raster::PreciseCullingPolicy::NativeHardware
                                            : raster::PreciseCullingPolicy::ProjectedBounds,
                                    configuration_.texture_filter,
                                    configuration_.precise_color,
                                    positive_w_clipping_active_
                                        ? raster::PreciseNearPlanePolicy::ClipPositiveW
                                        : raster::PreciseNearPlanePolicy::NativeFallback);
                            if (rendered.accepted()) {
                                consumed_precise = true;
                                ++precise_primitives_;
                                geometry_digest_ ^= geometry.visual_digest +
                                    0x9e3779b97f4a7c15ULL +
                                    (geometry_digest_ << 6U) + (geometry_digest_ >> 2U);
                                double left = precise[0].x, right = precise[0].x;
                                double top = precise[0].y, bottom = precise[0].y;
                                for (const raster::PreciseVertex& vertex : precise_span) {
                                    left = std::min(left, vertex.x);
                                    right = std::max(right, vertex.x);
                                    top = std::min(top, vertex.y);
                                    bottom = std::max(bottom, vertex.y);
                                }
                                const std::int32_t x0 = std::clamp(
                                    static_cast<std::int32_t>(std::floor(left)), 0, 1023);
                                const std::int32_t y0 = std::clamp(
                                    static_cast<std::int32_t>(std::floor(top)), 0, 511);
                                const std::int32_t x1 = std::clamp(
                                    static_cast<std::int32_t>(std::ceil(right)), 0, 1024);
                                const std::int32_t y1 = std::clamp(
                                    static_cast<std::int32_t>(std::ceil(bottom)), 0, 512);
                                if (x1 > x0 && y1 > y0) {
                                    precise_destination = raster::VramRect{
                                        static_cast<std::uint16_t>(x0),
                                        static_cast<std::uint16_t>(y0),
                                        static_cast<std::uint16_t>(x1 - x0),
                                        static_cast<std::uint16_t>(y1 - y0)};
                                }
                            }
                        }
                    }
                    if (!consumed_precise) ++precise_fallbacks_;
                }
            }
            if (!consumed_precise && !raster_.consume(*event,
                    use_replacement ? &sampling : nullptr).accepted()) {
                return {host::HostCode::unsupported};
            }
            if (configuration_.replacement_policy != nullptr) {
                configuration_.replacement_policy->observe(
                    *event, raster_.vram_words(), raster_.vram_generation());
                if (precise_destination.has_value()) {
                    configuration_.replacement_policy->observe_precise_draw(
                        *precise_destination);
                }
                const std::uint64_t invalidation =
                    configuration_.replacement_policy->invalidation_serial();
                if (invalidation != policy_invalidation_serial_) {
                    policy_invalidation_serial_ = invalidation;
                    if (configuration_.replacement_assets != nullptr) {
                        const host::HostStatus retired =
                            configuration_.replacement_assets->invalidate_all();
                        if (!retired.success()) return retired;
                    }
                }
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
        std::span<const std::uint8_t> resolve_source = scanout_;
        if (configuration_.precise_color ==
                raster::PreciseColorMode::TrueColorDisplay &&
            planned.plan.pixel_format == raster::ScanoutPixelFormat::Rgb555) {
            const std::uint64_t pixels =
                static_cast<std::uint64_t>(planned.plan.logical_width) *
                planned.plan.logical_height;
            bool enhanced =
                pixels <= configuration_.maximum_true_color_scanout_pixels;
            if (enhanced) {
                try {
                    true_color_scanout_.assign(scanout_.begin(), scanout_.end());
                } catch (...) {
                    enhanced = false;
                }
            }
            if (enhanced) {
                enhanced = raster_.apply_true_color_scanout(
                    planned.plan, true_color_scanout_).accepted();
            }
            if (enhanced) resolve_source = true_color_scanout_;
            else ++true_color_fallbacks_;
        }
        std::span<const std::uint8_t> presented = resolve_source;
        std::uint32_t presented_width = planned.plan.logical_width;
        std::uint32_t presented_height = planned.plan.logical_height;
        bool high_resolution_resolved = false;
        upscale_stats_ = {};
        if (configuration_.presentation_resolve ==
                raster::PresentationResolveMode::EdgeAware) {
            const std::uint32_t target_width =
                planned.plan.presentation.destination_width;
            const std::uint32_t target_height =
                planned.plan.presentation.destination_height;
            const std::uint64_t target_pixels =
                static_cast<std::uint64_t>(target_width) * target_height;
            bool resolved = target_pixels <= configuration_.maximum_resolved_pixels &&
                target_pixels <= 8192ULL * 8192ULL &&
                target_pixels <= std::numeric_limits<std::size_t>::max() / 3U;
            if (resolved) {
                try {
                    resolved_scanout_.resize(
                        static_cast<std::size_t>(target_pixels) * 3U);
                } catch (...) {
                    resolved = false;
                }
            }
            if (resolved) {
                const raster::EventResult upscale =
                    raster::ScanoutMapper::upscale_edge_aware_rgb888(
                        planned.plan.logical_width, planned.plan.logical_height,
                        target_width, target_height, resolve_source,
                        resolved_scanout_,
                        upscale_stats_);
                resolved = upscale.accepted();
            }
            if (resolved) {
                presented = resolved_scanout_;
                presented_width = target_width;
                presented_height = target_height;
                high_resolution_resolved = true;
            } else {
                ++upscale_fallbacks_;
            }
        }
        display_postprocess_stats_ = {};
        if (configuration_.display_postprocess ==
                raster::DisplayPostprocessMode::ChromaSmooth24Bit &&
            planned.plan.pixel_format == raster::ScanoutPixelFormat::PackedRgb888 &&
            high_resolution_resolved) {
            const std::uint64_t pixels =
                static_cast<std::uint64_t>(presented_width) * presented_height;
            bool processed = pixels <= configuration_.maximum_postprocess_pixels &&
                pixels <= std::numeric_limits<std::size_t>::max() / 3U;
            if (processed) {
                try {
                    postprocessed_scanout_.resize(
                        static_cast<std::size_t>(pixels) * 3U);
                } catch (...) {
                    processed = false;
                }
            }
            if (processed) {
                processed = raster::ScanoutMapper::chroma_smooth_rgb888(
                    presented_width, presented_height, presented,
                    postprocessed_scanout_, display_postprocess_stats_).accepted();
            }
            if (processed) {
                presented = postprocessed_scanout_;
                using_postprocessed_scanout_ = true;
            } else {
                ++display_postprocess_fallbacks_;
            }
        }
        if (configuration_.high_resolution_ui &&
            high_resolution_resolved && !ui_draws_.empty()) {
            std::span<std::uint8_t> enhanced_output =
                using_postprocessed_scanout_
                    ? std::span<std::uint8_t>(postprocessed_scanout_)
                    : std::span<std::uint8_t>(resolved_scanout_);
            const bool direct_scanout =
                planned.plan.pixel_format == raster::ScanoutPixelFormat::Rgb555 &&
                planned.plan.source_line_step == 1U &&
                planned.plan.source_field_offset == 0U &&
                static_cast<std::uint32_t>(planned.plan.vram_start_x_words) +
                        planned.plan.logical_width <= raster::kVramWidth &&
                static_cast<std::uint32_t>(planned.plan.vram_start_y) +
                        planned.plan.logical_height <= raster::kVramHeight;
            if (!direct_scanout) {
                ui_stats_.native_fallbacks += ui_draws_.size();
            } else {
                std::uint64_t remaining =
                    configuration_.maximum_ui_pixels_per_frame;
                const std::int64_t scan_left = planned.plan.vram_start_x_words;
                const std::int64_t scan_top = planned.plan.vram_start_y;
                const std::int64_t scan_right =
                    scan_left + planned.plan.logical_width;
                const std::int64_t scan_bottom =
                    scan_top + planned.plan.logical_height;
                const auto ceil_scale = [](std::uint64_t coordinate,
                                           std::uint32_t target,
                                           std::uint32_t source) noexcept {
                    return static_cast<std::uint32_t>(
                        (coordinate * target + source - 1U) / source);
                };
                for (const UiDraw& draw : ui_draws_) {
                    raster::ReplacementTexture texture{};
                    if (configuration_.replacement_assets == nullptr ||
                        !configuration_.replacement_assets->sampling_texture(
                            draw.handle, draw.match, texture)) {
                        ++ui_stats_.native_fallbacks;
                        continue;
                    }
                    const std::int64_t original_left =
                        static_cast<std::int64_t>(draw.event.origin.x) +
                        draw.event.state.draw_offset.x;
                    const std::int64_t original_top =
                        static_cast<std::int64_t>(draw.event.origin.y) +
                        draw.event.state.draw_offset.y;
                    const std::int64_t left = std::max({original_left,
                        static_cast<std::int64_t>(draw.event.state.draw_area.left),
                        scan_left});
                    const std::int64_t top = std::max({original_top,
                        static_cast<std::int64_t>(draw.event.state.draw_area.top),
                        scan_top});
                    const std::int64_t right = std::min({
                        original_left + draw.event.width,
                        static_cast<std::int64_t>(
                            draw.event.state.draw_area.right) + 1,
                        scan_right});
                    const std::int64_t bottom = std::min({
                        original_top + draw.event.height,
                        static_cast<std::int64_t>(
                            draw.event.state.draw_area.bottom) + 1,
                        scan_bottom});
                    if (right <= left || bottom <= top) {
                        ++ui_stats_.native_fallbacks;
                        continue;
                    }
                    const std::uint32_t output_left = ceil_scale(
                        static_cast<std::uint64_t>(left - scan_left),
                        presented_width, planned.plan.logical_width);
                    const std::uint32_t output_top = ceil_scale(
                        static_cast<std::uint64_t>(top - scan_top),
                        presented_height, planned.plan.logical_height);
                    const std::uint32_t output_right = ceil_scale(
                        static_cast<std::uint64_t>(right - scan_left),
                        presented_width, planned.plan.logical_width);
                    const std::uint32_t output_bottom = ceil_scale(
                        static_cast<std::uint64_t>(bottom - scan_top),
                        presented_height, planned.plan.logical_height);
                    const std::uint64_t pixels =
                        static_cast<std::uint64_t>(output_right - output_left) *
                        (output_bottom - output_top);
                    if (pixels == 0U || pixels > remaining) {
                        ++ui_stats_.native_fallbacks;
                        continue;
                    }
                    remaining -= pixels;
                    const std::int64_t pixels_per_word =
                        draw.match.depth == gpu::TextureDepth::Indexed4 ? 4 :
                        draw.match.depth == gpu::TextureDepth::Indexed8 ? 2 : 1;
                    const std::int64_t native_width =
                        static_cast<std::int64_t>(draw.match.source.width) *
                        pixels_per_word;
                    const std::int64_t native_height = draw.match.source.height;
                    const std::int64_t page_x =
                        static_cast<std::int64_t>(
                            draw.event.state.draw_mode.x_base) * 64;
                    const std::int64_t page_y =
                        draw.event.state.draw_mode.y_base;
                    const std::int64_t denominator_x =
                        static_cast<std::int64_t>(presented_width) * 2;
                    const std::int64_t denominator_y =
                        static_cast<std::int64_t>(presented_height) * 2;
                    bool touched = false;
                    for (std::uint32_t y = output_top; y < output_bottom; ++y) {
                        const std::int64_t local_y_numerator =
                            scan_top * denominator_y +
                            static_cast<std::int64_t>(2ULL * y + 1ULL) *
                                planned.plan.logical_height -
                            original_top * denominator_y;
                        const std::int64_t atlas_y_numerator =
                            (page_y - draw.match.source.y) * denominator_y +
                            static_cast<std::int64_t>(
                                draw.event.texture_coordinate->v) * denominator_y +
                            local_y_numerator;
                        if (atlas_y_numerator < 0 || native_height <= 0 ||
                            atlas_y_numerator >= native_height * denominator_y) {
                            continue;
                        }
                        const std::uint32_t sample_y =
                            static_cast<std::uint32_t>(atlas_y_numerator *
                                texture.height /
                                (native_height * denominator_y));
                        for (std::uint32_t x = output_left; x < output_right; ++x) {
                            ++ui_stats_.pixels_tested;
                            const std::int64_t local_x_numerator =
                                scan_left * denominator_x +
                                static_cast<std::int64_t>(2ULL * x + 1ULL) *
                                    planned.plan.logical_width -
                                original_left * denominator_x;
                            const std::int64_t atlas_x_numerator =
                                (page_x - draw.match.source.x) * pixels_per_word *
                                    denominator_x +
                                static_cast<std::int64_t>(
                                    draw.event.texture_coordinate->u) * denominator_x +
                                local_x_numerator;
                            if (atlas_x_numerator < 0 || native_width <= 0 ||
                                atlas_x_numerator >= native_width * denominator_x) {
                                continue;
                            }
                            const std::uint32_t sample_x =
                                static_cast<std::uint32_t>(atlas_x_numerator *
                                    texture.width /
                                    (native_width * denominator_x));
                            const std::size_t source =
                                (static_cast<std::size_t>(sample_y) * texture.width +
                                 sample_x) * 4U;
                            ++ui_stats_.texture_fetches;
                            if (texture.rgba8[source + 3U] == 0U) continue;
                            const auto channel = [&](std::size_t offset) noexcept {
                                const std::uint32_t value = texture.rgba8[source + offset];
                                if (draw.event.raw_texture) {
                                    return static_cast<std::uint8_t>(value);
                                }
                                const std::uint32_t modulation = offset == 0U
                                    ? draw.event.color.red
                                    : offset == 1U ? draw.event.color.green
                                                   : draw.event.color.blue;
                                return static_cast<std::uint8_t>(
                                    std::min(255U, value * modulation / 128U));
                            };
                            const std::size_t destination =
                                (static_cast<std::size_t>(y) * presented_width + x) * 3U;
                            enhanced_output[destination] = channel(0U);
                            enhanced_output[destination + 1U] = channel(1U);
                            enhanced_output[destination + 2U] = channel(2U);
                            ++ui_stats_.pixels_written;
                            touched = true;
                        }
                    }
                    if (touched) ++ui_stats_.composited;
                    else ++ui_stats_.native_fallbacks;
                }
            }
        }
        if (high_resolution_resolved) {
            resolved_width_ = presented_width;
            resolved_height_ = presented_height;
        }
        host::FrameSubmission submission{};
        const host::HostStatus bound = host::bind_rgb888_frame(
            presented_width, presented_height, presented,
            sequence_, update_index, submission);
        if (!bound.success()) return bound;
        visual_digest_ = kFnvOffset;
        digest_bytes(visual_digest_, presented);
        const std::array<std::uint64_t, 4U> metadata{
            raster_.vram_generation(), planned.plan.logical_width,
            planned.plan.logical_height,
            planned.plan.presentation.identity_4x3 ? 1U : 0U};
        digest_bytes(visual_digest_, std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(metadata.data()),
            sizeof(metadata)));
        if (high_resolution_resolved) {
            const std::array<std::uint64_t, 2U> resolved_metadata{
                presented_width, presented_height};
            digest_bytes(visual_digest_, std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(resolved_metadata.data()),
                sizeof(resolved_metadata)));
        }
        const host::HostStatus submitted = host.submit_frame(submission);
        if (submitted.success()) ++sequence_;
        return submitted;
    } catch (...) {
        return {host::HostCode::internal_error};
    }
}

std::uint64_t RuntimeGraphics::precise_primitives() const noexcept {
    return precise_primitives_;
}

std::uint64_t RuntimeGraphics::precise_fallbacks() const noexcept {
    return precise_fallbacks_;
}

bool RuntimeGraphics::positive_w_clipping_active() const noexcept {
    return positive_w_clipping_active_;
}

std::uint64_t RuntimeGraphics::geometry_digest() const noexcept {
    return geometry_digest_;
}

const raster::PreciseRasterStats& RuntimeGraphics::precise_raster_stats()
    const noexcept {
    return raster_.precise_stats();
}

const raster::UpscaleStats& RuntimeGraphics::upscale_stats() const noexcept {
    return upscale_stats_;
}

std::uint64_t RuntimeGraphics::upscale_fallbacks() const noexcept {
    return upscale_fallbacks_;
}

const RuntimeGraphics::UiStats& RuntimeGraphics::ui_stats() const noexcept {
    return ui_stats_;
}

std::span<const std::uint8_t> RuntimeGraphics::resolved_pixels() const noexcept {
    if (resolved_width_ == 0U || resolved_height_ == 0U) return {};
    return using_postprocessed_scanout_
        ? std::span<const std::uint8_t>(postprocessed_scanout_)
        : std::span<const std::uint8_t>(resolved_scanout_);
}

std::uint32_t RuntimeGraphics::resolved_width() const noexcept {
    return resolved_width_;
}

std::uint32_t RuntimeGraphics::resolved_height() const noexcept {
    return resolved_height_;
}

const raster::DisplayPostprocessStats&
RuntimeGraphics::display_postprocess_stats() const noexcept {
    return display_postprocess_stats_;
}

std::uint64_t RuntimeGraphics::display_postprocess_fallbacks() const noexcept {
    return display_postprocess_fallbacks_;
}

std::uint64_t RuntimeGraphics::true_color_fallbacks() const noexcept {
    return true_color_fallbacks_;
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

struct GpuTraceRecorder::Impl final {
    explicit Impl(GpuTraceIdentity value) : trace{std::move(value)} {}
    GpuCommandTrace trace{};
    gpu::FrontEnd frontend{};
    raster::ReferenceRasterizer raster{};
    GpuTraceCode failure = GpuTraceCode::Success;
    bool finished = false;
};

GpuTraceRecorder::GpuTraceRecorder(GpuTraceIdentity identity)
    : impl_(std::make_unique<Impl>(std::move(identity))) {
    if (!trace_sha256(impl_->trace.identity.variant_sha256) ||
        !trace_sha256(impl_->trace.identity.executable_sha256) ||
        impl_->trace.identity.initial_vram_generation !=
            impl_->raster.vram_generation()) {
        impl_->failure = GpuTraceCode::InvalidIdentity;
    }
}

GpuTraceRecorder::~GpuTraceRecorder() noexcept = default;

GpuTraceCode GpuTraceRecorder::append(
    const gpu::Event& event,
    std::span<const raster::PreciseVertex> precise_vertices) {
    if (impl_->failure != GpuTraceCode::Success) return impl_->failure;
    if (impl_->finished || impl_->trace.commands.size() >= kMaximumTraceCommands) {
        return impl_->failure = GpuTraceCode::DroppedCommand;
    }
    try {
        std::vector<std::uint32_t> words = trace_words(event);
        if (words.empty()) {
            return impl_->failure = GpuTraceCode::UnsupportedPrimitive;
        }
        std::optional<gpu::Event> decoded = trace_decode(impl_->frontend, words);
        if (!decoded.has_value()) {
            return impl_->failure = GpuTraceCode::MalformedCommand;
        }
        const bool polygon = std::holds_alternative<gpu::PolygonEvent>(*decoded);
        const std::size_t expected_vertices = polygon
            ? (std::get<gpu::PolygonEvent>(*decoded).quad ? 6U : 3U) : 0U;
        if ((!polygon && !precise_vertices.empty()) ||
            (polygon && !precise_vertices.empty() &&
             precise_vertices.size() != expected_vertices)) {
            return impl_->failure = GpuTraceCode::UnsupportedPrimitive;
        }
        for (const raster::PreciseVertex& vertex : precise_vertices) {
            if (vertex.storage_generation == 0U ||
                vertex.origin_generation == 0U) {
                return impl_->failure = GpuTraceCode::StaleVramGeneration;
            }
        }
        GpuTraceCommand command{};
        command.sequence = impl_->trace.commands.size();
        command.before_vram_generation = impl_->raster.vram_generation();
        command.gp0_words = std::move(words);
        command.precise_vertices.assign(
            precise_vertices.begin(), precise_vertices.end());
        const raster::EventResult consumed = impl_->raster.consume(*decoded);
        if (!consumed.accepted()) {
            return impl_->failure = consumed.error == raster::RasterError::WorkLimitExceeded
                ? GpuTraceCode::WorkLimitExceeded
                : GpuTraceCode::UnsupportedPrimitive;
        }
        command.after_vram_generation = impl_->raster.vram_generation();
        impl_->trace.commands.push_back(std::move(command));
        return GpuTraceCode::Success;
    } catch (...) {
        return impl_->failure = GpuTraceCode::MalformedCommand;
    }
}

std::optional<GpuCommandTrace> GpuTraceRecorder::finish(
    std::uint64_t declared_command_count) {
    if (impl_->failure != GpuTraceCode::Success || impl_->finished ||
        declared_command_count != impl_->trace.commands.size() ||
        declared_command_count == 0U) {
        impl_->failure = GpuTraceCode::DroppedCommand;
        return std::nullopt;
    }
    impl_->finished = true;
    impl_->trace.declared_command_count = declared_command_count;
    impl_->trace.command_digest = trace_digest(impl_->trace);
    return std::move(impl_->trace);
}

GpuReplayReceipt GpuTraceReplay::replay(
    const GpuCommandTrace& trace,
    const GpuTraceIdentity& expected_identity,
    raster::PreciseCullingPolicy culling,
    raster::TextureFilterMode filtering,
    raster::PreciseColorMode color_mode) noexcept {
    GpuReplayReceipt receipt{};
    receipt.identity = trace.identity;
    receipt.command_digest = trace.command_digest;
    try {
        if (!trace_sha256(trace.identity.variant_sha256) ||
            !trace_sha256(trace.identity.executable_sha256)) {
            receipt.code = GpuTraceCode::InvalidIdentity;
            return receipt;
        }
        if (!(trace.identity == expected_identity)) {
            receipt.code = GpuTraceCode::IdentityMismatch;
            return receipt;
        }
        if (trace.declared_command_count == 0U ||
            trace.declared_command_count != trace.commands.size() ||
            trace.commands.size() > kMaximumTraceCommands ||
            trace.command_digest != trace_digest(trace)) {
            receipt.code = GpuTraceCode::DroppedCommand;
            return receipt;
        }
        gpu::FrontEnd native_frontend{};
        gpu::FrontEnd enhanced_frontend{};
        raster::ReferenceRasterizer native{};
        raster::ReferenceRasterizer enhanced{};
        if (native.vram_generation() != trace.identity.initial_vram_generation ||
            enhanced.vram_generation() != trace.identity.initial_vram_generation) {
            receipt.code = GpuTraceCode::StaleVramGeneration;
            return receipt;
        }
        for (std::size_t index = 0U; index < trace.commands.size(); ++index) {
            const GpuTraceCommand& command = trace.commands[index];
            if (command.sequence != index) {
                receipt.code = GpuTraceCode::DroppedCommand;
                return receipt;
            }
            if (native.vram_generation() != command.before_vram_generation ||
                enhanced.vram_generation() != command.before_vram_generation) {
                receipt.code = GpuTraceCode::StaleVramGeneration;
                return receipt;
            }
            std::optional<gpu::Event> native_event =
                trace_decode(native_frontend, command.gp0_words);
            std::optional<gpu::Event> enhanced_event =
                trace_decode(enhanced_frontend, command.gp0_words);
            if (!native_event.has_value() || !enhanced_event.has_value()) {
                receipt.code = GpuTraceCode::MalformedCommand;
                return receipt;
            }
            const raster::EventResult native_result = native.consume(*native_event);
            if (!native_result.accepted()) {
                receipt.code = native_result.error == raster::RasterError::WorkLimitExceeded
                    ? GpuTraceCode::WorkLimitExceeded
                    : GpuTraceCode::UnsupportedPrimitive;
                return receipt;
            }
            receipt.native_work_units += native_result.work_units;
            raster::EventResult enhanced_result{};
            if (const auto* polygon =
                    std::get_if<gpu::PolygonEvent>(&*enhanced_event)) {
                ++receipt.polygon_commands;
                const std::size_t expected = polygon->quad ? 6U : 3U;
                if (command.precise_vertices.size() != expected ||
                    std::any_of(command.precise_vertices.begin(),
                        command.precise_vertices.end(),
                        [](const raster::PreciseVertex& vertex) noexcept {
                            return vertex.storage_generation == 0U ||
                                   vertex.origin_generation == 0U;
                        })) {
                    receipt.code = GpuTraceCode::UnsupportedPrimitive;
                    return receipt;
                }
                enhanced_result = enhanced.consume_precise_polygon(
                    *polygon, command.precise_vertices, nullptr, culling,
                    filtering, color_mode);
                ++receipt.precise_commands;
            } else {
                if (!command.precise_vertices.empty()) {
                    receipt.code = GpuTraceCode::UnsupportedPrimitive;
                    return receipt;
                }
                enhanced_result = enhanced.consume(*enhanced_event);
                ++receipt.native_only_commands;
            }
            if (!enhanced_result.accepted()) {
                receipt.code = enhanced_result.error ==
                        raster::RasterError::WorkLimitExceeded
                    ? GpuTraceCode::WorkLimitExceeded
                    : GpuTraceCode::UnsupportedPrimitive;
                return receipt;
            }
            receipt.enhanced_work_units += enhanced_result.work_units;
            if (native.vram_generation() != command.after_vram_generation ||
                enhanced.vram_generation() != command.after_vram_generation) {
                receipt.code = GpuTraceCode::StaleVramGeneration;
                return receipt;
            }
            ++receipt.commands_replayed;
        }
        receipt.native_vram_digest = native.digest();
        receipt.enhanced_vram_digest = enhanced.digest();
        receipt.precise_stats = enhanced.precise_stats();
        receipt.code = GpuTraceCode::Success;
        return receipt;
    } catch (...) {
        receipt.code = GpuTraceCode::MalformedCommand;
        return receipt;
    }
}

std::string GpuReplayReceipt::canonical_json() const {
    return "{\"command_digest\":" + std::to_string(command_digest) +
        ",\"commands_replayed\":" + std::to_string(commands_replayed) +
        ",\"coverage\":{\"native_only\":" +
        std::to_string(native_only_commands) + ",\"polygons\":" +
        std::to_string(polygon_commands) + ",\"precise\":" +
        std::to_string(precise_commands) + "},\"enhanced_vram_digest\":" +
        std::to_string(enhanced_vram_digest) + ",\"identity\":{\"executable_sha256\":\"" +
        identity.executable_sha256 + "\",\"frame_index\":" +
        std::to_string(identity.frame_index) + ",\"initial_vram_generation\":" +
        std::to_string(identity.initial_vram_generation) +
        ",\"variant_sha256\":\"" + identity.variant_sha256 +
        "\"},\"native_vram_digest\":" + std::to_string(native_vram_digest) +
        ",\"performance\":{\"enhanced_work_units\":" +
        std::to_string(enhanced_work_units) + ",\"native_work_units\":" +
        std::to_string(native_work_units) + ",\"precise_fragments\":" +
        std::to_string(precise_stats.precise_fragments) +
        "},\"schema\":\"clean-aot.gpu-replay-receipt.v1\",\"status\":" +
        std::to_string(static_cast<std::uint8_t>(code)) + "}";
}

} // namespace clean_aot::app
