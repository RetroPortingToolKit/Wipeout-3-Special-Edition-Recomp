#include "clean_aot/raster/raster.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using clean_aot::gpu::Color;
using clean_aot::gpu::DisplayTiming;
using clean_aot::gpu::Event;
using clean_aot::gpu::EventKind;
using clean_aot::gpu::LineEvent;
using clean_aot::gpu::PolygonEvent;
using clean_aot::gpu::RectangleEvent;
using clean_aot::gpu::RenderStateSnapshot;
using clean_aot::gpu::TextureCoordinate;
using clean_aot::gpu::TextureDepth;
using clean_aot::gpu::Vertex;
using clean_aot::gpu::VramCopyEvent;
using clean_aot::gpu::VramDownloadEvent;
using clean_aot::gpu::VramExtent;
using clean_aot::gpu::VramFillEvent;
using clean_aot::gpu::VramPoint;
using clean_aot::gpu::VramUploadEvent;
using clean_aot::raster::EventStatus;
using clean_aot::raster::PresentationMapper;
using clean_aot::raster::PresentationMode;
using clean_aot::raster::PresentationRequest;
using clean_aot::raster::RasterError;
using clean_aot::raster::ReferenceRasterizer;
using clean_aot::raster::ScanoutField;
using clean_aot::raster::ScanoutMapper;
using clean_aot::raster::ScanoutPixelFormat;
using clean_aot::raster::ScanoutRequest;
using clean_aot::raster::VramRect;
using clean_aot::raster::VramWord;
using clean_aot::raster::WorkBudget;
using clean_aot::raster::kVramWordCount;

class TestFailure final : public std::exception {
public:
  explicit TestFailure(std::string message) : message_(std::move(message)) {}

  [[nodiscard]] const char *what() const noexcept override {
    return message_.c_str();
  }

private:
  std::string message_;
};

void require(bool condition, const char *message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

template <typename Enum>
[[nodiscard]] Enum enum_from_byte(std::uint8_t value) noexcept {
  static_assert(sizeof(Enum) == sizeof(value));
  return std::bit_cast<Enum>(value);
}

[[nodiscard]] Color color(std::uint8_t red, std::uint8_t green,
                          std::uint8_t blue) noexcept {
  return Color{0, red, green, blue};
}

[[nodiscard]] Vertex vertex(std::int16_t x, std::int16_t y) noexcept {
  return Vertex{0, x, y};
}

[[nodiscard]] TextureCoordinate uv(std::uint8_t u, std::uint8_t v,
                                   std::uint16_t upper = 0) noexcept {
  return TextureCoordinate{0, u, v, upper};
}

[[nodiscard]] RenderStateSnapshot full_state() noexcept {
  RenderStateSnapshot state{};
  state.draw_area.left = 0;
  state.draw_area.top = 0;
  state.draw_area.right = 1023;
  state.draw_area.bottom = 511;
  return state;
}

[[nodiscard]] VramUploadEvent upload(VramPoint destination, VramExtent extent,
                                     std::vector<std::uint32_t> words,
                                     clean_aot::gpu::MaskState mask = {}) {
  VramUploadEvent event{};
  event.destination = destination;
  event.extent = extent;
  event.pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
  event.padded_word_count = (event.pixel_count + 1u) / 2u;
  event.final_word_valid_halfwords =
      static_cast<std::uint8_t>((event.pixel_count & 1u) != 0u ? 1u : 2u);
  event.data_words = std::move(words);
  event.mask = mask;
  return event;
}

void consume_ok(ReferenceRasterizer &rasterizer, const Event &event) {
  const auto result = rasterizer.consume(event);
  require(result.accepted(), "event was rejected");
}

void test_vram_transfers_and_handoff() {
  ReferenceRasterizer rasterizer;
  require(rasterizer.vram_words().size() == clean_aot::raster::kVramWordCount,
          "reference VRAM is not exactly 1024x512 halfwords");

  VramFillEvent fill{};
  fill.origin = VramPoint{1023, 511};
  fill.extent = VramExtent{2, 2};
  fill.color = color(255, 0, 0);
  consume_ok(rasterizer, Event{fill});
  require(rasterizer.read_vram(1023, 511) == 0x001fu,
          "fill did not write the first wrapped pixel");
  require(rasterizer.read_vram(0, 0) == 0x001fu,
          "fill did not wrap both coordinates");

  const VramUploadEvent odd_upload =
      upload(VramPoint{4, 5}, VramExtent{3, 1}, {0x22221111u, 0x00003333u});
  consume_ok(rasterizer, Event{odd_upload});
  require(rasterizer.read_vram(4, 5) == 0x1111u &&
              rasterizer.read_vram(5, 5) == 0x2222u &&
              rasterizer.read_vram(6, 5) == 0x3333u,
          "odd upload halfword order is incorrect");

  const VramCopyEvent copy = [&]() {
    VramCopyEvent event{};
    event.source = VramPoint{4, 5};
    event.destination = VramPoint{5, 5};
    event.extent = VramExtent{3, 1};
    return event;
  }();
  consume_ok(rasterizer, Event{copy});
  require(rasterizer.read_vram(5, 5) == 0x1111u &&
              rasterizer.read_vram(6, 5) == 0x2222u &&
              rasterizer.read_vram(7, 5) == 0x3333u,
          "overlapping copy did not use a source row buffer");

  consume_ok(rasterizer, Event{upload(VramPoint{30, 30}, VramExtent{1, 3},
                                      {0x00020001u, 0x00000003u})});
  VramCopyEvent vertical_overlap{};
  vertical_overlap.source = VramPoint{30, 30};
  vertical_overlap.destination = VramPoint{30, 31};
  vertical_overlap.extent = VramExtent{1, 3};
  consume_ok(rasterizer, Event{vertical_overlap});
  require(rasterizer.read_vram(30, 30) == 0x0001u &&
              rasterizer.read_vram(30, 31) == 0x0001u &&
              rasterizer.read_vram(30, 32) == 0x0001u &&
              rasterizer.read_vram(30, 33) == 0x0001u,
          "vertical overlap did not preserve top-to-bottom copy ordering");

  const VramUploadEvent masked_upload =
      upload(VramPoint{20, 20}, VramExtent{2, 1}, {0x56781234u},
             clean_aot::gpu::MaskState{0, false, true});
  consume_ok(rasterizer,
             Event{upload(VramPoint{20, 20}, VramExtent{2, 1}, {0x00008000u})});
  consume_ok(rasterizer, Event{masked_upload});
  require(rasterizer.read_vram(20, 20) == 0x8000u &&
              rasterizer.read_vram(21, 20) == 0x5678u,
          "upload mask check did not skip only the masked destination");

  consume_ok(rasterizer,
             Event{upload(VramPoint{24, 20}, VramExtent{1, 1}, {0x1234u})});
  VramCopyEvent masked_copy{};
  masked_copy.source = VramPoint{24, 20};
  masked_copy.destination = VramPoint{25, 20};
  masked_copy.extent = VramExtent{1, 1};
  masked_copy.mask.set_mask_bit = true;
  consume_ok(rasterizer, Event{masked_copy});
  require(rasterizer.read_vram(25, 20) == 0x9234u,
          "copy mask set did not set the destination mask bit");

  VramDownloadEvent download{};
  download.source = VramPoint{4, 5};
  download.extent = VramExtent{3, 1};
  download.pixel_count = 3;
  download.padded_word_count = 2;
  download.final_word_valid_halfwords = 1;
  download.data_words = {0x22221111u, 0x00003333u};
  Event download_event{download};
  const auto download_result = rasterizer.consume(download_event);
  require(download_result.status == EventStatus::Ignored,
          "download event was not accepted as an ignored transfer");
  require(download_result.download_words.size() == 2u,
          "download payload size was not handed off");
  require(download_result.download_words[1] == 0x00003333u,
          "download payload value was not preserved");
}

void test_hardware_vram_overlap_golden() {
  ReferenceRasterizer rasterizer;
  constexpr std::array<std::uint16_t, 7> sizes{{2, 8, 15, 16, 16, 16, 16}};

  for (std::size_t test = 0; test < sizes.size(); ++test) {
    const std::uint16_t size = sizes[test];
    for (std::uint16_t cell = 1; cell <= 21; ++cell) {
      const std::size_t pixel_count =
          static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
      std::vector<std::uint32_t> words((pixel_count + 1u) / 2u, 0u);
      for (std::uint16_t y = 0; y < size; ++y) {
        for (std::uint16_t x = 0; x < size; ++x) {
          const std::size_t index = static_cast<std::size_t>(y) * size + x;
          const std::uint16_t value =
              static_cast<std::uint16_t>(y * 64u + x * 2u);
          words[index / 2u] |= static_cast<std::uint32_t>(value)
                               << ((index & 1u) * 16u);
        }
      }
      consume_ok(
          rasterizer,
          Event{upload(VramPoint{static_cast<std::uint16_t>(cell * 42u + 4u),
                                 static_cast<std::uint16_t>(test * 42u + 4u)},
                       VramExtent{size, size}, std::move(words))});
    }
  }

  std::uint16_t cell = 1;
  for (std::int16_t offset_y = -1; offset_y <= 1; ++offset_y) {
    for (std::int16_t offset_x = -3; offset_x <= 3; ++offset_x, ++cell) {
      for (std::size_t test = 0; test < sizes.size(); ++test) {
        const std::uint16_t source_offset_x =
            (test == 4u || test == 6u) ? 1u : 0u;
        const std::uint16_t source_offset_y =
            (test == 5u || test == 6u) ? 1u : 0u;
        const std::uint16_t source_x =
            static_cast<std::uint16_t>(cell * 42u + 4u + source_offset_x);
        const std::uint16_t source_y =
            static_cast<std::uint16_t>(test * 42u + 4u + source_offset_y);
        VramCopyEvent copy{};
        copy.source = VramPoint{source_x, source_y};
        copy.destination =
            VramPoint{static_cast<std::uint16_t>(
                          static_cast<std::int32_t>(source_x) + offset_x),
                      static_cast<std::uint16_t>(
                          static_cast<std::int32_t>(source_y) + offset_y)};
        copy.extent = VramExtent{sizes[test], sizes[test]};
        consume_ok(rasterizer, Event{copy});
      }
    }
  }

  std::uint64_t digest = 14695981039346656037ull;
  const auto mix = [](std::uint64_t hash, std::uint8_t byte) {
    return (hash ^ byte) * 1099511628211ull;
  };
  for (std::size_t test = 0; test < sizes.size(); ++test) {
    for (std::uint16_t sample_cell = 1; sample_cell <= 21; ++sample_cell) {
      for (std::uint16_t y = 1; y < 25; ++y) {
        for (std::uint16_t x = 1; x < 25; ++x) {
          const std::uint16_t word = rasterizer.read_vram(
              static_cast<std::uint32_t>(sample_cell * 42u + x),
              static_cast<std::uint32_t>(test * 42u + y));
          digest = mix(digest, static_cast<std::uint8_t>(word & 0xffu));
          digest = mix(digest, static_cast<std::uint8_t>(word >> 8u));
        }
      }
    }
  }
  require(digest == 0x06080cd77ae85263ull,
          "VRAM overlap ordering differs from real-console VRAM");
}

void test_wrapped_transfer_aliasing() {
  ReferenceRasterizer rasterizer;
  consume_ok(rasterizer, Event{upload(VramPoint{1023, 511}, VramExtent{2, 2},
                                      {0x00020001u, 0x00040003u})});
  require(rasterizer.read_vram(1023, 511) == 0x0001u &&
              rasterizer.read_vram(0, 511) == 0x0002u &&
              rasterizer.read_vram(1023, 0) == 0x0003u &&
              rasterizer.read_vram(0, 0) == 0x0004u,
          "upload did not wrap X and Y independently");

  consume_ok(rasterizer, Event{upload(VramPoint{1023, 100}, VramExtent{2, 1},
                                      {0x22221111u})});
  VramCopyEvent horizontal{};
  horizontal.source = VramPoint{1023, 100};
  horizontal.destination = VramPoint{0, 100};
  horizontal.extent = VramExtent{2, 1};
  consume_ok(rasterizer, Event{horizontal});
  require(rasterizer.read_vram(0, 100) == 0x1111u &&
              rasterizer.read_vram(1, 100) == 0x2222u,
          "wrapped horizontal overlap did not snapshot its source row");

  consume_ok(rasterizer, Event{upload(VramPoint{20, 511}, VramExtent{1, 2},
                                      {0xbbbbaaaau})});
  VramCopyEvent vertical{};
  vertical.source = VramPoint{20, 511};
  vertical.destination = VramPoint{20, 0};
  vertical.extent = VramExtent{1, 2};
  consume_ok(rasterizer, Event{vertical});
  require(rasterizer.read_vram(20, 0) == 0xaaaau &&
              rasterizer.read_vram(20, 1) == 0xaaaau,
          "wrapped vertical overlap did not preserve top-to-bottom ordering");
}

void test_primitives_and_state() {
  ReferenceRasterizer rasterizer;
  RenderStateSnapshot state = full_state();

  RectangleEvent rectangle{};
  rectangle.state = state;
  rectangle.origin = vertex(1, 1);
  rectangle.width = 2;
  rectangle.height = 2;
  rectangle.color = color(0, 255, 0);
  rectangle.state.draw_offset.x = 2;
  rectangle.state.draw_offset.y = 3;
  consume_ok(rasterizer, Event{rectangle});
  require(rasterizer.read_vram(3, 4) == 0x03e0u &&
              rasterizer.read_vram(4, 5) == 0x03e0u,
          "rectangle offset or packing is incorrect");
  require(rasterizer.read_vram(1, 1) == 0u,
          "rectangle wrote before applying draw offset");

  LineEvent line{};
  line.state = state;
  line.vertices = {vertex(10, 10), vertex(13, 10)};
  line.first_color = color(0, 0, 255);
  consume_ok(rasterizer, Event{line});
  for (std::uint32_t x = 10; x <= 13; ++x) {
    require(rasterizer.read_vram(x, 10) == 0x7c00u,
            "line endpoint rasterization is not inclusive");
  }

  PolygonEvent triangle{};
  triangle.state = state;
  triangle.vertex_count = 3;
  triangle.vertices[0] = vertex(20, 20);
  triangle.vertices[1] = vertex(24, 20);
  triangle.vertices[2] = vertex(20, 24);
  triangle.first_color = color(255, 0, 0);
  consume_ok(rasterizer, Event{triangle});
  require(rasterizer.read_vram(20, 20) == 0x001fu &&
              rasterizer.read_vram(21, 20) == 0x001fu &&
              rasterizer.read_vram(20, 21) == 0x001fu,
          "triangle did not cover its top-left interior");

  PolygonEvent quad{};
  quad.state = state;
  quad.quad = true;
  quad.vertex_count = 4;
  quad.vertices[0] = vertex(30, 10);
  quad.vertices[1] = vertex(34, 10);
  quad.vertices[2] = vertex(34, 14);
  quad.vertices[3] = vertex(30, 14);
  quad.first_color = color(255, 255, 255);
  consume_ok(rasterizer, Event{quad});
  require(rasterizer.read_vram(30, 10) == 0x7fffu &&
              rasterizer.read_vram(33, 13) == 0x7fffu,
          "quad split or shared-edge ownership is incorrect");

  PolygonEvent gouraud{};
  gouraud.state = state;
  gouraud.gouraud = true;
  gouraud.vertex_count = 3;
  gouraud.vertices[0] = vertex(40, 10);
  gouraud.vertices[1] = vertex(44, 10);
  gouraud.vertices[2] = vertex(40, 14);
  gouraud.colors[0] = color(255, 0, 0);
  gouraud.colors[1] = color(0, 255, 0);
  gouraud.colors[2] = color(0, 0, 255);
  gouraud.color_present = {true, true, true, false};
  consume_ok(rasterizer, Event{gouraud});
  require(rasterizer.read_vram(40, 10) != 0u &&
              rasterizer.read_vram(41, 10) != rasterizer.read_vram(40, 11),
          "Gouraud triangle interpolation is not deterministic");

  LineEvent polyline{};
  polyline.state = state;
  polyline.polyline = true;
  polyline.vertices = {vertex(50, 10), vertex(52, 12), vertex(54, 10)};
  polyline.first_color = color(255, 255, 0);
  consume_ok(rasterizer, Event{polyline});
  require(rasterizer.read_vram(52, 12) == 0x03ffu,
          "polyline join was not rasterized");

  RenderStateSnapshot clipped_state = full_state();
  clipped_state.draw_area.left = 30;
  clipped_state.draw_area.top = 30;
  clipped_state.draw_area.right = 31;
  clipped_state.draw_area.bottom = 31;
  RectangleEvent clipped{};
  clipped.state = clipped_state;
  clipped.origin = vertex(29, 29);
  clipped.width = 4;
  clipped.height = 4;
  clipped.color = color(255, 255, 255);
  consume_ok(rasterizer, Event{clipped});
  require(rasterizer.read_vram(30, 30) == 0x7fffu &&
              rasterizer.read_vram(31, 31) == 0x7fffu &&
              rasterizer.read_vram(29, 29) == 0u,
          "draw area clipping is incorrect");

  rasterizer.reset(0x8000u);
  RenderStateSnapshot mask_state = full_state();
  mask_state.mask.check_mask_bit = true;
  RectangleEvent masked{};
  masked.state = mask_state;
  masked.origin = vertex(40, 40);
  masked.width = 2;
  masked.height = 1;
  masked.color = color(255, 0, 0);
  consume_ok(rasterizer, Event{masked});
  require(rasterizer.read_vram(40, 40) == 0x8000u,
          "primitive mask check did not protect the destination");

  rasterizer.reset();
  VramFillEvent background{};
  background.origin = VramPoint{50, 50};
  background.extent = VramExtent{1, 1};
  background.color = color(255, 0, 0);
  consume_ok(rasterizer, Event{background});
  RectangleEvent blended{};
  blended.state = full_state();
  blended.origin = vertex(50, 50);
  blended.width = 1;
  blended.height = 1;
  blended.color = color(0, 0, 255);
  blended.semi_transparent = true;
  consume_ok(rasterizer, Event{blended});
  require(rasterizer.read_vram(50, 50) == 0x3c0fu,
          "half semi-transparency result is incorrect");

  VramFillEvent blend_background{};
  blend_background.origin = VramPoint{52, 50};
  blend_background.extent = VramExtent{4, 1};
  blend_background.color = color(80, 160, 240);
  consume_ok(rasterizer, Event{blend_background});
  constexpr std::array<clean_aot::gpu::SemiTransparencyMode, 4> modes{{
      clean_aot::gpu::SemiTransparencyMode::HalfBackgroundHalfForeground,
      clean_aot::gpu::SemiTransparencyMode::BackgroundPlusForeground,
      clean_aot::gpu::SemiTransparencyMode::BackgroundMinusForeground,
      clean_aot::gpu::SemiTransparencyMode::BackgroundPlusQuarterForeground,
  }};
  constexpr std::array<std::uint16_t, 4> expected_blends{
      {0x6628u, 0x7ff0u, 0x28a4u, 0x7eebu}};
  for (std::size_t index = 0; index < modes.size(); ++index) {
    RectangleEvent blend_mode{};
    blend_mode.state = full_state();
    blend_mode.state.draw_mode.semi_transparency = modes[index];
    blend_mode.origin = vertex(static_cast<std::int16_t>(52 + index), 50);
    blend_mode.width = 1;
    blend_mode.height = 1;
    blend_mode.color = color(48, 120, 160);
    blend_mode.semi_transparent = true;
    consume_ok(rasterizer, Event{blend_mode});
    require(rasterizer.read_vram(static_cast<std::uint32_t>(52 + index), 50) ==
                expected_blends[index],
            "semi-transparency mode produced the wrong clamped RGB5 value");
  }

  rasterizer.reset();
  RenderStateSnapshot dither_state = full_state();
  dither_state.draw_mode.dither = true;
  RectangleEvent dithered{};
  dithered.state = dither_state;
  dithered.origin = vertex(60, 60);
  dithered.width = 2;
  dithered.height = 2;
  dithered.color = color(128, 128, 128);
  consume_ok(rasterizer, Event{dithered});
  require(rasterizer.read_vram(60, 60) == 0x4210u &&
              rasterizer.read_vram(61, 60) == 0x4210u,
          "rectangle command was incorrectly dithered");

  rasterizer.reset();
  LineEvent dithered_line{};
  dithered_line.state = dither_state;
  dithered_line.vertices = {vertex(60, 60), vertex(61, 60)};
  dithered_line.first_color = color(128, 128, 128);
  consume_ok(rasterizer, Event{dithered_line});
  require(rasterizer.read_vram(60, 60) == 0x3defu &&
              rasterizer.read_vram(61, 60) == 0x4210u,
          "line command did not apply ordered dithering");

  rasterizer.reset();
  PolygonEvent flat_dithered{};
  flat_dithered.state = dither_state;
  flat_dithered.vertex_count = 3;
  flat_dithered.vertices[0] = vertex(70, 70);
  flat_dithered.vertices[1] = vertex(74, 70);
  flat_dithered.vertices[2] = vertex(70, 74);
  flat_dithered.first_color = color(128, 128, 128);
  consume_ok(rasterizer, Event{flat_dithered});
  require(rasterizer.read_vram(70, 70) == 0x4210u,
          "flat untextured polygon was incorrectly dithered");
}

void test_hardware_line_semantics() {
  ReferenceRasterizer rasterizer;

  const auto draw_flat_line = [&](Vertex first, Vertex second) {
    LineEvent line{};
    line.state = full_state();
    line.vertices = {first, second};
    line.first_color = color(255, 0, 0);
    consume_ok(rasterizer, Event{line});
  };

  // These tie cases are sampled from the real-console gpu/lines VRAM image
  // in JaCzekanski/ps1-tests. A generic Bresenham implementation chooses the
  // other middle pixel for the first segment.
  draw_flat_line(vertex(200, 24), vertex(202, 25));
  require(rasterizer.read_vram(200, 24) == 0x001fu &&
              rasterizer.read_vram(201, 25) == 0x001fu &&
              rasterizer.read_vram(202, 25) == 0x001fu &&
              rasterizer.read_vram(201, 24) == 0u,
          "positive shallow line did not follow PS1 DDA tie coverage");

  draw_flat_line(vertex(202, 35), vertex(200, 34));
  require(rasterizer.read_vram(200, 34) == 0x001fu &&
              rasterizer.read_vram(201, 35) == 0x001fu &&
              rasterizer.read_vram(202, 35) == 0x001fu &&
              rasterizer.read_vram(201, 34) == 0u,
          "reversed shallow line changed PS1 endpoint-normalized coverage");

  draw_flat_line(vertex(200, 44), vertex(202, 43));
  require(rasterizer.read_vram(200, 44) == 0x001fu &&
              rasterizer.read_vram(201, 43) == 0x001fu &&
              rasterizer.read_vram(202, 43) == 0x001fu &&
              rasterizer.read_vram(201, 44) == 0u,
          "negative shallow line did not apply the PS1 subpixel bias");

  LineEvent point{};
  point.state = full_state();
  point.gouraud = true;
  point.vertices = {vertex(210, 24), vertex(210, 24)};
  point.colors = {color(255, 0, 0), color(0, 255, 0)};
  consume_ok(rasterizer, Event{point});
  require(rasterizer.read_vram(210, 24) == 0x001fu,
          "zero-length Gouraud line did not use its first vertex color");

  LineEvent gradient{};
  gradient.state = full_state();
  gradient.gouraud = true;
  gradient.vertices = {vertex(16, 70), vertex(79, 70)};
  gradient.colors = {color(0, 0, 0), color(255, 0, 0)};
  consume_ok(rasterizer, Event{gradient});
  require(rasterizer.read_vram(16, 70) == 0x0000u &&
              rasterizer.read_vram(18, 70) == 0x0001u &&
              rasterizer.read_vram(47, 70) == 0x000fu &&
              rasterizer.read_vram(48, 70) == 0x0010u &&
              rasterizer.read_vram(78, 70) == 0x001fu &&
              rasterizer.read_vram(79, 70) == 0x001fu,
          "Gouraud line interpolation differs from real-console VRAM");

  gradient.vertices = {vertex(79, 71), vertex(16, 71)};
  gradient.colors = {color(255, 0, 0), color(0, 0, 0)};
  consume_ok(rasterizer, Event{gradient});
  for (std::uint32_t x = 16; x <= 79; ++x) {
    require(rasterizer.read_vram(x, 71) == rasterizer.read_vram(x, 70),
            "reversed Gouraud line changed endpoint-normalized colors");
  }

  VramFillEvent background{};
  background.origin = VramPoint{240, 40};
  background.extent = VramExtent{3, 3};
  background.color = color(255, 255, 255);
  consume_ok(rasterizer, Event{background});

  LineEvent polyline{};
  polyline.state = full_state();
  polyline.polyline = true;
  polyline.semi_transparent = true;
  polyline.vertices = {vertex(240, 40), vertex(242, 40), vertex(242, 42)};
  polyline.first_color = color(255, 0, 0);
  consume_ok(rasterizer, Event{polyline});
  require(rasterizer.read_vram(241, 40) == 0x3dfeu,
          "first polyline segment did not blend exactly once");
  require(rasterizer.read_vram(242, 41) == 0x3dfeu,
          "second polyline segment did not blend exactly once");
  require(rasterizer.read_vram(242, 40) == 0x1cfeu,
          "polyline join was not emitted once by each adjacent segment");
}

void test_hardware_triangle_golden() {
  ReferenceRasterizer rasterizer;
  rasterizer.reset(0x7fffu);

  const auto draw_triangle = [&](std::int16_t center_x, std::int16_t center_y,
                                 std::int16_t side, bool dither) {
    PolygonEvent triangle{};
    triangle.state = full_state();
    triangle.state.draw_mode.dither = dither;
    triangle.gouraud = true;
    triangle.vertex_count = 3;

    // Integer vertices produced by gpu/triangle in ps1-tests for its 240- and
    // 500-pixel equilateral triangles (the source truncates float to int).
    const std::int16_t half_side = static_cast<std::int16_t>(side / 2);
    const std::int16_t lower_half_height = side == 240
                                               ? static_cast<std::int16_t>(103)
                                               : static_cast<std::int16_t>(216);
    const std::int16_t upper_half_height = side == 240
                                               ? static_cast<std::int16_t>(104)
                                               : static_cast<std::int16_t>(217);
    triangle.vertices[0] =
        vertex(static_cast<std::int16_t>(center_x - half_side),
               static_cast<std::int16_t>(center_y + lower_half_height));
    triangle.vertices[1] =
        vertex(static_cast<std::int16_t>(center_x + half_side),
               static_cast<std::int16_t>(center_y + lower_half_height));
    triangle.vertices[2] = vertex(
        center_x, static_cast<std::int16_t>(center_y - upper_half_height));
    triangle.colors[0] = color(255, 0, 0);
    triangle.colors[1] = color(0, 255, 0);
    triangle.colors[2] = color(0, 0, 255);
    triangle.color_present = {true, true, true, false};
    consume_ok(rasterizer, Event{triangle});
  };

  draw_triangle(160, 120, 240, false);
  draw_triangle(768, 256, 500, false);
  draw_triangle(160, 360, 240, true);
  const std::uint64_t digest = rasterizer.digest();
  require(digest == 0xd8867d7d262ae5bfull,
          "triangle coverage/interpolation differs from real-console VRAM");
}

void test_hardware_uv_and_color_interpolation_golden() {
  ReferenceRasterizer rasterizer;
  consume_ok(rasterizer,
             Event{upload(VramPoint{512, 0}, VramExtent{2, 1}, {0x03e0001fu})});

  for (std::int16_t width = 0; width < 256; ++width) {
    PolygonEvent textured{};
    textured.state = full_state();
    textured.state.draw_mode.x_base = 8;
    textured.state.draw_mode.depth = TextureDepth::Direct15;
    textured.quad = true;
    textured.textured = true;
    textured.vertex_count = 4;
    textured.vertices[0] = vertex(0, width);
    textured.vertices[1] = vertex(width, width);
    textured.vertices[2] = vertex(0, static_cast<std::int16_t>(width + 1));
    textured.vertices[3] = vertex(width, static_cast<std::int16_t>(width + 1));
    textured.first_color = color(128, 128, 128);
    textured.texture_coordinates[0] = uv(0, 0);
    textured.texture_coordinates[1] = uv(1, 0);
    textured.texture_coordinates[2] = uv(0, 0);
    textured.texture_coordinates[3] = uv(1, 0);
    textured.texture_coordinate_present = {true, true, true, true};
    consume_ok(rasterizer, Event{textured});

    const auto draw_color_row = [&](std::int16_t origin_x, bool dither) {
      PolygonEvent shaded{};
      shaded.state = full_state();
      shaded.state.draw_mode.dither = dither;
      shaded.quad = true;
      shaded.gouraud = true;
      shaded.vertex_count = 4;
      const std::int16_t y = static_cast<std::int16_t>(256 + width);
      shaded.vertices[0] = vertex(origin_x, y);
      shaded.vertices[1] =
          vertex(static_cast<std::int16_t>(origin_x + width), y);
      shaded.vertices[2] = vertex(origin_x, static_cast<std::int16_t>(y + 1));
      shaded.vertices[3] = vertex(static_cast<std::int16_t>(origin_x + width),
                                  static_cast<std::int16_t>(y + 1));
      shaded.colors[0] = color(255, 0, 0);
      shaded.colors[1] = color(0, 255, 0);
      shaded.colors[2] = color(255, 0, 0);
      shaded.colors[3] = color(0, 255, 0);
      shaded.color_present = {true, true, true, true};
      consume_ok(rasterizer, Event{shaded});
    };
    draw_color_row(0, false);
    draw_color_row(256, true);
  }

  require(rasterizer.digest() == 0xc8124b3a9ec213e6ull,
          "UV/color interpolation differs from real-console VRAM");
}

void seed_texture_memory(ReferenceRasterizer &rasterizer) {
  consume_ok(rasterizer,
             Event{upload(VramPoint{0, 0}, VramExtent{1, 1}, {0x4321u})});
  consume_ok(rasterizer,
             Event{upload(VramPoint{2, 0}, VramExtent{1, 1}, {0x0004u})});
  consume_ok(rasterizer,
             Event{upload(VramPoint{16, 10}, VramExtent{5, 1},
                          {0x001f0000u, 0x7c0003e0u, 0x00007fffu})});

  consume_ok(rasterizer,
             Event{upload(VramPoint{64, 1}, VramExtent{1, 1}, {0x0201u})});
  consume_ok(rasterizer, Event{upload(VramPoint{32, 11}, VramExtent{3, 1},
                                      {0x001f0000u, 0x000003e0u})});

  consume_ok(rasterizer,
             Event{upload(VramPoint{128, 2}, VramExtent{1, 1}, {0x7fffu})});
}

void test_textures() {
  ReferenceRasterizer rasterizer;
  seed_texture_memory(rasterizer);

  RenderStateSnapshot indexed4 = full_state();
  indexed4.draw_mode.depth = TextureDepth::Indexed4;
  RectangleEvent indexed4_rectangle{};
  indexed4_rectangle.state = indexed4;
  indexed4_rectangle.textured = true;
  indexed4_rectangle.raw_texture = true;
  indexed4_rectangle.origin = vertex(100, 100);
  indexed4_rectangle.width = 4;
  indexed4_rectangle.height = 1;
  indexed4_rectangle.texture_coordinate = uv(0, 0);
  indexed4_rectangle.clut = clean_aot::gpu::ClutAttribute{0, 16, 10};
  consume_ok(rasterizer, Event{indexed4_rectangle});
  require(rasterizer.read_vram(100, 100) == 0x001fu &&
              rasterizer.read_vram(101, 100) == 0x03e0u &&
              rasterizer.read_vram(102, 100) == 0x7c00u &&
              rasterizer.read_vram(103, 100) == 0x7fffu,
          "indexed-4 texture or CLUT addressing is incorrect");

  RenderStateSnapshot indexed8 = full_state();
  indexed8.draw_mode.x_base = 1;
  indexed8.draw_mode.y_base = 0;
  indexed8.draw_mode.depth = TextureDepth::Indexed8;
  RectangleEvent indexed8_rectangle{};
  indexed8_rectangle.state = indexed8;
  indexed8_rectangle.textured = true;
  indexed8_rectangle.raw_texture = true;
  indexed8_rectangle.origin = vertex(110, 100);
  indexed8_rectangle.width = 2;
  indexed8_rectangle.height = 1;
  indexed8_rectangle.texture_coordinate = uv(0, 1);
  indexed8_rectangle.clut = clean_aot::gpu::ClutAttribute{0, 32, 11};
  consume_ok(rasterizer, Event{indexed8_rectangle});
  require(rasterizer.read_vram(110, 100) == 0x001fu &&
              rasterizer.read_vram(111, 100) == 0x03e0u,
          "indexed-8 texture or CLUT addressing is incorrect");

  RenderStateSnapshot direct15 = full_state();
  direct15.draw_mode.x_base = 2;
  direct15.draw_mode.y_base = 0;
  direct15.draw_mode.depth = TextureDepth::Direct15;
  RectangleEvent direct_rectangle{};
  direct_rectangle.state = direct15;
  direct_rectangle.textured = true;
  direct_rectangle.raw_texture = true;
  direct_rectangle.origin = vertex(120, 100);
  direct_rectangle.width = 1;
  direct_rectangle.height = 1;
  direct_rectangle.texture_coordinate = uv(0, 2);
  consume_ok(rasterizer, Event{direct_rectangle});
  require(rasterizer.read_vram(120, 100) == 0x7fffu,
          "direct-15 texture page addressing is incorrect");

  RenderStateSnapshot windowed = indexed4;
  windowed.texture_window.mask_x = 1;
  windowed.texture_window.offset_x = 1;
  RectangleEvent windowed_rectangle = indexed4_rectangle;
  windowed_rectangle.state = windowed;
  windowed_rectangle.origin = vertex(130, 100);
  windowed_rectangle.width = 1;
  windowed_rectangle.texture_coordinate = uv(0, 0);
  consume_ok(rasterizer, Event{windowed_rectangle});
  require(rasterizer.read_vram(130, 100) == 0x7fffu,
          "texture window did not remap the requested coordinate");

  RenderStateSnapshot modulated_state = direct15;
  RectangleEvent modulated = direct_rectangle;
  modulated.state = modulated_state;
  modulated.origin = vertex(140, 100);
  modulated.color = color(64, 64, 64);
  modulated.raw_texture = false;
  consume_ok(rasterizer, Event{modulated});
  require(rasterizer.read_vram(140, 100) != 0x7fffu,
          "modulated texture did not use the vertex color");

  RectangleEvent disabled = direct_rectangle;
  disabled.state = direct15;
  disabled.state.draw_mode.texture_disable = true;
  disabled.origin = vertex(150, 100);
  disabled.color = color(255, 0, 0);
  consume_ok(rasterizer, Event{disabled});
  require(rasterizer.read_vram(150, 100) == 0x001fu,
          "texture disable did not select the flat primitive color");

  RenderStateSnapshot transparent_state = indexed4;
  RectangleEvent transparent = indexed4_rectangle;
  transparent.state = transparent_state;
  transparent.origin = vertex(160, 100);
  transparent.width = 1;
  transparent.texture_coordinate = uv(4, 0);
  consume_ok(rasterizer, Event{transparent});
  require(rasterizer.read_vram(160, 100) == 0u,
          "zero palette texel was not transparent");

  consume_ok(rasterizer,
             Event{upload(VramPoint{383, 2}, VramExtent{1, 1}, {0x03e0u})});
  RectangleEvent flipped_x = direct_rectangle;
  flipped_x.state.draw_mode.rectangle_flip_x = true;
  flipped_x.origin = vertex(161, 100);
  flipped_x.width = 2;
  flipped_x.texture_coordinate = uv(0, 2);
  consume_ok(rasterizer, Event{flipped_x});
  require(rasterizer.read_vram(161, 100) == 0x7fffu &&
              rasterizer.read_vram(162, 100) == 0x03e0u,
          "rectangle X flip did not wrap a negative U coordinate to 255");

  consume_ok(rasterizer,
             Event{upload(VramPoint{128, 0}, VramExtent{1, 1}, {0x7fffu})});
  consume_ok(rasterizer,
             Event{upload(VramPoint{128, 255}, VramExtent{1, 1}, {0x7c00u})});
  RectangleEvent flipped_y = direct_rectangle;
  flipped_y.state.draw_mode.rectangle_flip_y = true;
  flipped_y.origin = vertex(164, 100);
  flipped_y.height = 2;
  flipped_y.texture_coordinate = uv(0, 0);
  consume_ok(rasterizer, Event{flipped_y});
  require(rasterizer.read_vram(164, 100) == 0x7fffu &&
              rasterizer.read_vram(164, 101) == 0x7c00u,
          "rectangle Y flip did not wrap a negative V coordinate to 255");
}

void test_texture_mask_transparency_and_modulation() {
  ReferenceRasterizer rasterizer;
  consume_ok(rasterizer,
             Event{upload(VramPoint{192, 3}, VramExtent{5, 1},
                          {0x001f8000u, 0x800f801fu, 0x0000000fu})});
  consume_ok(rasterizer,
             Event{upload(VramPoint{170, 120}, VramExtent{5, 1},
                          {0x7c007c00u, 0x7c007c00u, 0x00007c00u})});

  RenderStateSnapshot direct = full_state();
  direct.draw_mode.x_base = 3;
  direct.draw_mode.y_base = 0;
  direct.draw_mode.depth = TextureDepth::Direct15;

  const auto draw_texel = [&](std::int16_t destination_x,
                              std::uint8_t texture_x, bool semi_transparent) {
    RectangleEvent rectangle{};
    rectangle.state = direct;
    rectangle.textured = true;
    rectangle.raw_texture = true;
    rectangle.semi_transparent = semi_transparent;
    rectangle.origin = vertex(destination_x, 120);
    rectangle.width = 1;
    rectangle.height = 1;
    rectangle.texture_coordinate = uv(texture_x, 3);
    consume_ok(rasterizer, Event{rectangle});
  };

  draw_texel(170, 0, true);
  draw_texel(171, 1, true);
  draw_texel(172, 2, true);
  draw_texel(173, 0, false);
  require(rasterizer.read_vram(170, 120) == 0xbc00u,
          "0x8000 texel was treated as transparent or lost bit 15");
  require(rasterizer.read_vram(171, 120) == 0x001fu,
          "opaque texel was blended by a semi-transparent texture command");
  require(rasterizer.read_vram(172, 120) == 0xbc0fu,
          "semi-transparent texel did not blend or retain bit 15");
  require(rasterizer.read_vram(173, 120) == 0x8000u,
          "opaque texture command did not preserve texel bit 15");

  RectangleEvent forced_mask{};
  forced_mask.state = direct;
  forced_mask.state.mask.set_mask_bit = true;
  forced_mask.textured = true;
  forced_mask.raw_texture = true;
  forced_mask.origin = vertex(174, 120);
  forced_mask.width = 1;
  forced_mask.height = 1;
  forced_mask.texture_coordinate = uv(1, 3);
  consume_ok(rasterizer, Event{forced_mask});
  require(rasterizer.read_vram(174, 120) == 0x801fu,
          "forced mask bit did not override an opaque texel");

  RectangleEvent modulated{};
  modulated.state = direct;
  modulated.textured = true;
  modulated.raw_texture = false;
  modulated.origin = vertex(175, 120);
  modulated.width = 1;
  modulated.height = 1;
  modulated.texture_coordinate = uv(4, 3);
  modulated.color = color(255, 0, 0);
  consume_ok(rasterizer, Event{modulated});
  require(rasterizer.read_vram(175, 120) == 0x001du,
          "texture modulation did not use native five-bit multiplication");

  RectangleEvent reserved = forced_mask;
  reserved.state = direct;
  reserved.state.draw_mode.depth = TextureDepth::Reserved;
  reserved.origin = vertex(176, 120);
  reserved.texture_coordinate = uv(1, 3);
  consume_ok(rasterizer, Event{reserved});
  require(rasterizer.read_vram(176, 120) == 0x001fu,
          "reserved texture depth did not alias direct-15 mode");
}

void test_bounds_presentation_and_backend() {
  ReferenceRasterizer rasterizer;
  RectangleEvent rectangle{};
  rectangle.state = full_state();
  rectangle.origin = vertex(200, 200);
  rectangle.width = 2;
  rectangle.height = 2;
  rectangle.color = color(255, 255, 255);

  WorkBudget no_work{1, 1};
  const auto rejected = rasterizer.consume(Event{rectangle}, no_work);
  require(rejected.error == RasterError::WorkLimitExceeded &&
              rasterizer.read_vram(200, 200) == 0u,
          "work budget failure was not fail-closed");

  RectangleEvent invalid_texture = rectangle;
  invalid_texture.textured = true;
  invalid_texture.texture_coordinate = uv(0, 0);
  invalid_texture.state.draw_mode.depth = enum_from_byte<TextureDepth>(0xffu);
  const auto invalid_texture_result =
      rasterizer.consume(Event{invalid_texture});
  require(invalid_texture_result.error == RasterError::InvalidTexture &&
              rasterizer.read_vram(200, 200) == 0u,
          "unsupported texture depth was not rejected before drawing");

  RectangleEvent invalid_clut = rectangle;
  invalid_clut.textured = true;
  invalid_clut.texture_coordinate = uv(0, 0);
  invalid_clut.state.draw_mode.depth = TextureDepth::Indexed4;
  invalid_clut.clut = clean_aot::gpu::ClutAttribute{0, 1, 0};
  const auto invalid_clut_result = rasterizer.consume(Event{invalid_clut});
  require(invalid_clut_result.error == RasterError::InvalidTexture &&
              rasterizer.read_vram(200, 200) == 0u,
          "non-canonical CLUT address was not rejected before drawing");

  RectangleEvent invalid_offset = rectangle;
  invalid_offset.state.draw_offset.x = 1024;
  const auto invalid_offset_result = rasterizer.consume(Event{invalid_offset});
  require(invalid_offset_result.error == RasterError::MalformedEvent &&
              rasterizer.read_vram(200, 200) == 0u,
          "out-of-range signed draw offset was not rejected");

  RectangleEvent invalid_draw_area = rectangle;
  invalid_draw_area.state.draw_area.right = 1024;
  const auto invalid_draw_area_result =
      rasterizer.consume(Event{invalid_draw_area});
  require(invalid_draw_area_result.error == RasterError::MalformedEvent &&
              rasterizer.read_vram(200, 200) == 0u,
          "out-of-range draw area was not rejected");

  clean_aot::gpu::CacheFlushEvent cache_flush{};
  const auto cache_flush_result = rasterizer.consume(Event{cache_flush});
  require(cache_flush_result.status == EventStatus::Ignored &&
              cache_flush_result.work_units == 1u,
          "canonical cache-flush marker was not accepted");
  cache_flush.kind = EventKind::Polygon;
  require(rasterizer.consume(Event{cache_flush}).error ==
              RasterError::MalformedEvent,
          "forged marker event ownership was accepted");

  VramUploadEvent invalid_upload =
      upload(VramPoint{200, 200}, VramExtent{2, 1}, {0x1234u, 0x5678u});
  const auto invalid_upload_result = rasterizer.consume(Event{invalid_upload});
  require(invalid_upload_result.error == RasterError::TransferDataMismatch &&
              rasterizer.read_vram(200, 200) == 0u,
          "malformed upload was not rejected before writing");

  LineEvent oversized_line{};
  oversized_line.state = full_state();
  oversized_line.polyline = true;
  oversized_line.vertices.resize(257u, vertex(0, 0));
  const auto oversized_result = rasterizer.consume(Event{oversized_line});
  require(oversized_result.error == RasterError::MalformedEvent,
          "oversized polyline was not bounded");

  LineEvent overwide_line{};
  overwide_line.state = full_state();
  overwide_line.vertices = {vertex(-1, 300), vertex(1023, 300)};
  overwide_line.first_color = color(255, 0, 0);
  const auto overwide_line_result = rasterizer.consume(Event{overwide_line});
  require(overwide_line_result.status == EventStatus::Ignored &&
              overwide_line_result.work_units == 0u &&
              rasterizer.read_vram(0, 300) == 0u,
          "1024-pixel line delta was not hardware-culled");

  PolygonEvent overtall_triangle{};
  overtall_triangle.state = full_state();
  overtall_triangle.vertex_count = 3;
  overtall_triangle.vertices[0] = vertex(300, -1);
  overtall_triangle.vertices[1] = vertex(301, -1);
  overtall_triangle.vertices[2] = vertex(300, 511);
  overtall_triangle.first_color = color(255, 0, 0);
  const auto overtall_result = rasterizer.consume(Event{overtall_triangle});
  require(overtall_result.status == EventStatus::Ignored &&
              overtall_result.work_units == 0u &&
              rasterizer.read_vram(300, 0) == 0u,
          "512-pixel polygon delta was not hardware-culled");

  LineEvent maximum_line{};
  maximum_line.state = full_state();
  maximum_line.vertices = {vertex(0, 320), vertex(1023, 320)};
  maximum_line.first_color = color(255, 0, 0);
  consume_ok(rasterizer, Event{maximum_line});
  require(rasterizer.read_vram(0, 320) == 0x001fu &&
              rasterizer.read_vram(1023, 320) == 0x001fu,
          "maximum valid line delta was incorrectly culled");

  const auto plan = PresentationMapper::map(
      PresentationRequest{1920, 1080, VramRect{0, 0, 320, 240},
                          PresentationMode::Identity4x3, 4, 3});
  require(plan.accepted && plan.plan.identity_4x3 &&
              plan.plan.destination_x == 240u &&
              plan.plan.destination_y == 0u &&
              plan.plan.destination_width == 1440u &&
              plan.plan.destination_height == 1080u,
          "identity 4:3 presentation mapping is incorrect");

  const auto ultrawide = PresentationMapper::map(
      PresentationRequest{2560, 1080, VramRect{0, 0, 320, 240},
                          PresentationMode::Generic32x9, 4, 3});
  require(ultrawide.accepted && !ultrawide.plan.identity_4x3 &&
              ultrawide.plan.aspect_width == 32u &&
              ultrawide.plan.aspect_height == 9u &&
              ultrawide.plan.destination_width == 2560u &&
              ultrawide.plan.destination_height == 720u,
          "32:9 presentation seam is incorrect");

  const auto invalid_plan = PresentationMapper::map(PresentationRequest{
      640, 480, VramRect{1024, 0, 1, 1}, PresentationMode::Identity4x3, 4, 3});
  require(!invalid_plan.accepted &&
              invalid_plan.error == RasterError::InvalidPresentationRequest,
          "invalid presentation source was not rejected");

  const auto invalid_mode = PresentationMapper::map(
      PresentationRequest{640, 480, VramRect{0, 0, 320, 240},
                          enum_from_byte<PresentationMode>(0xffu), 4, 3});
  require(!invalid_mode.accepted &&
              invalid_mode.error == RasterError::InvalidPresentationRequest,
          "unknown presentation mode was not rejected");

  const auto maximum_plan = PresentationMapper::map(PresentationRequest{
      std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max(), VramRect{0, 0, 1024, 512},
      PresentationMode::Custom, std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max()});
  require(maximum_plan.accepted && maximum_plan.plan.destination_x == 0u &&
              maximum_plan.plan.destination_y == 0u &&
              maximum_plan.plan.destination_width ==
                  std::numeric_limits<std::uint32_t>::max() &&
              maximum_plan.plan.destination_height ==
                  std::numeric_limits<std::uint32_t>::max(),
          "maximum presentation dimensions overflowed");

  const auto subpixel_plan = PresentationMapper::map(
      PresentationRequest{1, 1, VramRect{0, 0, 1, 1}, PresentationMode::Custom,
                          std::numeric_limits<std::uint32_t>::max(), 1});
  require(!subpixel_plan.accepted &&
              subpixel_plan.error == RasterError::InvalidPresentationRequest,
          "zero-sized integer presentation result was accepted");

  const auto backend = rasterizer.native_backend();
  require(clean_aot::raster::native_backend_compatible(backend) &&
              backend.read_vram != nullptr && backend.digest != nullptr &&
              backend.plan_presentation != nullptr,
          "reference backend table is incomplete");
  WorkBudget backend_budget{};
  Event backend_event{rectangle};
  const auto dispatched =
      backend.consume_event(backend.context, &backend_event, &backend_budget);
  require(dispatched.accepted() &&
              backend.digest(backend.context) == rasterizer.digest(),
          "native backend dispatch did not use the reference context");
  require(
      backend.consume_event(nullptr, &backend_event, &backend_budget).error ==
              RasterError::MalformedEvent &&
          backend.consume_event(backend.context, nullptr, &backend_budget)
                  .error == RasterError::MalformedEvent &&
          backend.read_vram(nullptr).empty() && backend.digest(nullptr) == 0u &&
          !backend.plan_presentation(backend.context, nullptr).accepted,
      "native backend null-boundary handling is not fail-closed");

  auto wrong_version = backend;
  ++wrong_version.abi_version;
  require(!clean_aot::raster::native_backend_compatible(wrong_version),
          "native backend accepted an unknown ABI version");
  auto missing_consumer = backend;
  missing_consumer.consume_event = nullptr;
  require(!clean_aot::raster::native_backend_compatible(missing_consumer),
          "native backend accepted a missing mandatory callback");
}

void test_adversarial_coordinates_and_budgets() {
  ReferenceRasterizer rasterizer;

  LineEvent minimum_line{};
  minimum_line.state = full_state();
  minimum_line.state.draw_offset.x = -1024;
  minimum_line.state.draw_offset.y = -1024;
  minimum_line.vertices = {
      vertex(std::numeric_limits<std::int16_t>::min(),
             std::numeric_limits<std::int16_t>::min()),
      vertex(static_cast<std::int16_t>(
                 std::numeric_limits<std::int16_t>::min() + 1),
             static_cast<std::int16_t>(
                 std::numeric_limits<std::int16_t>::min() + 1))};
  minimum_line.first_color = color(255, 255, 255);
  const auto minimum_result = rasterizer.consume(Event{minimum_line});
  require(minimum_result.status == EventStatus::Applied &&
              minimum_result.work_units == 2u &&
              rasterizer.digest() == ReferenceRasterizer{}.digest(),
          "minimum signed coordinates were not handled deterministically");

  LineEvent maximum_line{};
  maximum_line.state = full_state();
  maximum_line.state.draw_offset.x = 1023;
  maximum_line.state.draw_offset.y = 1023;
  maximum_line.vertices = {
      vertex(static_cast<std::int16_t>(
                 std::numeric_limits<std::int16_t>::max() - 1),
             static_cast<std::int16_t>(
                 std::numeric_limits<std::int16_t>::max() - 1)),
      vertex(std::numeric_limits<std::int16_t>::max(),
             std::numeric_limits<std::int16_t>::max())};
  maximum_line.first_color = color(255, 255, 255);
  const auto maximum_result = rasterizer.consume(Event{maximum_line});
  require(maximum_result.status == EventStatus::Applied &&
              maximum_result.work_units == 2u,
          "maximum signed coordinates were not handled deterministically");

  RectangleEvent rectangle{};
  rectangle.state = full_state();
  rectangle.origin = vertex(300, 300);
  rectangle.width = 2;
  rectangle.height = 2;
  rectangle.color = color(255, 0, 0);

  WorkBudget insufficient{2, 3};
  const auto insufficient_result =
      rasterizer.consume(Event{rectangle}, insufficient);
  require(insufficient_result.error == RasterError::WorkLimitExceeded &&
              insufficient.max_events == 2u &&
              insufficient.max_work_units == 3u &&
              rasterizer.read_vram(300, 300) == 0u,
          "failed reservation mutated budget or VRAM");

  WorkBudget exact{1, 4};
  const auto exact_result = rasterizer.consume(Event{rectangle}, exact);
  require(exact_result.accepted() && exact.max_events == 0u &&
              exact.max_work_units == 0u &&
              rasterizer.read_vram(300, 300) == 0x001fu,
          "exact work reservation did not consume its complete budget");
  const auto exhausted_result = rasterizer.consume(Event{rectangle}, exact);
  require(exhausted_result.error == RasterError::WorkLimitExceeded,
          "exhausted event budget accepted another event");

  ReferenceRasterizer per_event_limited(
      clean_aot::raster::RasterLimits{clean_aot::gpu::kMaxPolylineVertices, 3});
  require(per_event_limited.consume(Event{rectangle}).error ==
              RasterError::WorkLimitExceeded,
          "per-event work limit did not fail closed");

  RectangleEvent invalid_mode = rectangle;
  invalid_mode.state.draw_mode.semi_transparency =
      enum_from_byte<clean_aot::gpu::SemiTransparencyMode>(0xffu);
  require(rasterizer.consume(Event{invalid_mode}).error ==
              RasterError::MalformedEvent,
          "unknown semi-transparency mode was accepted");
}

std::uint64_t golden_scene_digest() {
  ReferenceRasterizer rasterizer;
  RenderStateSnapshot state = full_state();
  state.draw_mode.dither = true;

  consume_ok(rasterizer, Event{upload(VramPoint{0, 0}, VramExtent{4, 1},
                                      {0x7c0003e0u, 0x00007fffu})});
  RectangleEvent rectangle{};
  rectangle.state = state;
  rectangle.origin = vertex(4, 4);
  rectangle.width = 4;
  rectangle.height = 2;
  rectangle.color = color(128, 64, 192);
  consume_ok(rasterizer, Event{rectangle});

  PolygonEvent polygon{};
  polygon.state = state;
  polygon.vertex_count = 3;
  polygon.vertices[0] = vertex(8, 4);
  polygon.vertices[1] = vertex(14, 4);
  polygon.vertices[2] = vertex(8, 10);
  polygon.first_color = color(255, 32, 64);
  consume_ok(rasterizer, Event{polygon});

  LineEvent line{};
  line.state = state;
  line.vertices = {vertex(0, 0), vertex(15, 7)};
  line.first_color = color(64, 255, 96);
  consume_ok(rasterizer, Event{line});
  return rasterizer.digest();
}

void test_golden_digest() {
  const std::uint64_t digest = golden_scene_digest();
  std::cout << "golden_digest=0x" << std::hex << std::setw(16)
            << std::setfill('0') << digest << std::dec << '\n';
  require(digest == 0xeca53c1328e8cc30ull,
          "golden VRAM digest changed unexpectedly");
}

void test_scanout_boundaries() {
  ReferenceRasterizer generation_raster;
  const std::uint64_t initial_generation = generation_raster.vram_generation();
  clean_aot::gpu::CacheFlushEvent marker{};
  consume_ok(generation_raster, Event{marker});
  require(generation_raster.vram_generation() == initial_generation,
          "read-only marker changed texture source generation");
  VramFillEvent generation_write{};
  generation_write.origin = VramPoint{0u, 0u};
  generation_write.extent = VramExtent{16u, 1u};
  generation_write.color = color(255u, 0u, 0u);
  consume_ok(generation_raster, Event{generation_write});
  require(generation_raster.vram_generation() == initial_generation + 1u,
          "VRAM write did not advance texture source generation");

  DisplayTiming display{};
  display.display_enabled = true;
  display.display_start_x = 0u;
  display.display_start_y = 0u;
  display.horizontal_start = 0x200u;
  display.horizontal_end = 0xc00u;
  display.vertical_start = 0x10u;
  display.vertical_end = 0x100u;
  display.horizontal_resolution = 1u;

  const auto baseline = ScanoutMapper::plan(ScanoutRequest{
      display, ScanoutField::Progressive, 3840u, 2160u,
      PresentationMode::Identity4x3, 4u, 3u});
  require(baseline.accepted && baseline.plan.logical_width == 320u &&
              baseline.plan.logical_height == 240u &&
              baseline.plan.pixel_format == ScanoutPixelFormat::Rgb555 &&
              baseline.plan.presentation.identity_4x3 &&
              baseline.plan.presentation.destination_x == 480u &&
              baseline.plan.presentation.destination_width == 2880u &&
              baseline.plan.presentation.destination_height == 2160u &&
              baseline.plan.horizontal_start == display.horizontal_start &&
              baseline.plan.vertical_end == display.vertical_end,
          "4K scanout did not preserve the exact 4:3 baseline/boundaries");

  std::vector<VramWord> vram(kVramWordCount, 0u);
  vram[0] = 0x7c1fu;
  std::vector<std::uint8_t> rgb(baseline.plan.required_rgb_bytes, 0u);
  const auto decoded = ScanoutMapper::decode_rgb888(baseline.plan, vram, rgb);
  require(decoded.accepted() && decoded.work_units == 320u * 240u &&
              rgb[0] == 255u && rgb[1] == 0u && rgb[2] == 255u,
          "15-bit scanout channel expansion is incorrect");
  const auto too_small = ScanoutMapper::decode_rgb888(
      baseline.plan, vram, std::span<std::uint8_t>(rgb).first(rgb.size() - 1u));
  require(too_small.error == RasterError::ScanoutBufferTooSmall,
          "undersized scanout destination was not rejected");
  auto disabled_plan = baseline.plan;
  disabled_plan.display_enabled = false;
  std::fill(rgb.begin(), rgb.end(), 0xffu);
  require(ScanoutMapper::decode_rgb888(disabled_plan, vram, rgb).accepted() &&
              rgb.front() == 0u && rgb.back() == 0u,
          "disabled display did not produce bounded black scanout");

  display.display_depth_24 = true;
  display.display_start_x = 1023u;
  const auto packed = ScanoutMapper::plan(ScanoutRequest{
      display, ScanoutField::Progressive, 2560u, 1080u,
      PresentationMode::Generic32x9, 4u, 3u});
  require(packed.accepted &&
              packed.plan.pixel_format == ScanoutPixelFormat::PackedRgb888 &&
              packed.plan.presentation.aspect_width == 32u,
          "packed 24-bit ultrawide scanout plan is incorrect");
  vram[1023] = 0x2211u;
  vram[0] = 0x4433u;
  vram[1] = 0x6655u;
  rgb.assign(packed.plan.required_rgb_bytes, 0u);
  require(ScanoutMapper::decode_rgb888(packed.plan, vram, rgb).accepted() &&
              rgb[0] == 0x11u && rgb[1] == 0x22u && rgb[2] == 0x33u &&
              rgb[3] == 0x44u && rgb[4] == 0x55u && rgb[5] == 0x66u,
          "24-bit scanout packing did not wrap within the VRAM row");

  display.display_depth_24 = false;
  display.display_start_x = 0u;
  display.vertical_resolution_480 = true;
  display.interlaced = true;
  const auto odd = ScanoutMapper::plan(ScanoutRequest{
      display, ScanoutField::Odd, 1920u, 1080u,
      PresentationMode::Identity4x3, 4u, 3u});
  require(odd.accepted && odd.plan.logical_height == 240u &&
              odd.plan.source_line_step == 2u &&
              odd.plan.source_field_offset == 1u,
          "480-line odd-field scanout boundary is incorrect");
  const auto impossible_progressive = ScanoutMapper::plan(ScanoutRequest{
      display, ScanoutField::Progressive, 1920u, 1080u,
      PresentationMode::Identity4x3, 4u, 3u});
  require(!impossible_progressive.accepted,
          "interlaced scanout accepted an impossible progressive field");

  display.interlaced = false;
  display.horizontal_resolution = 4u;
  const auto forged_mode = ScanoutMapper::plan(ScanoutRequest{
      display, ScanoutField::Progressive, 1920u, 1080u,
      PresentationMode::Identity4x3, 4u, 3u});
  require(!forged_mode.accepted,
          "out-of-domain horizontal resolution was silently masked");
}

void test_precise_transparency_safe_filtering() {
  using clean_aot::raster::PreciseCullingPolicy;
  using clean_aot::raster::PreciseVertex;
  using clean_aot::raster::TextureFilterMode;
  ReferenceRasterizer rasterizer;
  consume_ok(rasterizer, Event{upload(
      VramPoint{64, 0}, VramExtent{2, 2},
      {0x03e0001fu, 0x7fff7c00u})});

  PolygonEvent polygon{};
  polygon.vertex_count = 3U;
  polygon.textured = true;
  polygon.raw_texture = true;
  polygon.state = full_state();
  polygon.state.draw_mode.x_base = 1U;
  polygon.state.draw_mode.depth = TextureDepth::Direct15;
  polygon.vertices[0] = vertex(10, 10);
  polygon.vertices[1] = vertex(30, 10);
  polygon.vertices[2] = vertex(10, 30);
  constexpr double texel = 1.0 / 255.0;
  const auto precise = [](double x, double y, double u, double v) {
    return PreciseVertex{x, y, 1.0, 1.0, u, v, 0xffffffffU, 1U, 1U};
  };
  std::array<PreciseVertex, 3U> vertices{
      precise(10.0, 10.0, 0.0, 0.0),
      precise(30.0, 10.0, texel, 0.0),
      precise(10.0, 30.0, 0.0, texel)};
  require(rasterizer.consume_precise_polygon(polygon, vertices).accepted() &&
              rasterizer.read_vram(15, 15) == 0x001fu,
          "default precise filtering changed native nearest sampling");

  for (PreciseVertex& point : vertices) point.x += 30.0;
  require(rasterizer.consume_precise_polygon(
              polygon, vertices, nullptr, PreciseCullingPolicy::NativeHardware,
              TextureFilterMode::BilinearTransparencySafe).accepted() &&
              rasterizer.read_vram(45, 15) == 0x2113u,
          "opaque precise footprint did not receive deterministic bilinear filtering");
  const auto opaque_stats = rasterizer.precise_stats();
  require(opaque_stats.bilinear_fragments != 0U &&
              opaque_stats.bilinear_texture_fetches ==
                  4U * (opaque_stats.bilinear_fragments +
                        opaque_stats.transparency_fallbacks) &&
              opaque_stats.bilinear_texture_fetches <=
                  opaque_stats.precise_fragments * 4U,
          "bilinear hot path exceeded its fixed four-fetch fragment budget");

  consume_ok(rasterizer, Event{upload(
      VramPoint{64, 0}, VramExtent{2, 2},
      {0x03e00000u, 0x7fff7c00u})});
  for (PreciseVertex& point : vertices) point.x += 30.0;
  const std::uint64_t fallbacks = rasterizer.precise_stats().transparency_fallbacks;
  require(rasterizer.consume_precise_polygon(
              polygon, vertices, nullptr, PreciseCullingPolicy::NativeHardware,
              TextureFilterMode::BilinearTransparencySafe).accepted() &&
              rasterizer.read_vram(75, 15) == 0U &&
              rasterizer.precise_stats().transparency_fallbacks > fallbacks,
          "transparent footprint was filtered across a PS1 cutout edge");
}

} // namespace

int main() {
  try {
    test_vram_transfers_and_handoff();
    test_hardware_vram_overlap_golden();
    test_wrapped_transfer_aliasing();
    test_primitives_and_state();
    test_hardware_line_semantics();
    test_hardware_triangle_golden();
    test_hardware_uv_and_color_interpolation_golden();
    test_textures();
    test_texture_mask_transparency_and_modulation();
    test_bounds_presentation_and_backend();
    test_adversarial_coordinates_and_budgets();
    test_precise_transparency_safe_filtering();
    test_scanout_boundaries();
    test_golden_digest();
  } catch (const TestFailure &failure) {
    std::cerr << "FAIL: " << failure.what() << '\n';
    return 1;
  } catch (const std::exception &failure) {
    std::cerr << "UNEXPECTED: " << failure.what() << '\n';
    return 1;
  }
  std::cout << "clean_aot_raster_tests: PASS\n";
  return 0;
}
