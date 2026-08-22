#include "clean_aot/raster/raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace clean_aot::raster {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

constexpr std::array<std::array<std::int32_t, 4>, 4> kDitherMatrix{{
    {{-4, 0, -3, 1}},
    {{2, -2, 3, -1}},
    {{-3, 1, -4, 0}},
    {{3, -1, 2, -2}},
}};

struct Point {
  std::int64_t x = 0;
  std::int64_t y = 0;
};

struct Color {
  std::int64_t red = 0;
  std::int64_t green = 0;
  std::int64_t blue = 0;
};

struct Uv {
  std::int64_t u = 0;
  std::int64_t v = 0;
};

struct TriangleVertex {
  Point point{};
  Color color{};
  Uv uv{};
};

struct Triangle {
  std::array<TriangleVertex, 3> vertices{};
};

struct Bounds {
  bool empty = true;
  std::int64_t left = 0;
  std::int64_t top = 0;
  std::int64_t right = -1;
  std::int64_t bottom = -1;
  std::uint64_t work_units = 0;
};

struct TextureSample {
  Color color_5bit{};
  bool transparent = false;
  bool semi_transparent = false;
};

struct AttributePlane {
  Point origin{};
  std::int64_t value_at_origin = 0;
  std::int64_t step_x = 0;
  std::int64_t step_y = 0;
};

[[nodiscard]] constexpr std::uint64_t mix_byte(std::uint64_t hash,
                                               std::uint8_t value) noexcept {
  return (hash ^ static_cast<std::uint64_t>(value)) * kFnvPrime;
}

[[nodiscard]] constexpr std::uint8_t low_byte(std::uint16_t value) noexcept {
  return static_cast<std::uint8_t>(value & 0xffu);
}

[[nodiscard]] constexpr std::uint8_t clamp_u8(std::int64_t value) noexcept {
  if (value <= 0) {
    return 0;
  }
  if (value >= 255) {
    return 255;
  }
  return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint16_t
color_to_word(const gpu::Color &color) noexcept {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(color.red >> 3u) |
      static_cast<std::uint16_t>((color.green >> 3u) << 5u) |
      static_cast<std::uint16_t>((color.blue >> 3u) << 10u));
}

[[nodiscard]] constexpr std::int64_t abs_i64(std::int64_t value) noexcept {
  return value < 0 ? -value : value;
}

[[nodiscard]] constexpr bool
valid_primitive_edge(const Point &first, const Point &second) noexcept {
  return abs_i64(second.x - first.x) <= 1023 &&
         abs_i64(second.y - first.y) <= 511;
}

[[nodiscard]] constexpr bool
valid_triangle_size(const std::array<TriangleVertex, 3> &vertices) noexcept {
  return valid_primitive_edge(vertices[0].point, vertices[1].point) &&
         valid_primitive_edge(vertices[1].point, vertices[2].point) &&
         valid_primitive_edge(vertices[2].point, vertices[0].point);
}

[[nodiscard]] constexpr bool
valid_semi_mode(gpu::SemiTransparencyMode mode) noexcept {
  return static_cast<std::uint8_t>(mode) <= 3u;
}

[[nodiscard]] constexpr bool valid_depth(gpu::TextureDepth depth) noexcept {
  return depth == gpu::TextureDepth::Indexed4 ||
         depth == gpu::TextureDepth::Indexed8 ||
         depth == gpu::TextureDepth::Direct15 ||
         depth == gpu::TextureDepth::Reserved;
}

[[nodiscard]] constexpr bool
valid_environment(gpu::EnvironmentKind environment) noexcept {
  return static_cast<std::uint8_t>(environment) <=
         static_cast<std::uint8_t>(gpu::EnvironmentKind::Mask);
}

[[nodiscard]] constexpr std::int64_t
wrap_coordinate(std::int64_t value, std::int64_t size) noexcept {
  const std::int64_t remainder = value % size;
  return remainder < 0 ? remainder + size : remainder;
}

[[nodiscard]] constexpr std::uint16_t
read_wrapped(const std::array<VramWord, kVramWordCount> &vram, std::int64_t x,
             std::int64_t y) noexcept {
  const std::size_t wrapped_x = static_cast<std::size_t>(
      wrap_coordinate(x, static_cast<std::int64_t>(kVramWidth)));
  const std::size_t wrapped_y = static_cast<std::size_t>(
      wrap_coordinate(y, static_cast<std::int64_t>(kVramHeight)));
  return vram[wrapped_y * static_cast<std::size_t>(kVramWidth) + wrapped_x];
}

void write_wrapped(std::array<VramWord, kVramWordCount> &vram, std::int64_t x,
                   std::int64_t y, std::uint16_t value) noexcept {
  const std::size_t wrapped_x = static_cast<std::size_t>(
      wrap_coordinate(x, static_cast<std::int64_t>(kVramWidth)));
  const std::size_t wrapped_y = static_cast<std::size_t>(
      wrap_coordinate(y, static_cast<std::int64_t>(kVramHeight)));
  vram[wrapped_y * static_cast<std::size_t>(kVramWidth) + wrapped_x] = value;
}

[[nodiscard]] constexpr std::int64_t edge(const Point &a, const Point &b,
                                          std::int64_t pixel_x,
                                          std::int64_t pixel_y) noexcept {
  // The PS1 samples polygons at integer raster coordinates. Combined with
  // the top/left ownership rule this excludes the lower and right edges.
  return (b.x - a.x) * (pixel_y - a.y) - (b.y - a.y) * (pixel_x - a.x);
}

[[nodiscard]] constexpr std::int64_t cross(const Point &a, const Point &b,
                                           const Point &c) noexcept {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] constexpr bool top_left_edge(const Point &a,
                                           const Point &b) noexcept {
  // Screen Y grows downwards. This is the half-open edge convention that
  // assigns a shared quad edge to exactly one of its two triangles.
  return b.y < a.y || (b.y == a.y && b.x > a.x);
}

[[nodiscard]] constexpr std::int64_t fixed_floor(std::int64_t value,
                                                 std::int64_t scale) noexcept {
  const std::int64_t quotient = value / scale;
  const std::int64_t remainder = value % scale;
  return quotient - (remainder < 0 ? 1 : 0);
}

[[nodiscard]] constexpr std::int64_t
line_coordinate_step(std::int64_t delta, std::int64_t steps) noexcept {
  constexpr std::int64_t kCoordinateScale = INT64_C(1) << 32u;
  std::int64_t numerator = delta * kCoordinateScale;
  // The line unit biases a non-integral slope by one subpixel quantum before
  // division. This is observable at half-way ties such as (0,0)-(2,1).
  if (delta > 0) {
    numerator += steps - 1;
  } else if (delta < 0) {
    numerator -= steps - 1;
  }
  return numerator / steps;
}

[[nodiscard]] constexpr std::int64_t
line_attribute_step(std::int64_t first, std::int64_t second,
                    std::int64_t steps) noexcept {
  constexpr std::int64_t kAttributeScale = INT64_C(1) << 12u;
  return ((second - first) * kAttributeScale) / steps;
}

[[nodiscard]] constexpr AttributePlane
make_attribute_plane(const std::array<TriangleVertex, 3> &vertices,
                     std::int64_t first, std::int64_t second,
                     std::int64_t third, std::int64_t area) noexcept {
  constexpr std::int64_t kAttributeScale = INT64_C(1) << 12u;
  constexpr std::int64_t kAttributeHalf = kAttributeScale / 2;
  const std::int64_t delta_x_one = vertices[1].point.x - vertices[0].point.x;
  const std::int64_t delta_y_one = vertices[1].point.y - vertices[0].point.y;
  const std::int64_t delta_x_two = vertices[2].point.x - vertices[0].point.x;
  const std::int64_t delta_y_two = vertices[2].point.y - vertices[0].point.y;
  const std::int64_t attribute_one = second - first;
  const std::int64_t attribute_two = third - first;
  const std::int64_t numerator_x =
      attribute_one * delta_y_two - attribute_two * delta_y_one;
  const std::int64_t numerator_y =
      delta_x_one * attribute_two - delta_x_two * attribute_one;
  return AttributePlane{vertices[0].point,
                        first * kAttributeScale + kAttributeHalf,
                        (numerator_x * kAttributeScale) / area,
                        (numerator_y * kAttributeScale) / area};
}

[[nodiscard]] constexpr std::int64_t
sample_attribute_plane(const AttributePlane &plane, std::int64_t x,
                       std::int64_t y) noexcept {
  constexpr std::int64_t kAttributeScale = INT64_C(1) << 12u;
  const std::int64_t fixed_value = plane.value_at_origin +
                                   plane.step_x * (x - plane.origin.x) +
                                   plane.step_y * (y - plane.origin.y);
  return fixed_floor(fixed_value, kAttributeScale);
}

[[nodiscard]] constexpr std::int64_t
window_coordinate(std::int64_t coordinate, std::uint8_t mask,
                  std::uint8_t offset) noexcept {
  const std::int64_t value = wrap_coordinate(coordinate, 256);
  const std::int64_t window_mask = static_cast<std::int64_t>(mask) << 3u;
  const std::int64_t window_offset = (static_cast<std::int64_t>(offset & mask))
                                     << 3u;
  return (value & ~window_mask) | window_offset;
}

[[nodiscard]] constexpr std::uint16_t
pack_color(Color color, std::int64_t x, std::int64_t y, bool dither) noexcept {
  if (dither) {
    const std::int32_t dither_value =
        kDitherMatrix[static_cast<std::size_t>(y & 3)]
                     [static_cast<std::size_t>(x & 3)];
    color.red += dither_value;
    color.green += dither_value;
    color.blue += dither_value;
  }
  const std::uint16_t red =
      static_cast<std::uint16_t>(clamp_u8(color.red) >> 3u);
  const std::uint16_t green =
      static_cast<std::uint16_t>(clamp_u8(color.green) >> 3u);
  const std::uint16_t blue =
      static_cast<std::uint16_t>(clamp_u8(color.blue) >> 3u);
  return static_cast<std::uint16_t>(red | (green << 5u) | (blue << 10u));
}

[[nodiscard]] constexpr std::uint16_t
blend_color(std::uint16_t foreground, std::uint16_t background,
            gpu::SemiTransparencyMode mode) noexcept {
  const std::uint16_t foreground_red = foreground & 0x1fu;
  const std::uint16_t foreground_green = (foreground >> 5u) & 0x1fu;
  const std::uint16_t foreground_blue = (foreground >> 10u) & 0x1fu;
  const std::uint16_t background_red = background & 0x1fu;
  const std::uint16_t background_green = (background >> 5u) & 0x1fu;
  const std::uint16_t background_blue = (background >> 10u) & 0x1fu;

  const auto blend_channel =
      [mode](std::uint16_t foreground_channel,
             std::uint16_t background_channel) -> std::uint16_t {
    std::int32_t value = 0;
    switch (mode) {
    case gpu::SemiTransparencyMode::HalfBackgroundHalfForeground:
      value = static_cast<std::int32_t>(background_channel / 2u) +
              static_cast<std::int32_t>(foreground_channel / 2u);
      break;
    case gpu::SemiTransparencyMode::BackgroundPlusForeground:
      value = static_cast<std::int32_t>(background_channel) +
              static_cast<std::int32_t>(foreground_channel);
      break;
    case gpu::SemiTransparencyMode::BackgroundMinusForeground:
      value = static_cast<std::int32_t>(background_channel) -
              static_cast<std::int32_t>(foreground_channel);
      break;
    case gpu::SemiTransparencyMode::BackgroundPlusQuarterForeground:
      value = static_cast<std::int32_t>(background_channel) +
              static_cast<std::int32_t>(foreground_channel / 4u);
      break;
    }
    if (value < 0) {
      value = 0;
    }
    if (value > 31) {
      value = 31;
    }
    return static_cast<std::uint16_t>(value);
  };

  return static_cast<std::uint16_t>(
      blend_channel(foreground_red, background_red) |
      (blend_channel(foreground_green, background_green) << 5u) |
      (blend_channel(foreground_blue, background_blue) << 10u));
}

[[nodiscard]] constexpr Color gpu_color(const gpu::Color &color) noexcept {
  return Color{color.red, color.green, color.blue};
}

[[nodiscard]] constexpr std::uint64_t area_work(std::int64_t width,
                                                std::int64_t height) noexcept {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  const auto unsigned_width = static_cast<std::uint64_t>(width);
  const auto unsigned_height = static_cast<std::uint64_t>(height);
  if (unsigned_width >
      std::numeric_limits<std::uint64_t>::max() / unsigned_height) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return unsigned_width * unsigned_height;
}

[[nodiscard]] Bounds clipped_bounds(const gpu::RenderStateSnapshot &state,
                                    std::int64_t left, std::int64_t top,
                                    std::int64_t right,
                                    std::int64_t bottom) noexcept {
  Bounds result{};
  if (left > right || top > bottom) {
    return result;
  }

  const std::int64_t state_left =
      std::min<std::int64_t>(state.draw_area.left, kVramWidth - 1);
  const std::int64_t state_top =
      std::min<std::int64_t>(state.draw_area.top, kVramHeight - 1);
  const std::int64_t state_right =
      std::min<std::int64_t>(state.draw_area.right, kVramWidth - 1);
  const std::int64_t state_bottom =
      std::min<std::int64_t>(state.draw_area.bottom, kVramHeight - 1);

  result.left = std::max({left, state_left, std::int64_t{0}});
  result.top = std::max({top, state_top, std::int64_t{0}});
  result.right =
      std::min({right, state_right, static_cast<std::int64_t>(kVramWidth) - 1});
  result.bottom = std::min(
      {bottom, state_bottom, static_cast<std::int64_t>(kVramHeight) - 1});
  if (result.left > result.right || result.top > result.bottom) {
    return Bounds{};
  }

  result.empty = false;
  result.work_units =
      area_work(result.right - result.left + 1, result.bottom - result.top + 1);
  return result;
}

[[nodiscard]] constexpr bool
valid_common_state(const gpu::RenderStateSnapshot &state) noexcept {
  return state.draw_area.left < kVramWidth &&
         state.draw_area.right < kVramWidth &&
         state.draw_area.top < kVramHeight &&
         state.draw_area.bottom < kVramHeight && state.draw_offset.x >= -1024 &&
         state.draw_offset.x <= 1023 && state.draw_offset.y >= -1024 &&
         state.draw_offset.y <= 1023 && state.draw_mode.x_base <= 15u &&
         (state.draw_mode.y_base == 0u || state.draw_mode.y_base == 256u) &&
         valid_semi_mode(state.draw_mode.semi_transparency) &&
         state.texture_window.mask_x <= 31u &&
         state.texture_window.mask_y <= 31u &&
         state.texture_window.offset_x <= 31u &&
         state.texture_window.offset_y <= 31u;
}

[[nodiscard]] constexpr Point
with_offset(const gpu::Vertex &vertex, const gpu::DrawOffset &offset) noexcept {
  return Point{static_cast<std::int64_t>(vertex.x) + offset.x,
               static_cast<std::int64_t>(vertex.y) + offset.y};
}

[[nodiscard]] constexpr Color vertex_color(const gpu::Color &color) noexcept {
  return gpu_color(color);
}

[[nodiscard]] constexpr Uv
texture_uv(const gpu::TextureCoordinate &coordinate) noexcept {
  return Uv{coordinate.u, coordinate.v};
}

[[nodiscard]] constexpr bool
valid_vram_origin(const gpu::VramPoint &point) noexcept {
  return point.x < kVramWidth && point.y < kVramHeight;
}

[[nodiscard]] constexpr bool
valid_extent(const gpu::VramExtent &extent) noexcept {
  return extent.width <= kVramWidth && extent.height <= kVramHeight;
}

[[nodiscard]] constexpr bool
valid_transfer_extent(const gpu::VramExtent &extent) noexcept {
  return extent.width > 0u && extent.height > 0u && valid_extent(extent);
}

[[nodiscard]] constexpr std::size_t
expected_words(std::size_t pixel_count) noexcept {
  return (pixel_count + 1u) / 2u;
}

[[nodiscard]] constexpr std::uint16_t
transfer_half(std::uint32_t word, std::size_t index) noexcept {
  return static_cast<std::uint16_t>((index & 1u) == 0u ? word & 0xffffu
                                                       : word >> 16u);
}

} // namespace

struct ReferenceRasterizer::Impl {
  explicit Impl(RasterLimits value) noexcept : limits(value) {}

  std::array<VramWord, kVramWordCount> vram{};
  RasterLimits limits{};
  std::uint64_t vram_generation = 0u;
  const ReplacementTexture *replacement = nullptr;
  PreciseRasterStats precise_stats{};

  [[nodiscard]] EventResult reject(RasterError error) const noexcept {
    return EventResult{EventStatus::Rejected, error, 0, {}};
  }

  [[nodiscard]] bool reserve(WorkBudget &budget,
                             std::uint64_t work_units) const noexcept {
    if (budget.max_events == 0u || work_units > budget.max_work_units ||
        work_units > limits.max_work_units_per_event) {
      return false;
    }
    --budget.max_events;
    budget.max_work_units -= work_units;
    return true;
  }

  [[nodiscard]] EventResult
  accept(WorkBudget &budget, std::uint64_t work_units,
         EventStatus status = EventStatus::Applied) const noexcept {
    if (!reserve(budget, work_units)) {
      return reject(RasterError::WorkLimitExceeded);
    }
    return EventResult{status, RasterError::None, work_units, {}};
  }

  [[nodiscard]] bool valid_texture_state(
      const gpu::RenderStateSnapshot &state, bool textured,
      const std::optional<gpu::ClutAttribute> &clut) const noexcept {
    if (!valid_common_state(state)) {
      return false;
    }
    if (!textured || state.draw_mode.texture_disable) {
      return true;
    }
    if (!valid_depth(state.draw_mode.depth)) {
      return false;
    }
    if ((state.draw_mode.depth == gpu::TextureDepth::Indexed4 ||
         state.draw_mode.depth == gpu::TextureDepth::Indexed8) &&
        !clut.has_value()) {
      return false;
    }
    if (clut.has_value() && (clut->x >= kVramWidth || clut->y >= kVramHeight ||
                             (clut->x & 0x0fu) != 0u)) {
      return false;
    }
    return true;
  }

  [[nodiscard]] Bounds triangle_bounds(
      const gpu::RenderStateSnapshot &state,
      const std::array<TriangleVertex, 3> &vertices) const noexcept {
    std::int64_t left = vertices[0].point.x;
    std::int64_t right = vertices[0].point.x;
    std::int64_t top = vertices[0].point.y;
    std::int64_t bottom = vertices[0].point.y;
    for (std::size_t index = 1; index < vertices.size(); ++index) {
      left = std::min(left, vertices[index].point.x);
      right = std::max(right, vertices[index].point.x);
      top = std::min(top, vertices[index].point.y);
      bottom = std::max(bottom, vertices[index].point.y);
    }
    // The maximum vertex coordinate is an exclusive boundary. This also keeps
    // a shared diagonal from producing an extra row or column.
    return clipped_bounds(state, left, top, right - 1, bottom - 1);
  }

  [[nodiscard]] TextureSample
  sample_texture(const gpu::RenderStateSnapshot &state,
                 const std::optional<gpu::ClutAttribute> &clut, std::int64_t u,
                 std::int64_t v) const noexcept {
    const bool indexed = state.draw_mode.depth == gpu::TextureDepth::Indexed4 ||
                         state.draw_mode.depth == gpu::TextureDepth::Indexed8;
    if (indexed && !clut.has_value()) {
      return TextureSample{Color{}, true, false};
    }
    const gpu::ClutAttribute palette = clut.value_or(gpu::ClutAttribute{});
    const std::int64_t windowed_u = window_coordinate(
        u, state.texture_window.mask_x, state.texture_window.offset_x);
    const std::int64_t windowed_v = window_coordinate(
        v, state.texture_window.mask_y, state.texture_window.offset_y);
    const std::int64_t page_x =
        static_cast<std::int64_t>(state.draw_mode.x_base) * 64;
    const std::int64_t page_y = state.draw_mode.y_base;

    if (replacement != nullptr && replacement->handle != 0u &&
        replacement->depth == state.draw_mode.depth &&
        replacement->width != 0u && replacement->height != 0u &&
        replacement->rgba8.size() ==
            static_cast<std::size_t>(replacement->width) *
                replacement->height * 4u) {
      const std::int64_t pixels_per_word =
          state.draw_mode.depth == gpu::TextureDepth::Indexed4 ? 4 :
          state.draw_mode.depth == gpu::TextureDepth::Indexed8 ? 2 : 1;
      const std::int64_t source_word_x = page_x + windowed_u / pixels_per_word;
      const std::int64_t source_y = page_y + windowed_v;
      const std::int64_t native_width =
          static_cast<std::int64_t>(replacement->source.width) * pixels_per_word;
      const std::int64_t native_height = replacement->source.height;
      const std::int64_t native_x =
          (source_word_x - replacement->source.x) * pixels_per_word +
          (windowed_u % pixels_per_word);
      const std::int64_t native_y = source_y - replacement->source.y;
      if (native_x >= 0 && native_y >= 0 && native_x < native_width &&
          native_y < native_height &&
          replacement->width % static_cast<std::uint32_t>(native_width) == 0u &&
          replacement->height % static_cast<std::uint32_t>(native_height) == 0u) {
        const std::uint32_t sample_x = static_cast<std::uint32_t>(native_x) *
            replacement->width / static_cast<std::uint32_t>(native_width);
        const std::uint32_t sample_y = static_cast<std::uint32_t>(native_y) *
            replacement->height / static_cast<std::uint32_t>(native_height);
        const std::size_t offset =
            (static_cast<std::size_t>(sample_y) * replacement->width + sample_x) * 4u;
        const std::uint8_t alpha = replacement->rgba8[offset + 3u];
        const auto channel = [](std::uint8_t value) noexcept -> std::int64_t {
          return value == 0u ? 0 : std::max<std::int64_t>(1, value >> 3u);
        };
        Color color{channel(replacement->rgba8[offset]),
                    channel(replacement->rgba8[offset + 1u]),
                    channel(replacement->rgba8[offset + 2u])};
        if (alpha != 0u && color.red == 0 && color.green == 0 && color.blue == 0) {
          color = Color{1, 1, 1};
        }
        return TextureSample{color,
            alpha == 0u, alpha != 0u && alpha != 255u};
      }
    }

    std::uint16_t texel = 0;
    if (state.draw_mode.depth == gpu::TextureDepth::Indexed4) {
      const std::uint16_t word =
          read_wrapped(vram, page_x + (windowed_u / 4), page_y + windowed_v);
      const unsigned shift = static_cast<unsigned>((windowed_u & 3) * 4);
      const std::uint16_t index =
          static_cast<std::uint16_t>((word >> shift) & 0x0fu);
      texel = read_wrapped(vram, static_cast<std::int64_t>(palette.x) + index,
                           palette.y);
    } else if (state.draw_mode.depth == gpu::TextureDepth::Indexed8) {
      const std::uint16_t word =
          read_wrapped(vram, page_x + (windowed_u / 2), page_y + windowed_v);
      const unsigned shift = static_cast<unsigned>((windowed_u & 1) * 8);
      const std::uint16_t index =
          static_cast<std::uint16_t>((word >> shift) & 0xffu);
      texel = read_wrapped(vram, static_cast<std::int64_t>(palette.x) + index,
                           palette.y);
    } else {
      texel = read_wrapped(vram, page_x + windowed_u, page_y + windowed_v);
    }

    return TextureSample{
        Color{texel & 0x1fu, (texel >> 5u) & 0x1fu, (texel >> 10u) & 0x1fu},
        texel == 0u, (texel & 0x8000u) != 0u};
  }

  [[nodiscard]] TextureSample sample_texture_filtered(
      const gpu::RenderStateSnapshot &state,
      const std::optional<gpu::ClutAttribute> &clut, double u, double v,
      TextureFilterMode filtering) noexcept {
    const std::int64_t nearest_u = static_cast<std::int64_t>(u);
    const std::int64_t nearest_v = static_cast<std::int64_t>(v);
    if (filtering == TextureFilterMode::NearestNative) {
      return sample_texture(state, clut, nearest_u, nearest_v);
    }
    const double floor_u = std::floor(u);
    const double floor_v = std::floor(v);
    const double fraction_u = u - floor_u;
    const double fraction_v = v - floor_v;
    if (fraction_u == 0.0 && fraction_v == 0.0) {
      return sample_texture(state, clut, nearest_u, nearest_v);
    }
    const std::int64_t x0 = static_cast<std::int64_t>(floor_u);
    const std::int64_t y0 = static_cast<std::int64_t>(floor_v);
    const std::array<TextureSample, 4U> samples{
        sample_texture(state, clut, x0, y0),
        sample_texture(state, clut, x0 + 1, y0),
        sample_texture(state, clut, x0, y0 + 1),
        sample_texture(state, clut, x0 + 1, y0 + 1)};
    precise_stats.bilinear_texture_fetches += 4U;
    const bool semi = samples[0].semi_transparent;
    if (std::any_of(samples.begin(), samples.end(),
                    [semi](const TextureSample& sample) {
                      return sample.transparent ||
                          sample.semi_transparent != semi;
                    })) {
      ++precise_stats.transparency_fallbacks;
      return sample_texture(state, clut, nearest_u, nearest_v);
    }
    const std::array<double, 4U> weights{
        (1.0 - fraction_u) * (1.0 - fraction_v),
        fraction_u * (1.0 - fraction_v),
        (1.0 - fraction_u) * fraction_v,
        fraction_u * fraction_v};
    const auto channel = [&](Color TextureSample::* color,
                             std::int64_t Color::* component) noexcept {
      double value = 0.0;
      for (std::size_t i = 0U; i < samples.size(); ++i) {
        value += static_cast<double>((samples[i].*color).*component) * weights[i];
      }
      return static_cast<std::int64_t>(std::lround(value));
    };
    ++precise_stats.bilinear_fragments;
    return TextureSample{Color{
        channel(&TextureSample::color_5bit, &Color::red),
        channel(&TextureSample::color_5bit, &Color::green),
        channel(&TextureSample::color_5bit, &Color::blue)}, false, semi};
  }

  [[nodiscard]] static Color raw_texture_color(Color texture_5bit) noexcept {
    return Color{texture_5bit.red << 3u, texture_5bit.green << 3u,
                 texture_5bit.blue << 3u};
  }

  [[nodiscard]] static Color modulate(Color texture_5bit,
                                      Color vertex) noexcept {
    const auto multiply = [](std::int64_t texture_channel_5bit,
                             std::int64_t vertex_channel) -> std::int64_t {
      // The GPU multiplies the native five-bit texel by the eight-bit
      // modulation color. Dithering and saturation happen only when packing.
      return (texture_channel_5bit * vertex_channel) >> 4u;
    };
    return Color{multiply(texture_5bit.red, vertex.red),
                 multiply(texture_5bit.green, vertex.green),
                 multiply(texture_5bit.blue, vertex.blue)};
  }

  void write_pixel(const gpu::RenderStateSnapshot &state, std::int64_t x,
                   std::int64_t y, Color color, bool transparent, bool blend,
                   bool dither, bool source_mask_bit) noexcept {
    if (transparent) {
      return;
    }
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(kVramWidth) +
        static_cast<std::size_t>(x);
    const std::uint16_t old_value = vram[index];
    if (state.mask.check_mask_bit && (old_value & 0x8000u) != 0u) {
      return;
    }

    std::uint16_t value = pack_color(color, x, y, dither);
    if (blend) {
      value = blend_color(value, old_value, state.draw_mode.semi_transparency);
    }
    if (state.mask.set_mask_bit || source_mask_bit) {
      value = static_cast<std::uint16_t>(value | 0x8000u);
    }
    vram[index] = value;
  }

  void draw_triangle(const gpu::RenderStateSnapshot &state,
                     const std::optional<gpu::ClutAttribute> &clut,
                     const Triangle &input, bool gouraud, bool textured,
                     bool raw_texture, bool semi_transparent) noexcept {
    Triangle triangle = input;
    std::int64_t area =
        cross(triangle.vertices[0].point, triangle.vertices[1].point,
              triangle.vertices[2].point);
    if (area == 0) {
      return;
    }
    if (area < 0) {
      std::swap(triangle.vertices[1], triangle.vertices[2]);
      area = -area;
    }

    const Bounds bounds = triangle_bounds(state, triangle.vertices);
    if (bounds.empty) {
      return;
    }
    const bool dither = state.draw_mode.dither &&
                        ((gouraud && !textured) || (textured && !raw_texture));
    const std::array<AttributePlane, 3> color_planes{{
        make_attribute_plane(triangle.vertices, triangle.vertices[0].color.red,
                             triangle.vertices[1].color.red,
                             triangle.vertices[2].color.red, area),
        make_attribute_plane(triangle.vertices,
                             triangle.vertices[0].color.green,
                             triangle.vertices[1].color.green,
                             triangle.vertices[2].color.green, area),
        make_attribute_plane(triangle.vertices, triangle.vertices[0].color.blue,
                             triangle.vertices[1].color.blue,
                             triangle.vertices[2].color.blue, area),
    }};
    const std::array<AttributePlane, 2> uv_planes{{
        make_attribute_plane(triangle.vertices, triangle.vertices[0].uv.u,
                             triangle.vertices[1].uv.u,
                             triangle.vertices[2].uv.u, area),
        make_attribute_plane(triangle.vertices, triangle.vertices[0].uv.v,
                             triangle.vertices[1].uv.v,
                             triangle.vertices[2].uv.v, area),
    }};
    for (std::int64_t y = bounds.top; y <= bounds.bottom; ++y) {
      for (std::int64_t x = bounds.left; x <= bounds.right; ++x) {
        const std::int64_t weight_zero =
            edge(triangle.vertices[1].point, triangle.vertices[2].point, x, y);
        const std::int64_t weight_one =
            edge(triangle.vertices[2].point, triangle.vertices[0].point, x, y);
        const std::int64_t weight_two =
            edge(triangle.vertices[0].point, triangle.vertices[1].point, x, y);
        if (weight_zero < 0 || weight_one < 0 || weight_two < 0 ||
            (weight_zero == 0 && !top_left_edge(triangle.vertices[1].point,
                                                triangle.vertices[2].point)) ||
            (weight_one == 0 && !top_left_edge(triangle.vertices[2].point,
                                               triangle.vertices[0].point)) ||
            (weight_two == 0 && !top_left_edge(triangle.vertices[0].point,
                                               triangle.vertices[1].point))) {
          continue;
        }

        Color color = triangle.vertices[0].color;
        if (gouraud) {
          color = Color{sample_attribute_plane(color_planes[0], x, y),
                        sample_attribute_plane(color_planes[1], x, y),
                        sample_attribute_plane(color_planes[2], x, y)};
        }

        bool transparent = false;
        bool texture_semi_transparent = false;
        if (textured) {
          const Uv uv{sample_attribute_plane(uv_planes[0], x, y),
                      sample_attribute_plane(uv_planes[1], x, y)};
          const TextureSample sample = sample_texture(state, clut, uv.u, uv.v);
          transparent = sample.transparent;
          texture_semi_transparent = sample.semi_transparent;
          color = raw_texture ? raw_texture_color(sample.color_5bit)
                              : modulate(sample.color_5bit, color);
        }
        const bool blend =
            semi_transparent && (!textured || texture_semi_transparent);
        write_pixel(state, x, y, color, transparent, blend, dither,
                    textured && texture_semi_transparent);
      }
    }
  }

  [[nodiscard]] EventResult consume_precise_polygon(
      const gpu::PolygonEvent &event,
      std::span<const PreciseVertex> vertices,
      PreciseCullingPolicy culling,
      TextureFilterMode filtering,
      WorkBudget &budget) noexcept {
    const std::size_t expected = event.quad ? 6u : 3u;
    const bool textured = event.textured && !event.state.draw_mode.texture_disable;
    if (event.kind != gpu::EventKind::Polygon || vertices.size() != expected ||
        !valid_common_state(event.state) ||
        !valid_texture_state(event.state, textured, event.clut) ||
        (filtering != TextureFilterMode::NearestNative &&
         filtering != TextureFilterMode::BilinearTransparencySafe)) {
      return reject(RasterError::MalformedEvent);
    }
    std::uint64_t work_units = 0U;
    std::array<Bounds, 2> bounds{};
    const std::array<std::array<std::size_t, 3U>, 2U> native_indices{{
        {{0U, 1U, 2U}}, {{1U, 2U, 3U}}}};
    for (std::size_t triangle = 0U; triangle < expected / 3U; ++triangle) {
      const PreciseVertex* v = vertices.data() + triangle * 3U;
      const bool sign = std::signbit(v[0].w);
      for (std::size_t corner = 0U; corner < 3U; ++corner) {
        if (!std::isfinite(v[corner].x) || !std::isfinite(v[corner].y) ||
            !std::isfinite(v[corner].z) || !std::isfinite(v[corner].w) ||
            !std::isfinite(v[corner].u) || !std::isfinite(v[corner].v) ||
            v[corner].w == 0.0 || std::signbit(v[corner].w) != sign ||
            v[corner].storage_generation == 0U ||
            v[corner].origin_generation == 0U) {
          return reject(RasterError::MalformedEvent);
        }
      }
      if (culling == PreciseCullingPolicy::NativeHardware) {
        const Triangle native = make_triangle(
            event, native_indices[triangle], textured);
        if (!valid_triangle_size(native.vertices)) continue;
      }
      const double min_x = std::min({v[0].x, v[1].x, v[2].x});
      const double max_x = std::max({v[0].x, v[1].x, v[2].x});
      const double min_y = std::min({v[0].y, v[1].y, v[2].y});
      const double max_y = std::max({v[0].y, v[1].y, v[2].y});
      if (min_x < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
          max_x > static_cast<double>(std::numeric_limits<std::int32_t>::max()) ||
          min_y < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
          max_y > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return reject(RasterError::MalformedEvent);
      }
      bounds[triangle] = clipped_bounds(
          event.state, static_cast<std::int64_t>(std::floor(min_x)),
          static_cast<std::int64_t>(std::floor(min_y)),
          static_cast<std::int64_t>(std::ceil(max_x)) - 1,
          static_cast<std::int64_t>(std::ceil(max_y)) - 1);
      if (work_units > std::numeric_limits<std::uint64_t>::max() -
                           bounds[triangle].work_units) {
        return reject(RasterError::WorkLimitExceeded);
      }
      work_units += bounds[triangle].work_units;
    }
    EventResult result = accept(budget, work_units,
        work_units == 0U ? EventStatus::Ignored : EventStatus::Applied);
    if (!result.accepted()) return result;

    const auto edge_d = [](const PreciseVertex& a, const PreciseVertex& b,
                           double x, double y) noexcept {
      return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    const auto top_left_d = [](const PreciseVertex& a,
                               const PreciseVertex& b) noexcept {
      return b.y < a.y || (b.y == a.y && b.x > a.x);
    };
    for (std::size_t triangle = 0U; triangle < expected / 3U; ++triangle) {
      std::array<PreciseVertex, 3> v{
          vertices[triangle * 3U], vertices[triangle * 3U + 1U],
          vertices[triangle * 3U + 2U]};
      double area = edge_d(v[0], v[1], v[2].x, v[2].y);
      if (area == 0.0 || bounds[triangle].empty) continue;
      if (area < 0.0) { std::swap(v[1], v[2]); area = -area; }
      for (std::int64_t y = bounds[triangle].top; y <= bounds[triangle].bottom; ++y) {
        for (std::int64_t x = bounds[triangle].left; x <= bounds[triangle].right; ++x) {
          const double weight_a = edge_d(
              v[1], v[2], static_cast<double>(x), static_cast<double>(y));
          const double weight_b = edge_d(
              v[2], v[0], static_cast<double>(x), static_cast<double>(y));
          const double weight_c = edge_d(
              v[0], v[1], static_cast<double>(x), static_cast<double>(y));
          if (weight_a < 0.0 || weight_b < 0.0 || weight_c < 0.0 ||
              (weight_a == 0.0 && !top_left_d(v[1], v[2])) ||
              (weight_b == 0.0 && !top_left_d(v[2], v[0])) ||
              (weight_c == 0.0 && !top_left_d(v[0], v[1]))) {
            continue;
          }
          const double a = weight_a / area;
          const double b = weight_b / area;
          const double c = weight_c / area;
          ++precise_stats.precise_fragments;
          const double denominator = a / v[0].w + b / v[1].w + c / v[2].w;
          if (!std::isfinite(denominator) || denominator == 0.0) continue;
          const double pa = (a / v[0].w) / denominator;
          const double pb = (b / v[1].w) / denominator;
          const double pc = (c / v[2].w) / denominator;
          const auto channel = [&](unsigned shift) noexcept {
            return static_cast<std::int64_t>(std::clamp(
                pa * static_cast<double>((v[0].color_rgba >> shift) & 0xffU) +
                pb * static_cast<double>((v[1].color_rgba >> shift) & 0xffU) +
                pc * static_cast<double>((v[2].color_rgba >> shift) & 0xffU),
                0.0, 255.0));
          };
          Color color{channel(0U), channel(8U), channel(16U)};
          bool transparent = false;
          bool texture_semi = false;
          if (textured) {
            const auto coordinate = [&](bool select_v) noexcept {
              const double value = pa * (select_v ? v[0].v : v[0].u) +
                  pb * (select_v ? v[1].v : v[1].u) +
                  pc * (select_v ? v[2].v : v[2].u);
              return value * 255.0;
            };
            const TextureSample sample = sample_texture_filtered(
                event.state, event.clut, coordinate(false), coordinate(true),
                filtering);
            transparent = sample.transparent;
            texture_semi = sample.semi_transparent;
            color = event.raw_texture ? raw_texture_color(sample.color_5bit)
                                      : modulate(sample.color_5bit, color);
          }
          const bool blend = event.semi_transparent && (!textured || texture_semi);
          const bool dither = event.state.draw_mode.dither &&
              ((event.gouraud && !textured) || (textured && !event.raw_texture));
          write_pixel(event.state, x, y, color, transparent, blend, dither,
                      textured && texture_semi);
        }
      }
    }
    if (result.status == EventStatus::Applied &&
        vram_generation != std::numeric_limits<std::uint64_t>::max()) {
      ++vram_generation;
    }
    return result;
  }

  [[nodiscard]] Triangle make_triangle(const gpu::PolygonEvent &event,
                                       const std::array<std::size_t, 3> &index,
                                       bool effective_textured) const noexcept {
    Triangle result{};
    for (std::size_t output_index = 0; output_index < index.size();
         ++output_index) {
      const std::size_t input_index = index[output_index];
      result.vertices[output_index].point =
          with_offset(event.vertices[input_index], event.state.draw_offset);
      result.vertices[output_index].color =
          event.gouraud ? vertex_color(event.colors[input_index])
                        : vertex_color(event.first_color);
      if (effective_textured) {
        result.vertices[output_index].uv =
            texture_uv(event.texture_coordinates[input_index]);
      }
    }
    return result;
  }

  [[nodiscard]] EventResult consume_polygon(const gpu::PolygonEvent &event,
                                            WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::Polygon ||
        (event.quad && event.vertex_count != 4u) ||
        (!event.quad && event.vertex_count != 3u) ||
        !valid_common_state(event.state)) {
      return reject(RasterError::MalformedEvent);
    }
    if (event.gouraud) {
      for (std::size_t index = 0; index < event.vertex_count; ++index) {
        if (!event.color_present[index]) {
          return reject(RasterError::MalformedEvent);
        }
      }
    }

    const bool effective_textured =
        event.textured && !event.state.draw_mode.texture_disable;
    if (!valid_texture_state(event.state, effective_textured, event.clut)) {
      return reject(effective_textured ? RasterError::InvalidTexture
                                       : RasterError::MalformedEvent);
    }
    if (effective_textured) {
      for (std::size_t index = 0; index < event.vertex_count; ++index) {
        if (!event.texture_coordinate_present[index]) {
          return reject(RasterError::MalformedEvent);
        }
      }
    }

    const std::array<std::array<std::size_t, 3>, 2> indices{{
        {{0, 1, 2}},
        {{1, 2, 3}},
    }};
    const std::size_t triangle_count = event.quad ? 2u : 1u;
    std::array<Triangle, 2> triangles{};
    std::array<bool, 2> render_triangle{};
    std::uint64_t work_units = 0;
    for (std::size_t triangle_index = 0; triangle_index < triangle_count;
         ++triangle_index) {
      triangles[triangle_index] =
          make_triangle(event, indices[triangle_index], effective_textured);
      render_triangle[triangle_index] =
          valid_triangle_size(triangles[triangle_index].vertices);
      if (!render_triangle[triangle_index]) {
        continue;
      }
      const Bounds bounds =
          triangle_bounds(event.state, triangles[triangle_index].vertices);
      if (work_units >
          std::numeric_limits<std::uint64_t>::max() - bounds.work_units) {
        return reject(RasterError::WorkLimitExceeded);
      }
      work_units += bounds.work_units;
    }

    const bool any_triangle = std::any_of(
        render_triangle.begin(), render_triangle.begin() + triangle_count,
        [](bool value) { return value; });
    EventResult result =
        accept(budget, work_units,
               any_triangle ? EventStatus::Applied : EventStatus::Ignored);
    if (!result.accepted()) {
      return result;
    }
    for (std::size_t triangle_index = 0; triangle_index < triangle_count;
         ++triangle_index) {
      if (!render_triangle[triangle_index]) {
        continue;
      }
      draw_triangle(event.state, event.clut, triangles[triangle_index],
                    event.gouraud, effective_textured, event.raw_texture,
                    event.semi_transparent);
    }
    return result;
  }

  void draw_line_segment(const gpu::RenderStateSnapshot &state, Point first,
                         Point second, Color first_color, Color second_color,
                         bool gouraud, bool semi_transparent) noexcept {
    const std::int64_t delta_x = abs_i64(second.x - first.x);
    const std::int64_t delta_y = abs_i64(second.y - first.y);
    const std::int64_t steps = std::max(delta_x, delta_y);
    if (steps == 0) {
      if (first.x >= 0 && first.x < kVramWidth && first.y >= 0 &&
          first.y < kVramHeight &&
          first.x >= static_cast<std::int64_t>(state.draw_area.left) &&
          first.x <= static_cast<std::int64_t>(state.draw_area.right) &&
          first.y >= static_cast<std::int64_t>(state.draw_area.top) &&
          first.y <= static_cast<std::int64_t>(state.draw_area.bottom)) {
        write_pixel(state, first.x, first.y, first_color, false,
                    semi_transparent, state.draw_mode.dither, false);
      }
      return;
    }

    // The PS1 line unit normalizes endpoint order by X. Equal-X segments are
    // processed bottom-to-top. Keep attributes attached to their vertices.
    if (first.x > second.x || (first.x == second.x && first.y < second.y)) {
      std::swap(first, second);
      std::swap(first_color, second_color);
    }

    constexpr std::int64_t kCoordinateScale = INT64_C(1) << 32u;
    constexpr std::int64_t kCoordinateHalf = kCoordinateScale / 2;
    constexpr std::int64_t kCoordinateEpsilon = 1024;
    constexpr std::int64_t kAttributeScale = INT64_C(1) << 12u;
    constexpr std::int64_t kAttributeHalf = kAttributeScale / 2;

    const std::int64_t x_step = line_coordinate_step(second.x - first.x, steps);
    const std::int64_t y_step = line_coordinate_step(second.y - first.y, steps);
    std::int64_t fixed_x =
        first.x * kCoordinateScale + kCoordinateHalf - kCoordinateEpsilon;
    std::int64_t fixed_y = first.y * kCoordinateScale + kCoordinateHalf -
                           (y_step < 0 ? kCoordinateEpsilon : 0);

    Color fixed_color{first_color.red * kAttributeScale + kAttributeHalf,
                      first_color.green * kAttributeScale + kAttributeHalf,
                      first_color.blue * kAttributeScale + kAttributeHalf};
    const Color color_step =
        gouraud ? Color{line_attribute_step(first_color.red, second_color.red,
                                            steps),
                        line_attribute_step(first_color.green,
                                            second_color.green, steps),
                        line_attribute_step(first_color.blue, second_color.blue,
                                            steps)}
                : Color{};

    for (std::int64_t index = 0; index <= steps; ++index) {
      const std::int64_t x = fixed_floor(fixed_x, kCoordinateScale);
      const std::int64_t y = fixed_floor(fixed_y, kCoordinateScale);
      const Color color =
          gouraud ? Color{fixed_floor(fixed_color.red, kAttributeScale),
                          fixed_floor(fixed_color.green, kAttributeScale),
                          fixed_floor(fixed_color.blue, kAttributeScale)}
                  : first_color;
      if (x >= 0 && x < kVramWidth && y >= 0 && y < kVramHeight &&
          x >= static_cast<std::int64_t>(state.draw_area.left) &&
          x <= static_cast<std::int64_t>(state.draw_area.right) &&
          y >= static_cast<std::int64_t>(state.draw_area.top) &&
          y <= static_cast<std::int64_t>(state.draw_area.bottom)) {
        write_pixel(state, x, y, color, false, semi_transparent,
                    state.draw_mode.dither, false);
      }
      fixed_x += x_step;
      fixed_y += y_step;
      fixed_color.red += color_step.red;
      fixed_color.green += color_step.green;
      fixed_color.blue += color_step.blue;
    }
  }

  [[nodiscard]] EventResult consume_line(const gpu::LineEvent &event,
                                         WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::Line || event.vertices.size() < 2u ||
        event.vertices.size() > limits.max_polyline_vertices ||
        (!event.polyline && event.vertices.size() != 2u) ||
        !valid_common_state(event.state)) {
      return reject(RasterError::MalformedEvent);
    }
    if (event.gouraud && event.colors.size() != event.vertices.size()) {
      return reject(RasterError::MalformedEvent);
    }

    std::uint64_t work_units = 0;
    bool any_segment = false;
    for (std::size_t index = 1; index < event.vertices.size(); ++index) {
      const Point first =
          with_offset(event.vertices[index - 1], event.state.draw_offset);
      const Point second =
          with_offset(event.vertices[index], event.state.draw_offset);
      if (!valid_primitive_edge(first, second)) {
        continue;
      }
      any_segment = true;
      const std::uint64_t segment_work = static_cast<std::uint64_t>(
          std::max(abs_i64(second.x - first.x), abs_i64(second.y - first.y)) +
          1);
      if (work_units >
          std::numeric_limits<std::uint64_t>::max() - segment_work) {
        return reject(RasterError::WorkLimitExceeded);
      }
      work_units += segment_work;
    }

    EventResult result =
        accept(budget, work_units,
               any_segment ? EventStatus::Applied : EventStatus::Ignored);
    if (!result.accepted()) {
      return result;
    }
    for (std::size_t index = 1; index < event.vertices.size(); ++index) {
      const Point first =
          with_offset(event.vertices[index - 1], event.state.draw_offset);
      const Point second =
          with_offset(event.vertices[index], event.state.draw_offset);
      if (!valid_primitive_edge(first, second)) {
        continue;
      }
      const Color first_color = event.gouraud
                                    ? vertex_color(event.colors[index - 1])
                                    : vertex_color(event.first_color);
      const Color second_color =
          event.gouraud ? vertex_color(event.colors[index]) : first_color;
      draw_line_segment(event.state, first, second, first_color, second_color,
                        event.gouraud, event.semi_transparent);
    }
    return result;
  }

  [[nodiscard]] Bounds rectangle_bounds(const gpu::RenderStateSnapshot &state,
                                        const gpu::Vertex &origin,
                                        std::uint16_t width,
                                        std::uint16_t height) const noexcept {
    const Point adjusted = with_offset(origin, state.draw_offset);
    return clipped_bounds(state, adjusted.x, adjusted.y,
                          adjusted.x + static_cast<std::int64_t>(width) - 1,
                          adjusted.y + static_cast<std::int64_t>(height) - 1);
  }

  [[nodiscard]] EventResult consume_rectangle(const gpu::RectangleEvent &event,
                                              WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::Rectangle || event.size_code > 3u ||
        event.width > kVramWidth || event.height > kVramHeight ||
        !valid_common_state(event.state)) {
      return reject(RasterError::MalformedEvent);
    }
    const bool effective_textured =
        event.textured && !event.state.draw_mode.texture_disable;
    if (!valid_texture_state(event.state, effective_textured, event.clut)) {
      return reject(effective_textured ? RasterError::InvalidTexture
                                       : RasterError::MalformedEvent);
    }
    if (effective_textured && !event.texture_coordinate.has_value()) {
      return reject(RasterError::MalformedEvent);
    }

    const Bounds bounds =
        rectangle_bounds(event.state, event.origin, event.width, event.height);
    EventResult result = accept(budget, bounds.work_units);
    if (!result.accepted()) {
      return result;
    }
    const Point adjusted = with_offset(event.origin, event.state.draw_offset);
    for (std::int64_t y = bounds.top; y <= bounds.bottom; ++y) {
      for (std::int64_t x = bounds.left; x <= bounds.right; ++x) {
        Color color = vertex_color(event.color);
        bool transparent = false;
        bool texture_semi_transparent = false;
        if (effective_textured) {
          const std::int64_t local_x = x - adjusted.x;
          const std::int64_t local_y = y - adjusted.y;
          const std::int64_t texture_x =
              event.state.draw_mode.rectangle_flip_x ? -local_x : local_x;
          const std::int64_t texture_y =
              event.state.draw_mode.rectangle_flip_y ? -local_y : local_y;
          const gpu::TextureCoordinate &coordinate = *event.texture_coordinate;
          const TextureSample sample = sample_texture(
              event.state, event.clut,
              static_cast<std::int64_t>(coordinate.u) + texture_x,
              static_cast<std::int64_t>(coordinate.v) + texture_y);
          transparent = sample.transparent;
          texture_semi_transparent = sample.semi_transparent;
          color = event.raw_texture ? raw_texture_color(sample.color_5bit)
                                    : modulate(sample.color_5bit, color);
        }
        const bool blend = event.semi_transparent &&
                           (!effective_textured || texture_semi_transparent);
        // Rectangle commands never dither, including modulated sprites.
        write_pixel(event.state, x, y, color, transparent, blend, false,
                    effective_textured && texture_semi_transparent);
      }
    }
    return result;
  }

  [[nodiscard]] EventResult consume_fill(const gpu::VramFillEvent &event,
                                         WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::VramFill ||
        !valid_vram_origin(event.origin) || !valid_extent(event.extent)) {
      return reject(RasterError::MalformedEvent);
    }
    const std::uint64_t work_units =
        area_work(event.extent.width, event.extent.height);
    EventResult result = accept(budget, work_units);
    if (!result.accepted()) {
      return result;
    }
    const std::uint16_t value = color_to_word(event.color);
    for (std::uint32_t y = 0; y < event.extent.height; ++y) {
      for (std::uint32_t x = 0; x < event.extent.width; ++x) {
        write_wrapped(vram, static_cast<std::int64_t>(event.origin.x) + x,
                      static_cast<std::int64_t>(event.origin.y) + y, value);
      }
    }
    return result;
  }

  [[nodiscard]] EventResult consume_copy(const gpu::VramCopyEvent &event,
                                         WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::VramCopy ||
        !valid_vram_origin(event.source) ||
        !valid_vram_origin(event.destination) ||
        !valid_transfer_extent(event.extent)) {
      return reject(RasterError::MalformedEvent);
    }
    const std::uint64_t work_units =
        area_work(event.extent.width, event.extent.height);
    EventResult result = accept(budget, work_units);
    if (!result.accepted()) {
      return result;
    }

    std::array<VramWord, kVramWidth> source_row{};
    for (std::uint32_t y = 0; y < event.extent.height; ++y) {
      for (std::uint32_t x = 0; x < event.extent.width; ++x) {
        source_row[static_cast<std::size_t>(x)] =
            read_wrapped(vram, static_cast<std::int64_t>(event.source.x) + x,
                         static_cast<std::int64_t>(event.source.y) + y);
      }
      for (std::uint32_t x = 0; x < event.extent.width; ++x) {
        const std::int64_t destination_x =
            static_cast<std::int64_t>(event.destination.x) + x;
        const std::int64_t destination_y =
            static_cast<std::int64_t>(event.destination.y) + y;
        const VramWord old_value =
            read_wrapped(vram, destination_x, destination_y);
        if (event.mask.check_mask_bit && (old_value & 0x8000u) != 0u) {
          continue;
        }
        VramWord value = source_row[static_cast<std::size_t>(x)];
        if (event.mask.set_mask_bit) {
          value = static_cast<VramWord>(value | 0x8000u);
        }
        write_wrapped(vram, destination_x, destination_y, value);
      }
    }
    return result;
  }

  [[nodiscard]] EventResult consume_upload(const gpu::VramUploadEvent &event,
                                           WorkBudget &budget) noexcept {
    if (event.kind != gpu::EventKind::VramUpload ||
        !valid_vram_origin(event.destination) ||
        !valid_transfer_extent(event.extent)) {
      return reject(RasterError::MalformedEvent);
    }
    const std::size_t pixel_count =
        static_cast<std::size_t>(event.extent.width) * event.extent.height;
    if (event.pixel_count != pixel_count ||
        event.padded_word_count != expected_words(pixel_count) ||
        event.data_words.size() != event.padded_word_count ||
        event.final_word_valid_halfwords !=
            static_cast<std::uint8_t>((pixel_count & 1u) != 0u ? 1u : 2u)) {
      return reject(RasterError::TransferDataMismatch);
    }
    const std::uint64_t work_units = static_cast<std::uint64_t>(pixel_count);
    EventResult result = accept(budget, work_units);
    if (!result.accepted()) {
      return result;
    }

    std::size_t index = 0;
    for (std::uint32_t y = 0; y < event.extent.height; ++y) {
      for (std::uint32_t x = 0; x < event.extent.width; ++x, ++index) {
        const std::int64_t destination_x =
            static_cast<std::int64_t>(event.destination.x) + x;
        const std::int64_t destination_y =
            static_cast<std::int64_t>(event.destination.y) + y;
        const VramWord old_value =
            read_wrapped(vram, destination_x, destination_y);
        if (event.mask.check_mask_bit && (old_value & 0x8000u) != 0u) {
          continue;
        }
        VramWord value = transfer_half(event.data_words[index / 2u], index);
        if (event.mask.set_mask_bit) {
          value = static_cast<VramWord>(value | 0x8000u);
        }
        write_wrapped(vram, destination_x, destination_y, value);
      }
    }
    return result;
  }

  [[nodiscard]] EventResult
  consume_download(const gpu::VramDownloadEvent &event,
                   WorkBudget &budget) const noexcept {
    if (event.kind != gpu::EventKind::VramDownload ||
        !valid_vram_origin(event.source) ||
        !valid_transfer_extent(event.extent)) {
      return reject(RasterError::MalformedEvent);
    }
    const std::size_t pixel_count =
        static_cast<std::size_t>(event.extent.width) * event.extent.height;
    if (event.pixel_count != pixel_count ||
        event.padded_word_count != expected_words(pixel_count) ||
        event.data_words.size() != event.padded_word_count ||
        event.final_word_valid_halfwords !=
            static_cast<std::uint8_t>((pixel_count & 1u) != 0u ? 1u : 2u)) {
      return reject(RasterError::TransferDataMismatch);
    }
    EventResult result = accept(budget, static_cast<std::uint64_t>(pixel_count),
                                EventStatus::Ignored);
    if (result.accepted()) {
      result.download_words = std::span<const std::uint32_t>(
          event.data_words.data(), event.data_words.size());
    }
    return result;
  }

  [[nodiscard]] EventResult
  consume_environment(const gpu::EnvironmentEvent &event,
                      WorkBudget &budget) const noexcept {
    if (event.kind != gpu::EventKind::Environment ||
        !valid_environment(event.environment) ||
        !valid_common_state(event.state)) {
      return reject(RasterError::MalformedEvent);
    }
    return accept(budget, 1u, EventStatus::Ignored);
  }

  template <typename Marker>
  [[nodiscard]] EventResult consume_marker(const Marker &event,
                                           WorkBudget &budget) const noexcept {
    const Marker canonical{};
    if (event.kind != canonical.kind) {
      return reject(RasterError::MalformedEvent);
    }
    return accept(budget, 1u, EventStatus::Ignored);
  }

  [[nodiscard]] EventResult consume(const gpu::Event &event,
                                    WorkBudget &budget) noexcept {
    EventResult result{};
    if (const auto *polygon = std::get_if<gpu::PolygonEvent>(&event)) {
      result = consume_polygon(*polygon, budget);
    } else if (const auto *line = std::get_if<gpu::LineEvent>(&event)) {
      result = consume_line(*line, budget);
    } else if (const auto *rectangle = std::get_if<gpu::RectangleEvent>(&event)) {
      result = consume_rectangle(*rectangle, budget);
    } else if (const auto *fill = std::get_if<gpu::VramFillEvent>(&event)) {
      result = consume_fill(*fill, budget);
    } else if (const auto *copy = std::get_if<gpu::VramCopyEvent>(&event)) {
      result = consume_copy(*copy, budget);
    } else if (const auto *upload = std::get_if<gpu::VramUploadEvent>(&event)) {
      result = consume_upload(*upload, budget);
    } else if (const auto *download = std::get_if<gpu::VramDownloadEvent>(&event)) {
      return consume_download(*download, budget);
    } else if (const auto *environment = std::get_if<gpu::EnvironmentEvent>(&event)) {
      return consume_environment(*environment, budget);
    } else if (const auto *flush = std::get_if<gpu::CacheFlushEvent>(&event)) {
      return consume_marker(*flush, budget);
    } else if (const auto *interrupt = std::get_if<gpu::InterruptRequestEvent>(&event)) {
      return consume_marker(*interrupt, budget);
    } else {
      return reject(RasterError::MalformedEvent);
    }
    if (result.status == EventStatus::Applied &&
        vram_generation != std::numeric_limits<std::uint64_t>::max()) {
      ++vram_generation;
    }
    return result;
  }
};

namespace {

[[nodiscard]] EventResult backend_consume_event(void *context,
                                                const gpu::Event *event,
                                                WorkBudget *budget) noexcept {
  if (context == nullptr || event == nullptr) {
    return EventResult{
        EventStatus::Rejected, RasterError::MalformedEvent, 0, {}};
  }
  ReferenceRasterizer *rasterizer = static_cast<ReferenceRasterizer *>(context);
  if (budget == nullptr) {
    return rasterizer->consume(*event);
  }
  return rasterizer->consume(*event, *budget);
}

[[nodiscard]] std::span<const VramWord>
backend_read_vram(void *context) noexcept {
  if (context == nullptr) {
    return {};
  }
  return static_cast<ReferenceRasterizer *>(context)->vram_words();
}

[[nodiscard]] std::uint64_t backend_digest(void *context) noexcept {
  if (context == nullptr) {
    return 0;
  }
  return static_cast<ReferenceRasterizer *>(context)->digest();
}

[[nodiscard]] PresentationResult
backend_plan_presentation(void *context,
                          const PresentationRequest *request) noexcept {
  static_cast<void>(context);
  if (request == nullptr) {
    return PresentationResult{};
  }
  return PresentationMapper::map(*request);
}

} // namespace

PresentationResult
PresentationMapper::map(const PresentationRequest &request) noexcept {
  PresentationResult result{};
  if (request.output_width == 0u || request.output_height == 0u ||
      request.source.width == 0u || request.source.height == 0u ||
      request.source.x >= kVramWidth || request.source.y >= kVramHeight ||
      static_cast<std::uint32_t>(request.source.x) + request.source.width >
          kVramWidth ||
      static_cast<std::uint32_t>(request.source.y) + request.source.height >
          kVramHeight) {
    return result;
  }

  std::uint32_t aspect_width = 4u;
  std::uint32_t aspect_height = 3u;
  bool identity_4x3 = false;
  switch (request.mode) {
  case PresentationMode::Identity4x3:
    identity_4x3 = true;
    break;
  case PresentationMode::Generic16x9:
    aspect_width = 16u;
    aspect_height = 9u;
    break;
  case PresentationMode::Generic21x9:
    aspect_width = 21u;
    aspect_height = 9u;
    break;
  case PresentationMode::Generic32x9:
    aspect_width = 32u;
    aspect_height = 9u;
    break;
  case PresentationMode::Custom:
    aspect_width = request.custom_aspect_width;
    aspect_height = request.custom_aspect_height;
    break;
  default:
    return result;
  }
  if (aspect_width == 0u || aspect_height == 0u) {
    return result;
  }

  std::uint64_t destination_width = request.output_width;
  std::uint64_t destination_height = request.output_height;
  if (destination_width * aspect_height > destination_height * aspect_width) {
    destination_width = (destination_height * aspect_width) / aspect_height;
  } else {
    destination_height = (destination_width * aspect_height) / aspect_width;
  }
  if (destination_width == 0u || destination_height == 0u) {
    return result;
  }

  result.accepted = true;
  result.error = RasterError::None;
  result.plan.source = request.source;
  result.plan.destination_width = static_cast<std::uint32_t>(destination_width);
  result.plan.destination_height =
      static_cast<std::uint32_t>(destination_height);
  result.plan.destination_x = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(request.output_width) - destination_width) /
      2u);
  result.plan.destination_y = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(request.output_height) - destination_height) /
      2u);
  result.plan.aspect_width = aspect_width;
  result.plan.aspect_height = aspect_height;
  result.plan.identity_4x3 = identity_4x3;
  return result;
}

ScanoutResult ScanoutMapper::plan(const ScanoutRequest &request) noexcept {
  ScanoutResult result{};
  const gpu::DisplayTiming &display = request.display;
  const std::uint16_t width = display.resolution_368
                                  ? 368u
                                  : std::array<std::uint16_t, 4>{256u, 320u, 512u, 640u}
                                        [display.horizontal_resolution & 3u];
  const bool full_height = display.vertical_resolution_480 && display.interlaced;
  const bool field_requested = request.field == ScanoutField::Even ||
                               request.field == ScanoutField::Odd;
  if (request.output_width == 0u || request.output_height == 0u ||
      display.display_start_x >= kVramWidth ||
      display.display_start_y >= kVramHeight ||
      display.horizontal_resolution > 3u ||
      display.horizontal_start >= display.horizontal_end ||
      display.vertical_start >= display.vertical_end ||
      (display.interlaced != field_requested) ||
      (!display.interlaced && request.field != ScanoutField::Progressive)) {
    return result;
  }

  // A 480-line interlaced frame is exposed as one bounded 240-line field.
  // The vertical-resolution bit has no 480-line scanout meaning without
  // interlace, matching GPUSTAT's effective vertical-resolution rule.
  const std::uint16_t height = 240u;
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  if (pixels > std::numeric_limits<std::size_t>::max() / 3u) {
    return result;
  }
  const PresentationResult presentation = PresentationMapper::map(
      PresentationRequest{request.output_width, request.output_height,
                          VramRect{0u, 0u, width, height},
                          request.presentation_mode, request.custom_aspect_width,
                          request.custom_aspect_height});
  if (!presentation.accepted) {
    return result;
  }

  result.accepted = true;
  result.error = RasterError::None;
  result.plan.display_enabled = display.display_enabled;
  result.plan.pixel_format = display.display_depth_24
                                 ? ScanoutPixelFormat::PackedRgb888
                                 : ScanoutPixelFormat::Rgb555;
  result.plan.field = request.field;
  result.plan.vram_start_x_words = display.display_start_x;
  result.plan.vram_start_y = display.display_start_y;
  result.plan.logical_width = width;
  result.plan.logical_height = height;
  result.plan.source_line_step = full_height ? 2u : 1u;
  result.plan.source_field_offset =
      full_height && request.field == ScanoutField::Odd ? 1u : 0u;
  result.plan.horizontal_start = display.horizontal_start;
  result.plan.horizontal_end = display.horizontal_end;
  result.plan.vertical_start = display.vertical_start;
  result.plan.vertical_end = display.vertical_end;
  result.plan.required_rgb_bytes = pixels * 3u;
  result.plan.presentation = presentation.plan;
  return result;
}

EventResult ScanoutMapper::decode_rgb888(
    const ScanoutPlan &plan, std::span<const VramWord> vram,
    std::span<std::uint8_t> destination) noexcept {
  if (vram.size() != kVramWordCount || plan.logical_width == 0u ||
      plan.logical_height == 0u || plan.source_line_step == 0u ||
      plan.vram_start_x_words >= kVramWidth || plan.vram_start_y >= kVramHeight ||
      (plan.pixel_format != ScanoutPixelFormat::Rgb555 &&
       plan.pixel_format != ScanoutPixelFormat::PackedRgb888) ||
      plan.required_rgb_bytes != static_cast<std::size_t>(plan.logical_width) *
                                     plan.logical_height * 3u) {
    return EventResult{EventStatus::Rejected, RasterError::InvalidScanoutRequest};
  }
  if (destination.size() < plan.required_rgb_bytes) {
    return EventResult{EventStatus::Rejected, RasterError::ScanoutBufferTooSmall};
  }
  if (!plan.display_enabled) {
    std::fill_n(destination.begin(), static_cast<std::ptrdiff_t>(plan.required_rgb_bytes), 0u);
    return EventResult{EventStatus::Applied, RasterError::None,
                       static_cast<std::uint64_t>(plan.logical_width) *
                           plan.logical_height};
  }

  const auto row_byte_at = [&vram](std::uint32_t y,
                                   std::size_t byte_x) noexcept {
    constexpr std::size_t row_bytes = static_cast<std::size_t>(kVramWidth) * 2u;
    const std::size_t wrapped_byte_x = byte_x % row_bytes;
    const VramWord word = vram[static_cast<std::size_t>(y) * kVramWidth +
                               wrapped_byte_x / 2u];
    return static_cast<std::uint8_t>((wrapped_byte_x & 1u) == 0u ? word & 0xffu
                                                                 : word >> 8u);
  };
  std::size_t output = 0u;
  for (std::uint32_t y = 0; y < plan.logical_height; ++y) {
    const std::uint32_t source_y =
        (static_cast<std::uint32_t>(plan.vram_start_y) +
         plan.source_field_offset + y * plan.source_line_step) % kVramHeight;
    if (plan.pixel_format == ScanoutPixelFormat::PackedRgb888) {
      const std::size_t row_byte =
          static_cast<std::size_t>(plan.vram_start_x_words) * 2u;
      for (std::uint32_t x = 0; x < plan.logical_width; ++x) {
        const std::size_t pixel_byte = row_byte + static_cast<std::size_t>(x) * 3u;
        destination[output++] = row_byte_at(source_y, pixel_byte);
        destination[output++] = row_byte_at(source_y, pixel_byte + 1u);
        destination[output++] = row_byte_at(source_y, pixel_byte + 2u);
      }
    } else {
      for (std::uint32_t x = 0; x < plan.logical_width; ++x) {
        const std::size_t word_index = static_cast<std::size_t>(source_y) * kVramWidth +
            ((static_cast<std::uint32_t>(plan.vram_start_x_words) + x) % kVramWidth);
        const VramWord pixel = vram[word_index];
        const auto expand = [](std::uint16_t channel) noexcept {
          return static_cast<std::uint8_t>((channel << 3u) | (channel >> 2u));
        };
        destination[output++] = expand(pixel & 0x1fu);
        destination[output++] = expand((pixel >> 5u) & 0x1fu);
        destination[output++] = expand((pixel >> 10u) & 0x1fu);
      }
    }
  }
  return EventResult{EventStatus::Applied, RasterError::None,
                     static_cast<std::uint64_t>(plan.logical_width) *
                         plan.logical_height};
}

EventResult ScanoutMapper::upscale_edge_aware_rgb888(
    std::uint32_t source_width, std::uint32_t source_height,
    std::uint32_t output_width, std::uint32_t output_height,
    std::span<const std::uint8_t> source_rgb888,
    std::span<std::uint8_t> output_rgb888,
    UpscaleStats &stats) noexcept {
  stats = {};
  constexpr std::uint64_t kMaximumOutputPixels = 8192ULL * 8192ULL;
  const std::uint64_t source_pixels =
      static_cast<std::uint64_t>(source_width) * source_height;
  const std::uint64_t output_pixels =
      static_cast<std::uint64_t>(output_width) * output_height;
  const std::uint64_t source_bytes = source_pixels * 3U;
  const std::uint64_t output_bytes = output_pixels * 3U;
  const std::uintptr_t source_begin =
      reinterpret_cast<std::uintptr_t>(source_rgb888.data());
  const std::uintptr_t output_begin =
      reinterpret_cast<std::uintptr_t>(output_rgb888.data());
  const bool address_overflow =
      source_bytes > std::numeric_limits<std::uintptr_t>::max() - source_begin ||
      output_bytes > std::numeric_limits<std::uintptr_t>::max() - output_begin;
  const bool overlaps = !address_overflow &&
      source_begin < output_begin + output_bytes &&
      output_begin < source_begin + source_bytes;
  if (source_width == 0U || source_height == 0U || output_width == 0U ||
      output_height == 0U || output_width < source_width * 2ULL ||
      output_height < source_height * 2ULL ||
      output_pixels > kMaximumOutputPixels ||
      source_bytes != source_rgb888.size() ||
      output_bytes != output_rgb888.size() || address_overflow || overlaps) {
    return EventResult{EventStatus::Rejected,
                       RasterError::InvalidPresentationRequest};
  }
  stats.source_pixels = source_pixels;
  stats.output_pixels = output_pixels;
  stats.source_pixel_reads = output_pixels * 5U;
  stats.bytes_written = output_bytes;
  const auto pixel = [&](std::uint32_t x, std::uint32_t y) noexcept {
    return source_rgb888.data() +
        (static_cast<std::size_t>(y) * source_width + x) * 3U;
  };
  const auto equal = [&](const std::uint8_t* left,
                         const std::uint8_t* right) noexcept {
    ++stats.color_comparisons;
    return left[0] == right[0] && left[1] == right[1] &&
        left[2] == right[2];
  };
  for (std::uint32_t output_y = 0U; output_y < output_height; ++output_y) {
    const std::uint64_t mapped_y =
        static_cast<std::uint64_t>(output_y) * source_height;
    const std::uint32_t source_y =
        static_cast<std::uint32_t>(mapped_y / output_height);
    const bool lower = (mapped_y % output_height) * 2U >= output_height;
    const std::uint32_t up_y = source_y == 0U ? 0U : source_y - 1U;
    const std::uint32_t down_y =
        std::min(source_y + 1U, source_height - 1U);
    for (std::uint32_t output_x = 0U; output_x < output_width; ++output_x) {
      const std::uint64_t mapped_x =
          static_cast<std::uint64_t>(output_x) * source_width;
      const std::uint32_t source_x =
          static_cast<std::uint32_t>(mapped_x / output_width);
      const bool right = (mapped_x % output_width) * 2U >= output_width;
      const std::uint32_t left_x = source_x == 0U ? 0U : source_x - 1U;
      const std::uint32_t right_x =
          std::min(source_x + 1U, source_width - 1U);
      const std::uint8_t* center = pixel(source_x, source_y);
      const std::uint8_t* up = pixel(source_x, up_y);
      const std::uint8_t* left = pixel(left_x, source_y);
      const std::uint8_t* adjacent_right = pixel(right_x, source_y);
      const std::uint8_t* down = pixel(source_x, down_y);
      const bool vertical_edge = !equal(up, down);
      const bool horizontal_edge = !equal(left, adjacent_right);
      const std::uint8_t* selected = center;
      if (vertical_edge && horizontal_edge) {
        if (!lower && !right && equal(left, up)) selected = left;
        else if (!lower && right && equal(up, adjacent_right)) selected = up;
        else if (lower && !right && equal(left, down)) selected = left;
        else if (lower && right && equal(down, adjacent_right)) selected = down;
      }
      const std::size_t destination =
          (static_cast<std::size_t>(output_y) * output_width + output_x) * 3U;
      output_rgb888[destination] = selected[0];
      output_rgb888[destination + 1U] = selected[1];
      output_rgb888[destination + 2U] = selected[2];
    }
  }
  return EventResult{EventStatus::Applied, RasterError::None, output_pixels};
}

EventResult ScanoutMapper::chroma_smooth_rgb888(
    std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> source_rgb888,
    std::span<std::uint8_t> output_rgb888,
    DisplayPostprocessStats &stats) noexcept {
  stats = {};
  constexpr std::uint64_t kMaximumPixels = 8192ULL * 8192ULL;
  const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
  const std::uint64_t bytes = pixels * 3U;
  const std::uintptr_t source_begin =
      reinterpret_cast<std::uintptr_t>(source_rgb888.data());
  const std::uintptr_t output_begin =
      reinterpret_cast<std::uintptr_t>(output_rgb888.data());
  const bool address_overflow =
      bytes > std::numeric_limits<std::uintptr_t>::max() - source_begin ||
      bytes > std::numeric_limits<std::uintptr_t>::max() - output_begin;
  const bool overlaps = !address_overflow &&
      source_begin < output_begin + bytes && output_begin < source_begin + bytes;
  if (width == 0U || height == 0U || pixels > kMaximumPixels ||
      bytes != source_rgb888.size() || bytes != output_rgb888.size() ||
      address_overflow || overlaps) {
    return EventResult{EventStatus::Rejected,
                       RasterError::InvalidPresentationRequest};
  }
  stats.pixels = pixels;
  stats.source_pixel_reads = pixels * 3U;
  stats.luma_comparisons = pixels * 2U;
  stats.bytes_written = bytes;
  const auto clamp_channel = [](std::int32_t value) noexcept {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
  };
  const auto luma = [](const std::uint8_t* pixel) noexcept {
    return (77 * static_cast<std::int32_t>(pixel[0]) +
            150 * static_cast<std::int32_t>(pixel[1]) +
             29 * static_cast<std::int32_t>(pixel[2]) + 128) >> 8;
  };
  for (std::uint32_t y = 0U; y < height; ++y) {
    for (std::uint32_t x = 0U; x < width; ++x) {
      const std::uint32_t left_x = x == 0U ? 0U : x - 1U;
      const std::uint32_t right_x = std::min(x + 1U, width - 1U);
      const std::uint8_t* center = source_rgb888.data() +
          (static_cast<std::size_t>(y) * width + x) * 3U;
      const std::uint8_t* left = source_rgb888.data() +
          (static_cast<std::size_t>(y) * width + left_x) * 3U;
      const std::uint8_t* right = source_rgb888.data() +
          (static_cast<std::size_t>(y) * width + right_x) * 3U;
      const std::int32_t center_y = luma(center);
      const std::int32_t left_y = luma(left);
      const std::int32_t right_y = luma(right);
      std::int32_t red = center[0];
      std::int32_t green = center[1];
      std::int32_t blue = center[2];
      if (std::abs(center_y - left_y) <= 12 &&
          std::abs(center_y - right_y) <= 12) {
        const std::int32_t cr =
            ((static_cast<std::int32_t>(left[0]) - left_y) +
             2 * (static_cast<std::int32_t>(center[0]) - center_y) +
             (static_cast<std::int32_t>(right[0]) - right_y) + 2) / 4;
        const std::int32_t cb =
            ((static_cast<std::int32_t>(left[2]) - left_y) +
             2 * (static_cast<std::int32_t>(center[2]) - center_y) +
             (static_cast<std::int32_t>(right[2]) - right_y) + 2) / 4;
        red = center_y + cr;
        blue = center_y + cb;
        green = (256 * center_y - 77 * red - 29 * blue + 75) / 150;
        ++stats.chroma_filtered_pixels;
      }
      const std::size_t destination =
          (static_cast<std::size_t>(y) * width + x) * 3U;
      output_rgb888[destination] = clamp_channel(red);
      output_rgb888[destination + 1U] = clamp_channel(green);
      output_rgb888[destination + 2U] = clamp_channel(blue);
    }
  }
  return EventResult{EventStatus::Applied, RasterError::None, pixels};
}

bool native_backend_compatible(const NativeBackendV1 &backend) noexcept {
  return backend.abi_version == kNativeBackendAbiVersion &&
         backend.consume_event != nullptr;
}

ReferenceRasterizer::ReferenceRasterizer(RasterLimits limits)
    : impl_(std::make_unique<Impl>(limits)) {
  reset();
}

ReferenceRasterizer::~ReferenceRasterizer() noexcept = default;

void ReferenceRasterizer::reset(VramWord value) noexcept {
  impl_->vram.fill(value);
  impl_->precise_stats = {};
  if (impl_->vram_generation != std::numeric_limits<std::uint64_t>::max()) {
    ++impl_->vram_generation;
  }
}

EventResult ReferenceRasterizer::consume(const gpu::Event &event) noexcept {
  WorkBudget budget{};
  return impl_->consume(event, budget);
}

EventResult ReferenceRasterizer::consume(
    const gpu::Event &event, const ReplacementTexture *replacement) noexcept {
  WorkBudget budget{};
  impl_->replacement = replacement;
  const EventResult result = impl_->consume(event, budget);
  impl_->replacement = nullptr;
  return result;
}

EventResult ReferenceRasterizer::consume_precise_polygon(
    const gpu::PolygonEvent &event,
    std::span<const PreciseVertex> triangle_vertices,
    const ReplacementTexture *replacement,
    PreciseCullingPolicy culling,
    TextureFilterMode filtering) noexcept {
  WorkBudget budget{};
  impl_->replacement = replacement;
  const EventResult result =
      impl_->consume_precise_polygon(
          event, triangle_vertices, culling, filtering, budget);
  impl_->replacement = nullptr;
  return result;
}

EventResult ReferenceRasterizer::consume(const gpu::Event &event,
                                         WorkBudget &budget) noexcept {
  return impl_->consume(event, budget);
}

BatchResult
ReferenceRasterizer::consume(std::span<const gpu::Event> events) noexcept {
  WorkBudget budget{};
  return consume(events, budget);
}

BatchResult ReferenceRasterizer::consume(std::span<const gpu::Event> events,
                                         WorkBudget budget) noexcept {
  BatchResult result{};
  for (const gpu::Event &event : events) {
    const EventResult event_result = impl_->consume(event, budget);
    if (!event_result.accepted()) {
      result.error = event_result.error;
      return result;
    }
    ++result.events_processed;
    result.work_units += event_result.work_units;
  }
  result.completed = true;
  return result;
}

VramWord ReferenceRasterizer::read_vram(std::uint32_t x,
                                        std::uint32_t y) const noexcept {
  return read_wrapped(impl_->vram, x, y);
}

std::span<const VramWord> ReferenceRasterizer::vram_words() const noexcept {
  return std::span<const VramWord>(impl_->vram.data(), impl_->vram.size());
}

std::uint64_t ReferenceRasterizer::digest() const noexcept {
  std::uint64_t hash = kFnvOffset;
  for (const VramWord word : impl_->vram) {
    hash = mix_byte(hash, low_byte(word));
    hash = mix_byte(hash, static_cast<std::uint8_t>(word >> 8u));
  }
  return hash;
}

std::uint64_t ReferenceRasterizer::vram_generation() const noexcept {
  return impl_->vram_generation;
}

const RasterLimits &ReferenceRasterizer::limits() const noexcept {
  return impl_->limits;
}

const PreciseRasterStats &ReferenceRasterizer::precise_stats() const noexcept {
  return impl_->precise_stats;
}

NativeBackendV1 ReferenceRasterizer::native_backend() noexcept {
  return NativeBackendV1{kNativeBackendAbiVersion, this,
                         &backend_consume_event,   &backend_read_vram,
                         &backend_digest,          &backend_plan_presentation};
}

} // namespace clean_aot::raster
