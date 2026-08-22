#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "clean_aot/gpu/gpu.hpp"

namespace {

using clean_aot::gpu::Color;
using clean_aot::gpu::EnvironmentEvent;
using clean_aot::gpu::Event;
using clean_aot::gpu::FrontEnd;
using clean_aot::gpu::Gp0InputPhase;
using clean_aot::gpu::LineEvent;
using clean_aot::gpu::PolygonEvent;
using clean_aot::gpu::RectangleEvent;
using clean_aot::gpu::TextureDepth;
using clean_aot::gpu::VramCopyEvent;
using clean_aot::gpu::VramDownloadEvent;
using clean_aot::gpu::VramFillEvent;
using clean_aot::gpu::VramUploadEvent;
using clean_aot::gpu::WriteError;

class TestFailure final : public std::runtime_error {
 public:
  explicit TestFailure(const std::string& message)
      : std::runtime_error(message) {}
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw TestFailure(message);
  }
}

template <typename T>
T take_event(FrontEnd& gpu, const std::string& message) {
  const std::optional<Event> event = gpu.pop_event();
  if (!event.has_value()) {
    throw TestFailure(message + ": missing event");
  }
  const T* typed = std::get_if<T>(&event.value());
  require(typed != nullptr, message + ": wrong event type");
  return *typed;
}

std::uint32_t vertex_word(std::int32_t x, std::int32_t y) {
  const std::uint32_t packed_x =
      static_cast<std::uint32_t>(static_cast<std::uint16_t>(x)) & 0x07ffu;
  const std::uint32_t packed_y =
      static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) & 0x07ffu;
  return packed_x | (packed_y << 16u);
}

std::uint32_t point_word(std::uint32_t x, std::uint32_t y) {
  return (x & 0x03ffu) | ((y & 0x01ffu) << 16u);
}

std::uint32_t extent_word(std::uint32_t width, std::uint32_t height) {
  return (width & 0xffffu) | ((height & 0xffffu) << 16u);
}

constexpr std::uint32_t gp0_word(std::uint8_t opcode,
                                 std::uint32_t parameter = 0u) noexcept {
  return (static_cast<std::uint32_t>(opcode) << 24u) |
         (parameter & 0x00ffffffu);
}

constexpr std::uint32_t pixel_pair(std::uint16_t low,
                                   std::uint16_t high) noexcept {
  return static_cast<std::uint32_t>(low) |
         (static_cast<std::uint32_t>(high) << 16u);
}

void send(FrontEnd& gpu, std::uint32_t word, const std::string& message) {
  const auto result = gpu.write_gp0(word);
  require(result.accepted,
          message + ": GP0 write rejected with error " +
              std::to_string(static_cast<unsigned>(result.error)));
}

void send_gp1(FrontEnd& gpu, std::uint32_t word, const std::string& message) {
  const auto result = gpu.write_gp1(word);
  require(result.accepted,
          message + ": GP1 write rejected with error " +
              std::to_string(static_cast<unsigned>(result.error)));
}

void send_flat_textured_triangle(FrontEnd& gpu, std::uint16_t texture_page,
                                 std::uint32_t command = 0x24010203u) {
  send(gpu, command, "textured triangle command");
  send(gpu, vertex_word(1, 2), "textured triangle vertex 0");
  send(gpu, 0x00400010u, "textured triangle CLUT/UV");
  send(gpu, vertex_word(3, 4), "textured triangle vertex 1");
  send(gpu, static_cast<std::uint32_t>(texture_page) << 16u,
       "textured triangle page/UV");
  send(gpu, vertex_word(5, 6), "textured triangle vertex 2");
  send(gpu, 0x00000030u, "textured triangle UV 2");
}

std::uint64_t mix_byte(std::uint64_t hash, std::uint8_t byte) {
  return (hash ^ static_cast<std::uint64_t>(byte)) * 1099511628211ull;
}

std::uint64_t mix_word(std::uint64_t hash, std::uint32_t word) {
  hash = mix_byte(hash, static_cast<std::uint8_t>(word & 0xffu));
  hash = mix_byte(hash, static_cast<std::uint8_t>((word >> 8u) & 0xffu));
  hash = mix_byte(hash, static_cast<std::uint8_t>((word >> 16u) & 0xffu));
  return mix_byte(hash, static_cast<std::uint8_t>((word >> 24u) & 0xffu));
}

std::uint64_t event_digest(const Event& event) {
  std::uint64_t hash = 14695981039346656037ull;
  std::visit(
      [&hash](const auto& item) {
        hash = mix_word(hash, static_cast<std::uint32_t>(item.kind));
        if constexpr (requires { item.raw_word_count; }) {
          for (std::size_t index = 0; index < item.raw_word_count; ++index) {
            hash = mix_word(hash, item.raw_words[index]);
          }
        } else if constexpr (requires { item.raw_words; }) {
          for (const std::uint32_t word : item.raw_words) {
            hash = mix_word(hash, word);
          }
        }
        if constexpr (requires { item.raw_word; }) {
          hash = mix_word(hash, item.raw_word);
        }
        if constexpr (requires { item.data_words; }) {
          for (const std::uint32_t word : item.data_words) {
            hash = mix_word(hash, word);
          }
        }
      },
      event);
  return hash;
}

std::vector<std::uint64_t> drain_event_digests(FrontEnd& gpu) {
  std::vector<std::uint64_t> result;
  while (const std::optional<Event> event = gpu.pop_event()) {
    result.push_back(event_digest(*event));
  }
  return result;
}

void test_reset_and_gp1_state() {
  FrontEnd gpu;
  require(gpu.status() == 0x14802000u,
          "power-on GPUSTAT differs from contract");
  require(gpu.vram().digest() == 0xa96777069d622325ull,
          "zeroed VRAM digest is not deterministic: " +
              std::to_string(gpu.vram().digest()));

  send_gp1(gpu, 0x03000000u, "display enable");
  send_gp1(gpu, 0x04000002u, "DMA direction");
  send_gp1(gpu, 0x05012345u, "display start");
  send_gp1(gpu, 0x06123456u, "horizontal range");
  send_gp1(gpu, 0x07123456u, "vertical range");
  send_gp1(gpu, 0x080000e9u, "display mode");
  send_gp1(gpu, 0x09000001u, "texture-disable permission latch");
  gpu.set_scanout_state(true, false);

  const auto& control = gpu.control();
  require(control.display.display_enabled, "display enable state not retained");
  require(control.texture_disable_allowed,
          "GP1(09) texture-disable permission not retained");
  require(control.display.video_standard == clean_aot::gpu::VideoStandard::Pal,
          "PAL display mode not decoded");
  require(control.display.interlaced, "interlace mode not decoded");
  require(control.display.odd_field, "display field handoff not retained");
  require((gpu.status() & (3u << 29u)) == (2u << 29u),
          "DMA direction status bit not exposed");
  require((gpu.status() & (1u << 31u)) != 0u,
          "odd display field status bit not exposed");
  require(gpu.read_dma_word().error ==
              clean_aot::gpu::ReadError::DmaDirectionMismatch,
          "DMA read ignored the CPU-to-GP0 direction");
  const auto dma_write = gpu.write_dma_word(0x00000000u);
  require(dma_write.accepted, "DMA write did not use the CPU-to-GP0 direction");
  require(gpu.event_count() == 0u,
          "hardware NOP unexpectedly consumed renderer-event capacity");

  send_gp1(gpu, 0x10000002u, "texture window read");
  require(gpu.read_gpuread().ready, "GPUREAD register latch not ready");
  send(gpu, 0xe2001234u, "texture window");
  const auto window = take_event<EnvironmentEvent>(gpu, "texture window event");
  require(window.environment == clean_aot::gpu::EnvironmentKind::TextureWindow,
          "wrong texture window event");
  send_gp1(gpu, 0x10000002u, "texture window read after update");
  require(gpu.read_gpuread().word == 0x00001234u,
          "GP1(10) texture window latch mismatch");

  send(gpu, 0xe3001008u, "draw-area top-left");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "draw-area top-left event"));
  send(gpu, 0xe4002010u, "draw-area bottom-right");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "draw-area bottom-right event"));
  send(gpu, 0xe5000081u, "draw offset");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "draw offset event"));
  send_gp1(gpu, 0x10000003u, "draw-area top-left read");
  require(gpu.read_gpuread().word == 0x00001008u,
          "GP1(10) draw-area top-left readback mismatch");
  send_gp1(gpu, 0x10000004u, "draw-area bottom-right read");
  require(gpu.read_gpuread().word == 0x00002010u,
          "GP1(10) draw-area bottom-right readback mismatch");
  send_gp1(gpu, 0x10000005u, "draw offset read");
  require(gpu.read_gpuread().word == 0x00000081u,
          "GP1(10) draw offset readback mismatch");

  send(gpu, 0xe6000003u, "mask environment");
  const auto mask_event = take_event<EnvironmentEvent>(gpu, "mask event");
  require(mask_event.state.mask.set_mask_bit &&
              mask_event.state.mask.check_mask_bit,
          "mask state was not decoded");
  require((gpu.status() & 0x1800u) == 0x1800u,
          "mask bits missing from GPUSTAT");

  send_gp1(gpu, 0x00000000u, "soft reset");
  require(gpu.status() == 0x14802000u,
          "soft reset did not restore architectural defaults");
  require(gpu.control().texture_disable_allowed,
          "GP1(00) incorrectly cleared GP1(09) permission latch");
}

void test_polygon_line_and_rectangle_framing() {
  FrontEnd gpu;
  send(gpu, 0x3f102030u, "gouraud textured quad command");
  send(gpu, vertex_word(-10, 20), "quad vertex 0");
  send(gpu, 0x00400080u, "quad UV/CLUT 0");
  send(gpu, 0x000a141eu, "quad color 1");
  send(gpu, vertex_word(30, 40), "quad vertex 1");
  send(gpu, 0x08900010u, "quad UV/page 1");
  send(gpu, 0x00010203u, "quad color 2");
  send(gpu, vertex_word(50, -60), "quad vertex 2");
  send(gpu, 0x00002030u, "quad UV 2");
  send(gpu, 0x00040506u, "quad color 3");
  send(gpu, vertex_word(70, 80), "quad vertex 3");
  send(gpu, 0x00004050u, "quad UV 3");
  const PolygonEvent polygon = take_event<PolygonEvent>(gpu, "polygon event");
  require(polygon.gouraud && polygon.quad && polygon.textured &&
              polygon.semi_transparent && polygon.raw_texture,
          "polygon flags were not preserved");
  require(polygon.vertex_count == 4u && polygon.vertices[0].x == -10 &&
              polygon.vertices[2].y == -60,
          "polygon vertex sign extension failed");
  require(polygon.color_present[3] && polygon.colors[3].red == 6u,
          "polygon gouraud color framing failed");
  require(polygon.texture_coordinates[0].u == 0x80u &&
              polygon.texture_coordinates[0].upper == 0x0040u,
          "polygon UV/CLUT preservation failed");
  require(polygon.clut.has_value() && polygon.clut->x == 0u &&
              polygon.clut->y == 1u,
          "polygon CLUT decode failed");
  require(polygon.inline_texture_page.has_value() &&
              polygon.inline_texture_page->x_base == 0u &&
              polygon.inline_texture_page->y_base == 256u &&
              polygon.inline_texture_page->depth == TextureDepth::Indexed8,
          "polygon inline texture-page decode failed");
  require(polygon.raw_word_count == 12u &&
              polygon.raw_words[0] == 0x3f102030u &&
              polygon.raw_words[11] == 0x00004050u,
          "polygon raw packet length or boundary words were not preserved");

  send(gpu, 0x200000ffu, "flat triangle command");
  send(gpu, vertex_word(1, 2), "flat triangle vertex 0");
  send(gpu, vertex_word(3, 4), "flat triangle vertex 1");
  send(gpu, vertex_word(5, 6), "flat triangle vertex 2");
  const PolygonEvent flat_triangle =
      take_event<PolygonEvent>(gpu, "flat triangle event");
  require(!flat_triangle.gouraud && !flat_triangle.quad &&
              !flat_triangle.textured && flat_triangle.vertex_count == 3u &&
              !flat_triangle.color_present[1],
          "flat triangle framing failed");
  require(flat_triangle.raw_word_count == 4u &&
              flat_triangle.raw_words[3] == vertex_word(5, 6),
          "flat triangle raw packet length was not preserved");

  send(gpu, 0x40010203u, "flat line command");
  send(gpu, vertex_word(1, 2), "line vertex 0");
  send(gpu, vertex_word(3, 4), "line vertex 1");
  const LineEvent line = take_event<LineEvent>(gpu, "line event");
  require(!line.gouraud && !line.polyline && line.vertices.size() == 2u,
          "flat line framing failed");
  require(line.raw_words.size() == 3u,
          "flat line raw packet length was not preserved");

  send(gpu, 0x58112233u, "gouraud polyline command");
  send(gpu, vertex_word(5, 6), "polyline vertex 0");
  send(gpu, 0x00010203u, "polyline color 1");
  send(gpu, vertex_word(7, 8), "polyline vertex 1");
  send(gpu, 0x00040506u, "polyline color 2");
  send(gpu, vertex_word(9, 10), "polyline vertex 2");
  send(gpu, 0x50005000u, "polyline terminator");
  const LineEvent polyline = take_event<LineEvent>(gpu, "polyline event");
  require(polyline.gouraud && polyline.polyline &&
              polyline.vertices.size() == 3u && polyline.colors.size() == 3u,
          "variable gouraud polyline framing failed");
  require(polyline.terminator_word == 0x50005000u,
          "polyline terminator was not preserved");

  send(gpu, 0x640a0b0cu, "textured variable rectangle command");
  send(gpu, vertex_word(-1, -2), "rectangle origin");
  send(gpu, 0x00400040u, "rectangle UV/CLUT");
  send(gpu, extent_word(31, 17), "rectangle extent");
  const RectangleEvent rectangle =
      take_event<RectangleEvent>(gpu, "rectangle event");
  require(rectangle.textured && rectangle.size_code == 0u &&
              rectangle.raw_extent_present && rectangle.width == 31u &&
              rectangle.height == 17u,
          "variable rectangle framing failed");
  require(
      rectangle.texture_coordinate.has_value() && rectangle.clut.has_value() &&
          rectangle.clut->x == 0u,
      "rectangle CLUT preservation failed: uv=" +
          std::to_string(rectangle.texture_coordinate.has_value()) +
          " clut=" + std::to_string(rectangle.clut.has_value()) + " x=" +
          std::to_string(rectangle.clut.has_value() ? rectangle.clut->x : 0u));
  require(rectangle.raw_word_count == 4u &&
              rectangle.raw_words[3] == extent_word(31, 17),
          "variable rectangle raw packet length was not preserved");

  send(gpu, 0x68000000u, "1x1 rectangle command");
  send(gpu, vertex_word(12, 13), "sprite origin");
  const RectangleEvent sprite = take_event<RectangleEvent>(gpu, "sprite event");
  require(sprite.size_code == 1u && sprite.width == 1u && sprite.height == 1u,
          "single-pixel rectangle size decode failed");
  require(sprite.raw_word_count == 2u && !sprite.raw_extent_present &&
              sprite.raw_width == 0u && sprite.raw_height == 0u,
          "fixed-size rectangle invented an absent raw extent word");
}

void test_polyline_rejection_and_truncation() {
  FrontEnd gpu;
  send(gpu, 0x48000000u, "flat polyline command");
  send(gpu, vertex_word(0, 0), "first polyline vertex");
  const auto early = gpu.write_gp0(0x50005000u);
  require(
      !early.accepted && early.error == WriteError::PolylineTerminatorTooEarly,
      "early polyline terminator was accepted");
  require(gpu.event_count() == 0u, "rejected polyline emitted an event");

  send(gpu, 0x48000000u, "second flat polyline command");
  send(gpu, vertex_word(0, 0), "second first vertex");
  send(gpu, vertex_word(1, 1), "second vertex");
  const auto truncated = gpu.finish_gp0_stream();
  require(!truncated.accepted && truncated.error == WriteError::TruncatedPacket,
          "unterminated polyline was not reported");
  require(gpu.event_count() == 0u, "truncated polyline emitted an event");

  send(gpu, 0x44000000u, "line ignored-bit alias command");
  send(gpu, vertex_word(1, 2), "line ignored-bit alias vertex 0");
  send(gpu, vertex_word(3, 4), "line ignored-bit alias vertex 1");
  const LineEvent aliased_line =
      take_event<LineEvent>(gpu, "line ignored-bit alias event");
  require(aliased_line.opcode == 0x44u && !aliased_line.gouraud,
          "line ignored-bit alias was not framed as hardware does");
  const auto unknown = gpu.write_gp0(0x03000000u);
  require(!unknown.accepted && unknown.error == WriteError::UnknownGp0Command,
          "reserved GP0 command was not rejected");
  const auto invalid_gp1 = gpu.write_gp1(0x0a000000u);
  require(!invalid_gp1.accepted &&
              invalid_gp1.error == WriteError::InvalidGp1Command,
          "reserved GP1 command was not rejected");

  send(gpu, 0x02000000u, "truncated fixed packet");
  require(gpu.staged_word_count() == 1u,
          "partial fixed packet did not expose FIFO staging depth");
  const auto fixed_truncated = gpu.finish_gp0_stream();
  require(!fixed_truncated.accepted && gpu.staged_word_count() == 0u,
          "fixed packet truncation did not clear staging");

  for (std::size_t index = 0; index < clean_aot::gpu::kEventQueueCapacity;
       ++index) {
    const auto result = gpu.write_gp0(gp0_word(0x01u));
    require(result.accepted, "bounded event queue rejected a capacity word");
  }
  const auto nop_while_full = gpu.write_gp0(0x00000000u);
  require(nop_while_full.accepted &&
              gpu.event_count() == clean_aot::gpu::kEventQueueCapacity,
          "hardware NOP was blocked by host renderer backpressure");
  const auto queue_full = gpu.write_gp0(gp0_word(0x01u));
  require(
      !queue_full.accepted && queue_full.error == WriteError::EventQueueFull,
      "bounded event queue accepted an overflow word");
}

void test_vram_fill_copy_upload_download_and_masks() {
  FrontEnd gpu;
  send(gpu, 0xe6000000u, "clear mask state");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "clear mask event"));

  send(gpu, 0x02e0c0a0u, "fill command");
  send(gpu, point_word(0x03ffu, 511u), "fill origin");
  send(gpu, extent_word(0x0011u, 2u), "fill extent");
  const VramFillEvent fill = take_event<VramFillEvent>(gpu, "fill event");
  require(fill.origin.x == 0x03f0u && fill.extent.width == 32u &&
              fill.extent.height == 2u,
          "fill rounding/masking failed");
  const std::uint16_t fill_value = static_cast<std::uint16_t>(
      ((0xa0u >> 3u) & 0x1fu) | (((0xc0u >> 3u) & 0x1fu) << 5u) |
      (((0xe0u >> 3u) & 0x1fu) << 10u));
  require(gpu.vram().read_wrapped(0u, 511u) == fill_value &&
              gpu.vram().read_wrapped(15u, 0u) == fill_value,
          "fill did not wrap both VRAM axes deterministically");

  send(gpu, gp0_word(0xa0u), "odd upload command");
  send(gpu, point_word(1022u, 511u), "odd upload destination");
  send(gpu, extent_word(3u, 1u), "odd upload extent");
  send(gpu, pixel_pair(0x0002u, 0x8001u), "odd upload data word 0");
  send(gpu, 0x00000003u, "odd upload data word 1");
  const VramUploadEvent upload =
      take_event<VramUploadEvent>(gpu, "upload event");
  require(upload.pixel_count == 3u && upload.padded_word_count == 2u &&
              upload.data_words.size() == 2u && upload.data_words[1] == 3u &&
              upload.final_word_valid_halfwords == 1u,
          "odd upload framing/padding failed");
  require(gpu.vram().read_wrapped(1022u, 511u) == 2u &&
              gpu.vram().read_wrapped(1023u, 511u) == 0x8001u &&
              gpu.vram().read_wrapped(0u, 511u) == 3u,
          "odd upload did not wrap and write exact pixels");

  send(gpu, gp0_word(0xa0u), "copy overlap seed command");
  send(gpu, point_word(0u, 0u), "copy overlap seed destination");
  send(gpu, extent_word(4u, 1u), "copy overlap seed extent");
  send(gpu, 0x00020001u, "copy overlap seed word 0");
  send(gpu, 0x00040003u, "copy overlap seed word 1");
  static_cast<void>(take_event<VramUploadEvent>(gpu, "copy seed event"));
  send(gpu, gp0_word(0x80u), "overlap copy command");
  send(gpu, point_word(0u, 0u), "overlap source");
  send(gpu, point_word(1u, 0u), "overlap destination");
  send(gpu, extent_word(4u, 1u), "overlap extent");
  const VramCopyEvent copy = take_event<VramCopyEvent>(gpu, "copy event");
  require(copy.extent.width == 4u && gpu.vram().read_wrapped(0u, 0u) == 1u &&
              gpu.vram().read_wrapped(1u, 0u) == 1u &&
              gpu.vram().read_wrapped(2u, 0u) == 2u &&
              gpu.vram().read_wrapped(4u, 0u) == 4u,
          "horizontal overlap was not source-scanline buffered");

  send(gpu, 0xe6000001u, "set mask state");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "set mask event"));
  send(gpu, gp0_word(0xa0u), "masked upload command");
  send(gpu, point_word(20u, 20u), "masked upload destination");
  send(gpu, extent_word(1u, 1u), "masked upload extent");
  send(gpu, 0x00000007u, "masked upload data");
  static_cast<void>(take_event<VramUploadEvent>(gpu, "masked upload event"));
  require(gpu.vram().read_wrapped(20u, 20u) == 0x8007u,
          "set-mask upload rule failed");

  send(gpu, 0xe6000002u, "check mask state");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "check mask event"));
  send(gpu, gp0_word(0xa0u), "check-mask upload command");
  send(gpu, point_word(20u, 20u), "check-mask destination");
  send(gpu, extent_word(1u, 1u), "check-mask extent");
  send(gpu, 0x00000009u, "check-mask data");
  static_cast<void>(
      take_event<VramUploadEvent>(gpu, "check-mask upload event"));
  require(gpu.vram().read_wrapped(20u, 20u) == 0x8007u,
          "check-mask upload rule failed");

  send(gpu, gp0_word(0x80u), "check-mask copy command");
  send(gpu, point_word(0u, 0u), "check-mask copy source");
  send(gpu, point_word(20u, 20u), "check-mask copy destination");
  send(gpu, extent_word(1u, 1u), "check-mask copy extent");
  const VramCopyEvent checked_copy =
      take_event<VramCopyEvent>(gpu, "check-mask copy event");
  require(checked_copy.mask.check_mask_bit &&
              gpu.vram().read_wrapped(20u, 20u) == 0x8007u,
          "check-mask VRAM copy rule failed");

  send(gpu, 0xe6000001u, "set mask before copy");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "set-mask copy event"));
  send(gpu, gp0_word(0x80u), "set-mask copy command");
  send(gpu, point_word(0u, 0u), "set-mask copy source");
  send(gpu, point_word(21u, 20u), "set-mask copy destination");
  send(gpu, extent_word(1u, 1u), "set-mask copy extent");
  static_cast<void>(take_event<VramCopyEvent>(gpu, "set-mask copy event"));
  require(gpu.vram().read_wrapped(21u, 20u) == 0x8001u,
          "set-mask VRAM copy rule failed");

  send(gpu, 0xe6000003u, "mask state before fast fill");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "fast-fill mask event"));
  send(gpu, 0x020000ffu, "fast fill ignores mask command");
  send(gpu, point_word(16u, 20u), "fast fill ignores mask origin");
  send(gpu, extent_word(1u, 1u), "fast fill ignores mask extent");
  static_cast<void>(
      take_event<VramFillEvent>(gpu, "fast fill ignores mask event"));
  require(gpu.vram().read_wrapped(20u, 20u) == 0x001fu &&
              gpu.vram().read_wrapped(21u, 20u) == 0x001fu,
          "fast fill incorrectly honored mask-test/set state");

  send_gp1(gpu, 0x04000003u, "GPUREAD DMA direction");
  send(gpu, 0xc0000000u, "download command");
  send(gpu, point_word(1022u, 511u), "download source");
  send(gpu, extent_word(3u, 1u), "download extent");
  const VramDownloadEvent download =
      take_event<VramDownloadEvent>(gpu, "download event");
  require(download.data_words.size() == 2u &&
              download.data_words[0] == pixel_pair(0x0002u, 0x8001u) &&
              download.data_words[1] == 0x00000003u,
          "odd download packing failed");
  require(
      (gpu.status() & (1u << 27u)) != 0u && (gpu.status() & (1u << 25u)) != 0u,
      "download ready/DMA request status failed");
  const auto first = gpu.read_dma_word();
  const auto second = gpu.read_dma_word();
  require(first.ready && second.ready &&
              first.word == pixel_pair(0x0002u, 0x8001u) &&
              second.word == 0x00000003u,
          "GPUREAD download sequence failed");
  require((gpu.status() & (1u << 27u)) == 0u,
          "download ready bit stayed asserted after final word");
}

void test_transfer_truncation_and_reset() {
  FrontEnd gpu;
  const std::uint64_t initial_digest = gpu.vram().digest();
  send(gpu, gp0_word(0xa0u), "truncated upload command");
  send(gpu, point_word(1u, 1u), "truncated upload destination");
  send(gpu, extent_word(3u, 1u), "truncated upload extent");
  send(gpu, 0x00020001u, "truncated upload first word");
  const auto truncated = gpu.finish_gp0_stream();
  require(!truncated.accepted && truncated.error == WriteError::TruncatedPacket,
          "truncated upload was not reported");
  require(gpu.vram().digest() == initial_digest && gpu.event_count() == 0u,
          "truncated upload partially mutated state");

  send(gpu, gp0_word(0xa0u), "busy upload command");
  send(gpu, point_word(1u, 1u), "busy upload destination");
  send(gpu, extent_word(1u, 1u), "busy upload extent");
  send(gpu, 0x00000005u, "busy upload data");
  static_cast<void>(take_event<VramUploadEvent>(gpu, "busy upload event"));
  send(gpu, 0xc0000000u, "busy download command");
  send(gpu, point_word(1u, 1u), "busy download source");
  send(gpu, extent_word(1u, 1u), "busy download extent");
  static_cast<void>(take_event<VramDownloadEvent>(gpu, "busy download event"));
  const auto busy_write = gpu.write_gp0(0x00000000u);
  require(!busy_write.accepted && busy_write.error == WriteError::TransferBusy,
          "GP0 accepted a command while GPUREAD data was pending");
  require(gpu.read_gpuread().ready, "busy download did not provide data");
  send(gpu, 0x00000000u, "NOP after download");
  require(gpu.event_count() == 0u,
          "post-download NOP emitted a renderer event");

  send(gpu, 0xe6000003u, "mask before reset");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "mask before reset event"));
  send_gp1(gpu, 0x09000001u, "texture-disable permission before reset");
  send_gp1(gpu, 0x00000000u, "reset after partial state");
  require(gpu.vram().read_wrapped(1u, 1u) == 5u,
          "soft reset incorrectly cleared VRAM");
  require(gpu.control().texture_disable_allowed,
          "soft reset incorrectly cleared texture-disable permission");
  gpu.power_on_reset();
  require(gpu.vram().digest() == initial_digest && gpu.status() == 0x14802000u,
          "power-on reset did not clear VRAM and restore status");
}

void test_exhaustive_gp0_opcode_framing() {
  for (std::uint32_t raw_opcode = 0; raw_opcode <= 0xffu; ++raw_opcode) {
    const auto opcode = static_cast<std::uint8_t>(raw_opcode);
    FrontEnd gpu;
    const std::uint32_t command = raw_opcode << 24u | 0x00112233u;
    const auto first = gpu.write_gp0(command);
    const bool unsupported = opcode == 0x03u || opcode >= 0xf0u;
    if (unsupported) {
      require(!first.accepted && first.error == WriteError::UnknownGp0Command,
              "unsupported GP0 opcode was not rejected: " +
                  std::to_string(raw_opcode));
      require(gpu.input_phase() == Gp0InputPhase::Command,
              "unsupported GP0 opcode did not resynchronize parser");
      continue;
    }

    require(first.accepted,
            "known GP0 opcode was rejected: " + std::to_string(raw_opcode));
    if (opcode == 0x02u) {
      send(gpu, point_word(0, 0), "fill alias point");
      send(gpu, extent_word(1, 1), "fill alias extent");
    } else if (opcode >= 0x20u && opcode <= 0x3fu) {
      const bool gouraud = (opcode & 0x10u) != 0u;
      const bool quad = (opcode & 0x08u) != 0u;
      const bool textured = (opcode & 0x04u) != 0u;
      const std::size_t vertex_count = quad ? 4u : 3u;
      const std::size_t words = 1u + vertex_count +
                                (gouraud ? vertex_count - 1u : 0u) +
                                (textured ? vertex_count : 0u);
      for (std::size_t index = 1; index < words; ++index) {
        send(gpu, static_cast<std::uint32_t>(index),
             "polygon exhaustive parameter");
      }
    } else if (opcode >= 0x40u && opcode <= 0x5fu) {
      const bool gouraud = (opcode & 0x10u) != 0u;
      const bool polyline = (opcode & 0x08u) != 0u;
      send(gpu, vertex_word(1, 2), "line exhaustive vertex 0");
      if (gouraud) {
        send(gpu, 0xa5010203u, "line exhaustive color 1");
      }
      send(gpu, vertex_word(3, 4), "line exhaustive vertex 1");
      if (polyline) {
        send(gpu, 0x5abc5defu, "line exhaustive terminator");
      }
    } else if (opcode >= 0x60u && opcode <= 0x7fu) {
      const bool textured = (opcode & 0x04u) != 0u;
      const bool variable = ((opcode >> 3u) & 0x03u) == 0u;
      send(gpu, vertex_word(1, 2), "rectangle exhaustive origin");
      if (textured) {
        send(gpu, 0x00400010u, "rectangle exhaustive texture word");
      }
      if (variable) {
        send(gpu, extent_word(1, 1), "rectangle exhaustive extent");
      }
    } else if (opcode >= 0x80u && opcode <= 0x9fu) {
      send(gpu, point_word(0, 0), "copy exhaustive source");
      send(gpu, point_word(1, 1), "copy exhaustive destination");
      send(gpu, extent_word(1, 1), "copy exhaustive extent");
    } else if (opcode >= 0xa0u && opcode <= 0xbfu) {
      send(gpu, point_word(0, 0), "upload exhaustive destination");
      send(gpu, extent_word(1, 1), "upload exhaustive extent");
      send(gpu, 0xbeef1234u, "upload exhaustive data");
    } else if (opcode >= 0xc0u && opcode <= 0xdfu) {
      send(gpu, point_word(0, 0), "download exhaustive source");
      send(gpu, extent_word(1, 1), "download exhaustive extent");
    }

    const bool nop = opcode == 0x00u || (opcode >= 0x04u && opcode <= 0x1eu) ||
                     opcode == 0xe0u || (opcode >= 0xe7u && opcode <= 0xefu);
    require(gpu.event_count() == (nop ? 0u : 1u),
            "known GP0 opcode emitted the wrong event count: " +
                std::to_string(raw_opcode));
    if (nop) {
      continue;
    }
    const std::optional<Event> event = gpu.pop_event();
    require(event.has_value(), "exhaustive GP0 event disappeared");
    if (opcode >= 0x80u && opcode <= 0x9fu) {
      require(std::holds_alternative<VramCopyEvent>(*event),
              "VRAM copy alias emitted wrong event");
    } else if (opcode >= 0xa0u && opcode <= 0xbfu) {
      const auto* upload = std::get_if<VramUploadEvent>(&*event);
      require(upload != nullptr && upload->raw_words[0] == command,
              "CPU-to-VRAM alias lost its exact command word");
    } else if (opcode >= 0xc0u && opcode <= 0xdfu) {
      const auto* download = std::get_if<VramDownloadEvent>(&*event);
      require(download != nullptr && download->raw_words[0] == command,
              "VRAM-to-CPU alias lost its exact command word");
    }
  }
}

void test_all_fixed_packet_prefixes_are_transactional() {
  for (std::uint32_t raw_opcode = 0; raw_opcode <= 0xefu; ++raw_opcode) {
    const auto opcode = static_cast<std::uint8_t>(raw_opcode);
    if (opcode == 0x03u ||
        (opcode >= 0x40u && opcode <= 0x5fu && (opcode & 0x08u) != 0u)) {
      continue;
    }

    std::vector<std::uint32_t> packet{raw_opcode << 24u | 0x00112233u};
    if (opcode == 0x02u) {
      packet.push_back(point_word(1, 2));
      packet.push_back(extent_word(3, 4));
    } else if (opcode >= 0x20u && opcode <= 0x3fu) {
      const bool gouraud = (opcode & 0x10u) != 0u;
      const bool quad = (opcode & 0x08u) != 0u;
      const bool textured = (opcode & 0x04u) != 0u;
      const std::size_t vertices = quad ? 4u : 3u;
      const std::size_t words = 1u + vertices + (gouraud ? vertices - 1u : 0u) +
                                (textured ? vertices : 0u);
      packet.resize(words, 0u);
    } else if (opcode >= 0x40u && opcode <= 0x5fu) {
      packet.resize((opcode & 0x10u) != 0u ? 4u : 3u, 0u);
    } else if (opcode >= 0x60u && opcode <= 0x7fu) {
      const bool textured = (opcode & 0x04u) != 0u;
      const bool variable = ((opcode >> 3u) & 0x03u) == 0u;
      packet.resize(2u + (textured ? 1u : 0u) + (variable ? 1u : 0u), 0u);
    } else if (opcode >= 0x80u && opcode <= 0x9fu) {
      packet = {packet[0], point_word(0, 0), point_word(1, 1),
                extent_word(1, 1)};
    } else if (opcode >= 0xa0u && opcode <= 0xbfu) {
      packet = {packet[0], point_word(0, 0), extent_word(1, 1), 0x12345678u};
    } else if (opcode >= 0xc0u && opcode <= 0xdfu) {
      packet = {packet[0], point_word(0, 0), extent_word(1, 1)};
    }

    if (packet.size() <= 1u) {
      continue;
    }
    for (std::size_t prefix = 1; prefix < packet.size(); ++prefix) {
      FrontEnd gpu;
      for (std::size_t index = 0; index < prefix; ++index) {
        const auto result = gpu.write_gp0(packet[index]);
        require(result.accepted,
                "valid fixed-packet prefix was rejected for opcode " +
                    std::to_string(raw_opcode));
      }
      const auto truncated = gpu.finish_gp0_stream();
      require(
          !truncated.accepted &&
              truncated.error == WriteError::TruncatedPacket &&
              gpu.event_count() == 0u &&
              gpu.input_phase() == Gp0InputPhase::Command,
          "fixed-packet prefix was not rejected transactionally for opcode " +
              std::to_string(raw_opcode));
    }
  }
}

void test_polyline_slots_and_bounds() {
  FrontEnd gpu;
  send(gpu, 0x58112233u, "gouraud terminator-slot command");
  send(gpu, vertex_word(1, 2), "gouraud terminator-slot vertex 0");
  send(gpu, 0xa5010203u, "gouraud terminator-slot color 1");
  send(gpu, 0x50005000u, "gouraud terminator-looking vertex 1");
  send(gpu, 0x5fff5aaau, "gouraud terminator in color slot");
  const LineEvent gouraud =
      take_event<LineEvent>(gpu, "gouraud terminator-slot event");
  require(
      gouraud.vertices.size() == 2u && gouraud.vertices[1].raw == 0x50005000u &&
          gouraud.terminator_word == 0x5fff5aaau,
      "gouraud parser consumed a terminator-looking vertex as a terminator");
  require(gouraud.colors.size() == 2u && gouraud.colors[1].raw == 0xa5010203u,
          "gouraud color did not preserve ignored upper bits");

  send(gpu, 0x48000000u, "maximum flat polyline command");
  for (std::size_t index = 0; index < clean_aot::gpu::kMaxPolylineVertices;
       ++index) {
    send(gpu, vertex_word(static_cast<std::int32_t>(index & 0x3ffu), 0),
         "maximum flat polyline vertex");
  }
  const auto overlong = gpu.write_gp0(vertex_word(1, 1));
  require(!overlong.accepted && overlong.error == WriteError::PolylineTooLong,
          "polyline accepted a vertex beyond its explicit safety bound");
  require(
      gpu.input_phase() == Gp0InputPhase::Command && gpu.event_count() == 0u,
      "overlong polyline did not fail transactionally");
}

void test_draw_mode_texture_disable_and_inline_update() {
  FrontEnd gpu;
  send(gpu, 0xe1000fffu, "draw mode without texture-disable permission");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "draw mode event 0"));
  require((gpu.status() & 0x000087ffu) == 0x000007ffu,
          "GP0(E1).11 set texture disable without GP1(09) permission");

  send_gp1(gpu, 0x09000001u, "allow texture disable");
  send(gpu, 0xe1000fffu, "draw mode with texture-disable permission");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "draw mode event 1"));
  require((gpu.status() & 0x000087ffu) == 0x000087ffu,
          "GP1(09) did not permit GP0(E1).11 texture disable");

  send(gpu, 0xe1000600u, "draw mode before textured polygon");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "draw mode event 2"));
  send_flat_textured_triangle(gpu, 0xffffu, 0x25010203u);
  const PolygonEvent polygon =
      take_event<PolygonEvent>(gpu, "inline texture page update event");
  require(polygon.inline_texture_page.has_value() &&
              polygon.inline_texture_page->raw == 0xffffu &&
              polygon.inline_texture_page->texture_disable_requested,
          "inline texture-page raw attribute was not retained");
  const std::uint32_t inline_status = gpu.status() & 0x000087ffu;
  require(inline_status == 0x000087ffu,
          "textured polygon did not update effective GPUSTAT draw-mode bits: " +
              std::to_string(inline_status));
  require(polygon.state.draw_mode.effective_bits == 0x0fffu &&
              polygon.state.draw_mode.raw_word == 0xe1000600u,
          "polygon snapshot invented an E1 word or missed inline state");
  require(polygon.texture_coordinate_present[0] &&
              polygon.texture_coordinate_present[1] &&
              polygon.texture_coordinate_present[2] &&
              !polygon.texture_coordinate_present[3],
          "polygon UV presence was represented by default-filled values");

  send_gp1(gpu, 0x09000000u, "disallow future texture-disable sets");
  require((gpu.status() & 0x00008000u) != 0u,
          "GP1(09) incorrectly cleared active texture-disable state");
  send(gpu, 0xe1000000u, "clear texture disable while permission is off");
  static_cast<void>(take_event<EnvironmentEvent>(gpu, "draw mode event 3"));
  require((gpu.status() & 0x00008000u) == 0u,
          "GP0(E1) could not clear texture disable while permission was off");
}

void test_status_dma_and_input_phases() {
  FrontEnd gpu;
  require(gpu.input_phase() == Gp0InputPhase::Command,
          "reset input phase was not command");
  require((gpu.status() & ((1u << 26u) | (1u << 28u))) ==
              ((1u << 26u) | (1u << 28u)),
          "reset ready bits were not asserted");

  send(gpu, 0x20000000u, "partial polygon command");
  require(gpu.input_phase() == Gp0InputPhase::Parameters &&
              (gpu.status() & (1u << 26u)) == 0u &&
              (gpu.status() & (1u << 28u)) == 0u,
          "polygon command did not lower logical command/DMA readiness");
  send_gp1(gpu, 0x03000000u, "GP1 during polygon parameters");
  require(gpu.input_phase() == Gp0InputPhase::Parameters,
          "unrelated GP1 command interrupted GP0 packet");
  const auto invalid_gp1 = gpu.write_gp1(0x0a000000u);
  require(!invalid_gp1.accepted &&
              invalid_gp1.error == WriteError::InvalidGp1Command &&
              gpu.input_phase() == Gp0InputPhase::Parameters,
          "unsupported GP1 command interrupted the GP0 parser");
  send(gpu, vertex_word(1, 1), "partial polygon vertex 0");
  send(gpu, vertex_word(2, 2), "partial polygon vertex 1");
  send(gpu, vertex_word(3, 3), "partial polygon vertex 2");
  static_cast<void>(take_event<PolygonEvent>(gpu, "completed partial polygon"));

  send(gpu, 0x02000000u, "partial fill command");
  require(
      (gpu.status() & (1u << 26u)) == 0u && (gpu.status() & (1u << 28u)) != 0u,
      "fixed non-render parameters exposed wrong readiness");
  send_gp1(gpu, 0x01000000u, "cancel partial fill");
  require(gpu.input_phase() == Gp0InputPhase::Command,
          "GP1 command-buffer reset did not cancel partial packet");

  send_gp1(gpu, 0x04000001u, "FIFO DMA request mode");
  require((gpu.status() & (1u << 25u)) != 0u,
          "FIFO DMA request did not reflect available synchronous input");
  send_gp1(gpu, 0x04000002u, "CPU-to-GP0 DMA mode");
  require((gpu.status() & (1u << 25u)) == ((gpu.status() & (1u << 28u)) >> 3u),
          "DMA mode 2 request was not GPUSTAT.28");

  send(gpu, gp0_word(0xa0u), "upload phase command");
  send(gpu, point_word(0, 0), "upload phase destination");
  send(gpu, extent_word(1, 1), "upload phase extent");
  require(gpu.input_phase() == Gp0InputPhase::CpuToVramData &&
              (gpu.status() & (1u << 26u)) == 0u &&
              (gpu.status() & (1u << 28u)) != 0u,
          "upload data phase exposed wrong readiness");
  send(gpu, 0x00000001u, "upload phase data");
  static_cast<void>(take_event<VramUploadEvent>(gpu, "upload phase event"));

  send(gpu, 0xc0000000u, "download phase command");
  send(gpu, point_word(0, 0), "download phase source");
  send(gpu, extent_word(1, 1), "download phase extent");
  require(gpu.input_phase() == Gp0InputPhase::VramToCpuData &&
              (gpu.status() & (1u << 27u)) != 0u &&
              (gpu.status() & ((1u << 26u) | (1u << 28u))) == 0u,
          "download phase exposed wrong ready bits");
  send_gp1(gpu, 0x04000003u, "GPUREAD DMA mode");
  require((gpu.status() & (1u << 25u)) != 0u,
          "DMA mode 3 request was not GPUSTAT.27");
  require(gpu.read_dma_word().ready, "download phase DMA read failed");
  static_cast<void>(take_event<VramDownloadEvent>(gpu, "download phase event"));
}

void test_reset_latch_and_command_interruption() {
  FrontEnd gpu;
  send(gpu, 0xe2001234u, "seed texture window latch value");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "seed texture window event"));
  send_gp1(gpu, 0x10000002u, "latch texture window");
  const auto latched = gpu.read_gpuread();
  require(
      latched.ready && latched.word == 0x1234u && latched.valid_halfwords == 2u,
      "internal-register latch setup failed");

  send(gpu, gp0_word(0xa0u), "partial upload before command reset");
  send(gpu, point_word(4, 4), "partial upload reset destination");
  send(gpu, extent_word(3, 1), "partial upload reset extent");
  send(gpu, 0x00020001u, "partial upload reset data");
  send_gp1(gpu, 0x01000000u, "command-buffer-only reset");
  require(gpu.input_phase() == Gp0InputPhase::Command &&
              gpu.vram().read_wrapped(4, 4) == 0u,
          "GP1(01) did not transactionally cancel partial upload");
  const auto latch_after_reset = gpu.read_gpuread();
  require(latch_after_reset.ready && latch_after_reset.word == 0x1234u,
          "GP1(01) incorrectly cleared the GPUREAD register latch");

  send(gpu, gp0_word(0xa0u), "seed odd download");
  send(gpu, point_word(7, 7), "seed odd download destination");
  send(gpu, extent_word(1, 1), "seed odd download extent");
  send(gpu, 0xdead4321u, "seed odd download data and ignored padding");
  const VramUploadEvent upload =
      take_event<VramUploadEvent>(gpu, "seed odd download upload event");
  require(upload.data_words[0] == 0xdead4321u &&
              gpu.vram().read_wrapped(7, 7) == 0x4321u,
          "odd upload did not preserve raw padding while ignoring its pixel");

  send(gpu, 0xc0000000u, "odd download command");
  send(gpu, point_word(7, 7), "odd download source");
  send(gpu, extent_word(1, 1), "odd download extent");
  static_cast<void>(take_event<VramDownloadEvent>(gpu, "odd download event"));
  send_gp1(gpu, 0x01000000u, "command reset during completed download");
  require((gpu.status() & (1u << 27u)) != 0u,
          "GP1(01) incorrectly cancelled a completed VRAM read transfer");
  const auto odd = gpu.read_gpuread();
  require(odd.ready && odd.word == 0x00004321u && odd.valid_halfwords == 1u,
          "odd download did not expose explicit padding validity");
  const auto repeated = gpu.read_gpuread();
  require(repeated.ready && repeated.word == odd.word &&
              repeated.valid_halfwords == 1u,
          "final GPUREAD word did not remain latched after transfer");

  send(gpu, gp0_word(0x01u), "completed event before full reset");
  send(gpu, 0xc0000000u, "download before full reset");
  send(gpu, point_word(7, 7), "download before full reset source");
  send(gpu, extent_word(1, 1), "download before full reset extent");
  send_gp1(gpu, 0x00000000u, "full GPU reset");
  require(!gpu.read_gpuread().ready && (gpu.status() & (1u << 27u)) == 0u,
          "GP1(00) did not clear completed read state");
  require(gpu.vram().read_wrapped(7, 7) == 0x4321u,
          "GP1(00) incorrectly cleared VRAM");
  require(gpu.event_count() == 2u,
          "GP1(00) discarded commands that had already completed");
  static_cast<void>(take_event<clean_aot::gpu::CacheFlushEvent>(
      gpu, "completed pre-reset event"));
  static_cast<void>(
      take_event<VramDownloadEvent>(gpu, "completed pre-reset download event"));
  const auto& area = gpu.control().render.draw_area;
  require(area.left == 0u && area.top == 0u && area.right == 0u &&
              area.bottom == 0u,
          "GPU reset invented a full-VRAM drawing area instead of E3/E4 zero");
}

void test_fill_dimensions_and_scanline_copy_overlap() {
  FrontEnd gpu;
  send(gpu, 0x02ffffffu, "maximum rounded fill");
  send(gpu, point_word(0, 10), "maximum rounded fill origin");
  send(gpu, extent_word(0x03f1u, 1u), "maximum rounded fill extent");
  const VramFillEvent maximum =
      take_event<VramFillEvent>(gpu, "maximum rounded fill event");
  require(maximum.extent.width == 1024u &&
              gpu.vram().read_wrapped(0, 10) == 0x7fffu &&
              gpu.vram().read_wrapped(1023, 10) == 0x7fffu,
          "fill widths 0x3F1..0x3FF did not round to 1024 pixels");

  const std::uint64_t before_zero_fill = gpu.vram().digest();
  send(gpu, 0x020000ffu, "zero-width fill");
  send(gpu, point_word(0, 11), "zero-width fill origin");
  send(gpu, extent_word(0x0400u, 1u), "zero-width fill extent");
  const VramFillEvent zero =
      take_event<VramFillEvent>(gpu, "zero-width fill event");
  require(zero.extent.width == 0u && gpu.vram().digest() == before_zero_fill,
          "fill raw width 0x400 was not masked to zero");

  send(gpu, gp0_word(0xa0u), "overlap seed upload");
  send(gpu, point_word(20, 20), "overlap seed destination");
  send(gpu, extent_word(4, 4), "overlap seed extent");
  for (std::uint32_t row = 0; row < 4u; ++row) {
    send(gpu, ((row * 16u + 1u) << 16u) | (row * 16u),
         "overlap seed data pair 0");
    send(gpu, ((row * 16u + 3u) << 16u) | (row * 16u + 2u),
         "overlap seed data pair 1");
  }
  static_cast<void>(take_event<VramUploadEvent>(gpu, "overlap seed event"));

  send(gpu, 0x9fabcdefu, "aliased downward overlap copy");
  send(gpu, point_word(20, 20), "downward overlap source");
  send(gpu, point_word(20, 21), "downward overlap destination");
  send(gpu, extent_word(4, 4), "downward overlap extent");
  const VramCopyEvent downward =
      take_event<VramCopyEvent>(gpu, "downward overlap event");
  require(downward.raw_words[0] == 0x9fabcdefu,
          "VRAM-copy opcode alias was not preserved");
  for (std::uint32_t row = 1; row <= 4u; ++row) {
    for (std::uint32_t column = 0; column < 4u; ++column) {
      require(gpu.vram().read_wrapped(20u + column, 20u + row) == column,
              "downward overlap did not match hardware scanline feedback");
    }
  }

  send(gpu, gp0_word(0xa0u), "horizontal overlap seed");
  send(gpu, point_word(30, 30), "horizontal overlap seed destination");
  send(gpu, extent_word(4, 1), "horizontal overlap seed extent");
  send(gpu, 0x00020001u, "horizontal overlap seed data 0");
  send(gpu, 0x00040003u, "horizontal overlap seed data 1");
  static_cast<void>(
      take_event<VramUploadEvent>(gpu, "horizontal overlap seed event"));
  send(gpu, gp0_word(0x80u), "horizontal overlap copy");
  send(gpu, point_word(30, 30), "horizontal overlap source");
  send(gpu, point_word(31, 30), "horizontal overlap destination");
  send(gpu, extent_word(4, 1), "horizontal overlap extent");
  static_cast<void>(
      take_event<VramCopyEvent>(gpu, "horizontal overlap copy event"));
  require(gpu.vram().read_wrapped(31, 30) == 1u &&
              gpu.vram().read_wrapped(32, 30) == 2u &&
              gpu.vram().read_wrapped(33, 30) == 3u &&
              gpu.vram().read_wrapped(34, 30) == 4u,
          "horizontal overlap was not buffered one source scanline at a time");
}

void test_revision_specific_status_and_readback() {
  FrontEnd old_gpu(clean_aot::gpu::GpuRevision::Version0);
  FrontEnd prototype_gpu(clean_aot::gpu::GpuRevision::Version1);
  FrontEnd late_gpu(clean_aot::gpu::GpuRevision::Version2);
  require((old_gpu.status() & (1u << 13u)) == 0u &&
              (late_gpu.status() & (1u << 13u)) != 0u,
          "non-interlaced GPUSTAT.13 ignored GPU revision");

  send_gp1(prototype_gpu, 0x08000080u, "prototype reverse request");
  send_gp1(late_gpu, 0x08000080u, "late reverse request");
  require((prototype_gpu.status() & (1u << 14u)) != 0u &&
              (late_gpu.status() & (1u << 14u)) == 0u,
          "GPUSTAT.14 exposed prototype reverse behavior on late hardware");

  const auto old_permission = old_gpu.write_gp1(0x09000001u);
  require(!old_permission.accepted &&
              old_permission.error == WriteError::InvalidGp1Command,
          "late-GPU texture-disable permission was accepted on version 0");

  send(old_gpu, 0xe2004321u, "old GPU texture window");
  static_cast<void>(
      take_event<EnvironmentEvent>(old_gpu, "old GPU window event"));
  send_gp1(old_gpu, 0x10000002u, "old GPU window read");
  const std::uint32_t old_latch = old_gpu.read_gpuread().word;
  send_gp1(old_gpu, 0x10000007u, "old GPU unsupported version read");
  require(old_gpu.read_gpuread().word == old_latch,
          "version-0 unsupported register read changed GPUREAD latch");
  send_gp1(late_gpu, 0x10000007u, "late GPU version read");
  require(late_gpu.read_gpuread().word == 2u,
          "late GPU version register did not return version 2");

  send(old_gpu, 0xe30ffc01u, "old GPU draw area with upper Y bit");
  static_cast<void>(
      take_event<EnvironmentEvent>(old_gpu, "old GPU draw area event"));
  send(late_gpu, 0xe30ffc01u, "late GPU draw area with upper Y bit");
  static_cast<void>(
      take_event<EnvironmentEvent>(late_gpu, "late GPU draw area event"));
  require(old_gpu.control().render.draw_area.top == 511u &&
              late_gpu.control().render.draw_area.top == 1023u,
          "draw-area Y register width did not follow GPU revision");

  send_gp1(late_gpu, 0x08000004u, "480-line request without interlace");
  require((late_gpu.status() & (1u << 19u)) == 0u,
          "GPUSTAT.19 asserted 480-line mode without interlace");
  send_gp1(late_gpu, 0x08000024u, "480-line interlaced mode");
  require((late_gpu.status() & (1u << 19u)) != 0u,
          "GPUSTAT.19 did not assert in 480-line interlaced mode");
  late_gpu.set_scanout_state(true, true);
  require((late_gpu.status() & (1u << 31u)) == 0u,
          "GPUSTAT.31 did not clear during vblank");
  late_gpu.set_scanout_state(true, false);
  require((late_gpu.status() & (1u << 31u)) != 0u,
          "GPUSTAT.31 did not expose active odd field");
}

void test_event_payload_budget_and_snapshot_ownership() {
  FrontEnd gpu;
  send(gpu, 0xe1000123u, "snapshot draw mode");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "snapshot draw mode event"));
  send(gpu, 0x20010203u, "queued snapshot polygon");
  send(gpu, vertex_word(1, 2), "queued snapshot vertex 0");
  send(gpu, vertex_word(3, 4), "queued snapshot vertex 1");
  send(gpu, vertex_word(5, 6), "queued snapshot vertex 2");
  send(gpu, 0xe1000000u, "mutate draw mode after queued polygon");
  const PolygonEvent polygon =
      take_event<PolygonEvent>(gpu, "owned polygon snapshot");
  require(polygon.state.draw_mode.effective_bits == 0x0123u,
          "queued primitive aliased mutable draw-mode state");
  static_cast<void>(
      take_event<EnvironmentEvent>(gpu, "post-polygon draw mode event"));

  FrontEnd partial_a;
  FrontEnd partial_b;
  send(partial_a, gp0_word(0xa0u), "partial digest upload A command");
  send(partial_a, point_word(1, 2), "partial digest upload A destination");
  send(partial_a, extent_word(3, 1), "partial digest upload A extent");
  send(partial_a, 0x22221111u, "partial digest upload A data");
  send(partial_b, gp0_word(0xa0u), "partial digest upload B command");
  send(partial_b, point_word(2, 2), "partial digest upload B destination");
  send(partial_b, extent_word(3, 1), "partial digest upload B extent");
  send(partial_b, 0x22221111u, "partial digest upload B data");
  require(partial_a.state_digest() != partial_b.state_digest(),
          "state digest omitted the owned header of a partial upload");

  FrontEnd snapshot_a;
  FrontEnd snapshot_b;
  send(snapshot_a, 0xe1000001u, "snapshot digest state A");
  static_cast<void>(
      take_event<EnvironmentEvent>(snapshot_a, "snapshot digest event A"));
  send(snapshot_b, 0xe1000002u, "snapshot digest state B");
  static_cast<void>(
      take_event<EnvironmentEvent>(snapshot_b, "snapshot digest event B"));
  for (FrontEnd* candidate :
       std::array<FrontEnd*, 2>{&snapshot_a, &snapshot_b}) {
    send(*candidate, 0x20010203u, "snapshot digest polygon");
    send(*candidate, vertex_word(1, 2), "snapshot digest vertex 0");
    send(*candidate, vertex_word(3, 4), "snapshot digest vertex 1");
    send(*candidate, vertex_word(5, 6), "snapshot digest vertex 2");
    send(*candidate, 0xe1000000u, "snapshot digest normalize state");
  }
  require(snapshot_a.status() == snapshot_b.status() &&
              snapshot_a.state_digest() != snapshot_b.state_digest(),
          "state digest omitted an immutable queued render snapshot");

  FrontEnd rejected_download;
  for (std::size_t index = 0; index < 14u; ++index) {
    send(rejected_download, gp0_word(0x01u),
         "download preflight queue payload");
  }
  send(rejected_download, 0xc0000000u, "preflight download command");
  send(rejected_download, point_word(0, 0), "preflight download source");
  const auto rejected_header = rejected_download.write_gp0(extent_word(0, 0));
  require(!rejected_header.accepted &&
              rejected_header.error == WriteError::EventQueueFull &&
              rejected_download.event_count() == 14u &&
              rejected_download.input_phase() == Gp0InputPhase::Command &&
              (rejected_download.status() & (1u << 27u)) == 0u,
          "maximum download was not rejected transactionally at its header");

  send(gpu, gp0_word(0xa0u), "maximum upload command");
  send(gpu, point_word(0, 0), "maximum upload destination");
  send(gpu, extent_word(0, 0), "maximum upload normalized extent");
  constexpr std::size_t kMaximumTransferWords =
      clean_aot::gpu::kVramWordCount / 2u;
  for (std::size_t index = 0; index < kMaximumTransferWords; ++index) {
    const auto result = gpu.write_gp0(static_cast<std::uint32_t>(index));
    require(result.accepted, "bounded maximum upload was rejected");
  }
  require(gpu.event_count() == 1u,
          "maximum upload did not produce one bounded event");
  constexpr std::size_t kRemainingPayloadWords = 13u;
  for (std::size_t index = 0; index < kRemainingPayloadWords; ++index) {
    send(gpu, gp0_word(0x01u), "fill remaining event payload budget");
  }
  require(gpu.event_count() == 1u + kRemainingPayloadWords,
          "event queue did not admit its exact documented payload budget");
  const auto nop_under_payload_pressure = gpu.write_gp0(0x00000000u);
  require(nop_under_payload_pressure.accepted &&
              gpu.event_count() == 1u + kRemainingPayloadWords,
          "hardware NOP was coupled to transfer-event payload pressure");
  const auto backpressure = gpu.write_gp0(gp0_word(0x01u));
  require(!backpressure.accepted &&
              backpressure.error == WriteError::EventQueueFull,
          "queued transfer payload exceeded its explicit total-word budget");
  const VramUploadEvent maximum =
      take_event<VramUploadEvent>(gpu, "maximum upload event");
  require(maximum.data_words.size() == kMaximumTransferWords &&
              maximum.pixel_count == clean_aot::gpu::kVramWordCount,
          "maximum upload event was not ownership-complete");
  for (std::size_t index = 0; index < kRemainingPayloadWords; ++index) {
    static_cast<void>(take_event<clean_aot::gpu::CacheFlushEvent>(
        gpu, "remaining payload cache-flush event"));
  }
  send(gpu, gp0_word(0x01u), "cache flush after payload release");
  static_cast<void>(take_event<clean_aot::gpu::CacheFlushEvent>(
      gpu, "cache flush after payload release event"));
}

void test_backpressure_state_is_transactional() {
  const auto fill_to_one_word = [](FrontEnd& gpu) {
    send(gpu, gp0_word(0xa0u), "transaction pressure upload command");
    send(gpu, point_word(0u, 0u), "transaction pressure upload destination");
    send(gpu, extent_word(0u, 0u), "transaction pressure upload extent");
    constexpr std::size_t kMaximumTransferWords =
        clean_aot::gpu::kVramWordCount / 2u;
    for (std::size_t index = 0U; index < kMaximumTransferWords; ++index) {
      const auto result = gpu.write_gp0(static_cast<std::uint32_t>(index));
      require(result.accepted, "transaction pressure upload data rejected");
    }
    for (std::size_t index = 0U; index < 12U; ++index) {
      send(gpu, gp0_word(0x01u), "transaction pressure cache flush");
    }
  };

  FrontEnd rejected;
  FrontEnd control;
  fill_to_one_word(rejected);
  fill_to_one_word(control);
  const std::uint32_t status_before = rejected.status();

  send(rejected, 0x24010203u, "rejected textured polygon command");
  send(rejected, vertex_word(1, 2), "rejected textured vertex 0");
  send(rejected, 0x00400010u, "rejected textured CLUT");
  send(rejected, vertex_word(3, 4), "rejected textured vertex 1");
  send(rejected, 0x01ff0020u, "rejected inline texture page");
  send(rejected, vertex_word(5, 6), "rejected textured vertex 2");
  const auto final_word = rejected.write_gp0(0x00000030u);
  require(!final_word.accepted &&
              final_word.error == WriteError::EventQueueFull &&
              rejected.input_phase() == Gp0InputPhase::Command &&
              rejected.status() == status_before &&
              rejected.state_digest() == control.state_digest(),
          "rejected inline texture page escaped transactional backpressure");

  const auto rejected_pressure = drain_event_digests(rejected);
  const auto control_pressure = drain_event_digests(control);
  require(rejected_pressure == control_pressure &&
              rejected.state_digest() == control.state_digest(),
          "rejected packet contaminated deterministic queue replay");

  send_flat_textured_triangle(rejected, 0x0123u);
  send_flat_textured_triangle(control, 0x0123u);
  const PolygonEvent rejected_replay =
      take_event<PolygonEvent>(rejected, "accepted replay polygon");
  const PolygonEvent control_replay =
      take_event<PolygonEvent>(control, "control replay polygon");
  require(event_digest(Event{rejected_replay}) ==
              event_digest(Event{control_replay}) &&
              rejected.state_digest() == control.state_digest(),
          "post-backpressure native packet digest diverged from control");
}

void test_ownership_complete_snapshot_replay() {
  FrontEnd live;
  send(live, 0xe1000123u, "snapshot draw state");
  send(live, gp0_word(0xa0u), "snapshot partial upload command");
  send(live, point_word(40u, 50u), "snapshot partial upload destination");
  send(live, extent_word(3u, 1u), "snapshot partial upload extent");
  send(live, 0x22221111u, "snapshot partial upload first pair");

  FrontEnd upload_snapshot(live);
  FrontEnd restored;
  restored = live;
  require(upload_snapshot.state_digest() == live.state_digest() &&
              restored.state_digest() == live.state_digest() &&
              upload_snapshot.input_phase() == Gp0InputPhase::CpuToVramData,
          "partial-upload snapshot was not ownership complete");
  for (FrontEnd* candidate :
       std::array<FrontEnd*, 3>{&live, &upload_snapshot, &restored}) {
    send(*candidate, 0x44443333u, "snapshot partial upload final pair");
  }
  require(live.state_digest() == upload_snapshot.state_digest() &&
              live.state_digest() == restored.state_digest() &&
              live.vram().digest() == upload_snapshot.vram().digest() &&
              live.vram().digest() == restored.vram().digest(),
          "partial-upload snapshot replay diverged");

  const auto live_events = drain_event_digests(live);
  const auto copied_events = drain_event_digests(upload_snapshot);
  const auto restored_events = drain_event_digests(restored);
  require(live_events == copied_events && live_events == restored_events,
          "snapshot did not preserve queued immutable events");

  send(live, gp0_word(0xc0u), "snapshot download command");
  send(live, point_word(40u, 50u), "snapshot download source");
  send(live, extent_word(3u, 1u), "snapshot download extent");
  const auto first = live.read_gpuread();
  require(first.ready && first.word == 0x22221111u,
          "snapshot download seed mismatch");
  FrontEnd download_snapshot(live);
  const auto live_tail = live.read_gpuread();
  const auto copied_tail = download_snapshot.read_gpuread();
  require(live_tail.ready && copied_tail.ready &&
              live_tail.word == copied_tail.word &&
              live_tail.valid_halfwords == 1u &&
              copied_tail.valid_halfwords == 1u &&
              live.state_digest() == download_snapshot.state_digest(),
          "active GPUREAD snapshot replay diverged");
  require(drain_event_digests(live) == drain_event_digests(download_snapshot),
          "download event snapshot replay diverged");

  send(live, 0x48010203u, "snapshot flat polyline command");
  send(live, vertex_word(1, 2), "snapshot polyline vertex 0");
  send(live, vertex_word(3, 4), "snapshot polyline vertex 1");
  FrontEnd polyline_snapshot(live);
  send(live, 0x50005000u, "snapshot polyline terminator");
  send(polyline_snapshot, 0x50005000u, "copied polyline terminator");
  require(live.state_digest() == polyline_snapshot.state_digest() &&
              drain_event_digests(live) ==
                  drain_event_digests(polyline_snapshot),
          "in-flight polyline snapshot replay diverged");
  require(live.state_digest() == 3998441259986554230ull,
          "pinned snapshot replay digest changed");
}

void test_deterministic_replay() {
  const std::vector<std::uint32_t> stream{0xe1000321u,
                                          0xe2001a2bu,
                                          0xe3001008u,
                                          0xe4002009u,
                                          0xe5000000u,
                                          0xe6000001u,
                                          0x02a0b0c0u,
                                          point_word(100u, 500u),
                                          extent_word(32u, 3u),
                                          gp0_word(0xa0u),
                                          point_word(100u, 500u),
                                          extent_word(3u, 1u),
                                          0x00020001u,
                                          0x00000003u,
                                          0x3c010203u,
                                          vertex_word(1, 2),
                                          0x00030040u,
                                          vertex_word(5, 6),
                                          0x00040506u,
                                          0x00050060u,
                                          vertex_word(7, 8),
                                          0x00070809u,
                                          vertex_word(9, 10),
                                          0x000a0b0cu,
                                          0x000d0e0fu,
                                          0x48000000u,
                                          vertex_word(1, 1),
                                          vertex_word(2, 2),
                                          0x50005000u};

  auto run = [&stream]() {
    FrontEnd gpu;
    for (const std::uint32_t word : stream) {
      const auto result = gpu.write_gp0(word);
      require(result.accepted, "deterministic replay stream rejected");
    }
    const std::uint64_t before = gpu.state_digest();
    const std::vector<std::uint64_t> events = drain_event_digests(gpu);
    const std::uint64_t after = gpu.state_digest();
    return std::pair<std::uint64_t, std::uint64_t>{
        before ^ after ^ gpu.vram().digest(),
        events.empty() ? 0u : events.front()};
  };

  const auto first = run();
  const auto second = run();
  require(first == second, "identical GP0 replay was not deterministic");
}

void run_test(const char* name, void (*test)()) {
  test();
  std::cout << "PASS " << name << '\n';
}

}  // namespace

int main() {
  try {
    run_test("reset_and_gp1_state", test_reset_and_gp1_state);
    run_test("polygon_line_and_rectangle_framing",
             test_polygon_line_and_rectangle_framing);
    run_test("polyline_rejection_and_truncation",
             test_polyline_rejection_and_truncation);
    run_test("vram_fill_copy_upload_download_and_masks",
             test_vram_fill_copy_upload_download_and_masks);
    run_test("transfer_truncation_and_reset",
             test_transfer_truncation_and_reset);
    run_test("exhaustive_gp0_opcode_framing",
             test_exhaustive_gp0_opcode_framing);
    run_test("all_fixed_packet_prefixes_are_transactional",
             test_all_fixed_packet_prefixes_are_transactional);
    run_test("polyline_slots_and_bounds", test_polyline_slots_and_bounds);
    run_test("draw_mode_texture_disable_and_inline_update",
             test_draw_mode_texture_disable_and_inline_update);
    run_test("status_dma_and_input_phases", test_status_dma_and_input_phases);
    run_test("reset_latch_and_command_interruption",
             test_reset_latch_and_command_interruption);
    run_test("fill_dimensions_and_scanline_copy_overlap",
             test_fill_dimensions_and_scanline_copy_overlap);
    run_test("revision_specific_status_and_readback",
             test_revision_specific_status_and_readback);
    run_test("event_payload_budget_and_snapshot_ownership",
             test_event_payload_budget_and_snapshot_ownership);
    run_test("backpressure_state_is_transactional",
             test_backpressure_state_is_transactional);
    run_test("ownership_complete_snapshot_replay",
             test_ownership_complete_snapshot_replay);
    run_test("deterministic_replay", test_deterministic_replay);
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
  return 0;
}
