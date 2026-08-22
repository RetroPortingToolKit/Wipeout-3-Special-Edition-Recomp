#include "clean_aot/gpu/gpu.hpp"

#include <algorithm>
#include <utility>

namespace clean_aot::gpu {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

[[nodiscard]] constexpr std::uint64_t mix_byte(std::uint64_t hash,
                                               std::uint8_t value) noexcept {
  return (hash ^ static_cast<std::uint64_t>(value)) * kFnvPrime;
}

[[nodiscard]] constexpr std::uint64_t mix_word(std::uint64_t hash,
                                               std::uint32_t value) noexcept {
  hash = mix_byte(hash, static_cast<std::uint8_t>(value & 0xffu));
  hash = mix_byte(hash, static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  hash = mix_byte(hash, static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  return mix_byte(hash, static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

[[nodiscard]] std::uint64_t mix_mask_state(std::uint64_t hash,
                                           const MaskState& mask) noexcept {
  hash = mix_word(hash, mask.raw);
  hash = mix_word(hash, mask.set_mask_bit ? 1u : 0u);
  return mix_word(hash, mask.check_mask_bit ? 1u : 0u);
}

[[nodiscard]] std::uint64_t mix_render_state(
    std::uint64_t hash, const RenderStateSnapshot& state) noexcept {
  hash = mix_word(hash, state.draw_mode.raw_word);
  hash = mix_word(hash, state.draw_mode.effective_bits);
  hash = mix_word(hash, state.texture_window.raw);
  hash = mix_word(hash, state.draw_area.raw_top_left);
  hash = mix_word(hash, state.draw_area.raw_bottom_right);
  hash = mix_word(hash, state.draw_offset.raw);
  return mix_mask_state(hash, state.mask);
}

[[nodiscard]] constexpr std::uint16_t pack_draw_mode_bits(
    const DrawModeState& mode) noexcept {
  std::uint32_t value = static_cast<std::uint32_t>(mode.x_base & 0x0fu);
  value |= mode.y_base >= 256u ? 0x0010u : 0u;
  value |= static_cast<std::uint32_t>(mode.semi_transparency) << 5u;
  value |= static_cast<std::uint32_t>(mode.depth) << 7u;
  value |= mode.dither ? 0x0200u : 0u;
  value |= mode.draw_to_display ? 0x0400u : 0u;
  value |= mode.texture_disable ? 0x0800u : 0u;
  value |= mode.rectangle_flip_x ? 0x1000u : 0u;
  value |= mode.rectangle_flip_y ? 0x2000u : 0u;
  return static_cast<std::uint16_t>(value);
}

[[nodiscard]] constexpr std::uint8_t opcode_of(std::uint32_t word) noexcept {
  return static_cast<std::uint8_t>((word >> 24u) & 0xffu);
}

[[nodiscard]] constexpr std::uint16_t low_half(std::uint32_t word) noexcept {
  return static_cast<std::uint16_t>(word & 0xffffu);
}

[[nodiscard]] constexpr std::uint16_t high_half(std::uint32_t word) noexcept {
  return static_cast<std::uint16_t>((word >> 16u) & 0xffffu);
}

[[nodiscard]] constexpr std::uint16_t coordinate_x(
    std::uint32_t word) noexcept {
  return static_cast<std::uint16_t>(word & 0x03ffu);
}

[[nodiscard]] constexpr std::uint16_t coordinate_y(
    std::uint32_t word) noexcept {
  return static_cast<std::uint16_t>((word >> 16u) & 0x01ffu);
}

[[nodiscard]] constexpr std::uint16_t transfer_width(
    std::uint32_t word) noexcept {
  return low_half(word);
}

[[nodiscard]] constexpr std::uint16_t transfer_height(
    std::uint32_t word) noexcept {
  return high_half(word);
}

[[nodiscard]] constexpr bool is_environment_opcode(
    std::uint8_t opcode) noexcept {
  return opcode >= 0xe1u && opcode <= 0xe6u;
}

[[nodiscard]] constexpr bool is_nop_opcode(std::uint8_t opcode) noexcept {
  return opcode == 0x00u || (opcode >= 0x04u && opcode <= 0x1eu) ||
         opcode == 0xe0u || (opcode >= 0xe7u && opcode <= 0xefu);
}

[[nodiscard]] constexpr std::uint8_t enum_bits(std::uint32_t value,
                                               unsigned shift) noexcept {
  return static_cast<std::uint8_t>((value >> shift) & 0x03u);
}

}  // namespace

Vram::Vram() noexcept { reset(); }

void Vram::reset(std::uint16_t value) noexcept { words_.fill(value); }

std::uint16_t Vram::read_wrapped(std::uint32_t x,
                                 std::uint32_t y) const noexcept {
  const std::size_t wrapped_x = static_cast<std::size_t>(x % kVramWidth);
  const std::size_t wrapped_y = static_cast<std::size_t>(y % kVramHeight);
  return words_[wrapped_y * static_cast<std::size_t>(kVramWidth) + wrapped_x];
}

void Vram::write_wrapped(std::uint32_t x, std::uint32_t y,
                         std::uint16_t value) noexcept {
  const std::size_t wrapped_x = static_cast<std::size_t>(x % kVramWidth);
  const std::size_t wrapped_y = static_cast<std::size_t>(y % kVramHeight);
  words_[wrapped_y * static_cast<std::size_t>(kVramWidth) + wrapped_x] = value;
}

std::span<const std::uint16_t> Vram::words() const noexcept {
  return std::span<const std::uint16_t>(words_.data(), words_.size());
}

std::span<std::uint16_t> Vram::mutable_words() noexcept {
  return std::span<std::uint16_t>(words_.data(), words_.size());
}

std::uint64_t Vram::digest() const noexcept {
  std::uint64_t hash = kFnvOffset;
  for (const std::uint16_t word : words_) {
    hash = mix_byte(hash, static_cast<std::uint8_t>(word & 0xffu));
    hash = mix_byte(hash, static_cast<std::uint8_t>((word >> 8u) & 0xffu));
  }
  return hash;
}

FrontEnd::FrontEnd(GpuRevision revision) {
  control_.revision = revision;
  vram_ = std::make_unique<Vram>();
  packet_words_.reserve(2u + (kMaxPolylineVertices * 2u));
  line_builder_.event.vertices.reserve(kMaxPolylineVertices);
  line_builder_.event.colors.reserve(kMaxPolylineVertices);
  power_on_reset();
}

FrontEnd::FrontEnd(const FrontEnd& other)
    : control_(other.control_),
      vram_(std::make_unique<Vram>(*other.vram_)),
      parser_kind_(other.parser_kind_),
      expected_words_(other.expected_words_),
      packet_words_(other.packet_words_),
      line_builder_(other.line_builder_),
      upload_builder_(other.upload_builder_),
      download_words_(other.download_words_),
      download_final_word_valid_halfwords_(
          other.download_final_word_valid_halfwords_),
      download_index_(other.download_index_),
      events_(other.events_),
      queued_event_words_(other.queued_event_words_) {}

FrontEnd& FrontEnd::operator=(const FrontEnd& other) {
  if (this == &other) {
    return *this;
  }
  // Allocate and clone first. A failed snapshot leaves live GPU state intact.
  FrontEnd snapshot(other);
  *vram_ = *snapshot.vram_;
  control_ = snapshot.control_;
  parser_kind_ = snapshot.parser_kind_;
  expected_words_ = snapshot.expected_words_;
  packet_words_ = std::move(snapshot.packet_words_);
  line_builder_ = std::move(snapshot.line_builder_);
  upload_builder_ = std::move(snapshot.upload_builder_);
  download_words_ = std::move(snapshot.download_words_);
  download_final_word_valid_halfwords_ =
      snapshot.download_final_word_valid_halfwords_;
  download_index_ = snapshot.download_index_;
  events_ = std::move(snapshot.events_);
  queued_event_words_ = snapshot.queued_event_words_;
  return *this;
}

void FrontEnd::power_on_reset() noexcept {
  const GpuRevision revision = control_.revision;
  control_ = GpuControlState{};
  control_.revision = revision;
  control_.display.display_enabled = false;
  control_.display.display_start_x = 0;
  control_.display.display_start_y = 0;
  control_.display.horizontal_start = 0x0200u;
  control_.display.horizontal_end = 0x0c00u;
  control_.display.vertical_start = 0x0010u;
  control_.display.vertical_end = 0x0100u;
  control_.display.horizontal_resolution = 0;
  control_.display.resolution_368 = false;
  control_.display.vertical_resolution_480 = false;
  control_.display.video_standard = VideoStandard::Ntsc;
  control_.display.display_depth_24 = false;
  control_.display.interlaced = false;
  control_.display.reverse = false;
  control_.display.odd_field = false;
  control_.display.in_vblank = true;

  control_.render.draw_mode = decode_draw_mode(0u, false);
  control_.render.texture_window = decode_texture_window(0xe2000000u);
  control_.render.draw_area = DrawArea{};
  control_.render.draw_offset = decode_draw_offset(0xe5000000u);
  control_.render.mask = decode_mask(0xe6000000u);

  parser_kind_ = ParserKind::Idle;
  expected_words_ = 0;
  packet_words_.clear();
  line_builder_.event = LineEvent{};
  line_builder_.expectation = PolylineExpectation::Vertex;
  upload_builder_.event = VramUploadEvent{};
  upload_builder_.expected_words = 0;
  download_words_.clear();
  download_final_word_valid_halfwords_ = 2;
  download_index_ = 0;
  events_.clear();
  queued_event_words_ = 0;
  vram_->reset();
}

WriteResult FrontEnd::write_gp0(std::uint32_t word) {
  if (!download_words_.empty() && download_index_ < download_words_.size()) {
    return WriteResult::fail(WriteError::TransferBusy);
  }

  switch (parser_kind_) {
    case ParserKind::Idle:
      return begin_gp0(word);
    case ParserKind::Fixed:
      return continue_fixed(word);
    case ParserKind::Polyline:
      return continue_polyline(word);
    case ParserKind::Upload:
      return continue_upload(word);
  }
  return WriteResult::fail(WriteError::MalformedPacket);
}

WriteResult FrontEnd::begin_gp0(std::uint32_t word) {
  packet_words_.clear();
  packet_words_.push_back(word);
  const std::uint8_t opcode = opcode_of(word);

  if (is_nop_opcode(opcode)) {
    // These opcodes are consumed immediately and do not occupy the hardware
    // FIFO. They also must not consume renderer-event capacity.
    abandon_packet();
    return WriteResult::ok();
  }

  if (!has_event_slot()) {
    abandon_packet();
    return WriteResult::fail(WriteError::EventQueueFull);
  }

  if (opcode == 0x01u || opcode == 0x1fu) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 1;
    return finish_fixed();
  }

  if (opcode == 0x02u) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 3;
    return WriteResult::ok();
  }

  if (opcode >= 0x20u && opcode <= 0x3fu) {
    const bool gouraud = (opcode & 0x10u) != 0;
    const bool quad = (opcode & 0x08u) != 0;
    const bool textured = (opcode & 0x04u) != 0;
    const std::size_t vertices = quad ? 4u : 3u;
    expected_words_ = 1u + vertices + (gouraud ? vertices - 1u : 0u) +
                      (textured ? vertices : 0u);
    parser_kind_ = ParserKind::Fixed;
    return WriteResult::ok();
  }

  if (opcode >= 0x40u && opcode <= 0x5fu) {
    const bool gouraud = (opcode & 0x10u) != 0;
    const bool polyline = (opcode & 0x08u) != 0;
    if (!polyline) {
      expected_words_ = gouraud ? 4u : 3u;
      parser_kind_ = ParserKind::Fixed;
      return WriteResult::ok();
    }

    line_builder_.event = LineEvent{};
    line_builder_.event.opcode = opcode;
    line_builder_.event.command_word = word;
    line_builder_.event.gouraud = gouraud;
    line_builder_.event.polyline = true;
    line_builder_.event.semi_transparent = (opcode & 0x02u) != 0;
    line_builder_.event.first_color = decode_color(word);
    line_builder_.event.state = control_.render;
    line_builder_.event.vertices.reserve(kMaxPolylineVertices);
    line_builder_.event.colors.reserve(kMaxPolylineVertices);
    line_builder_.event.colors.push_back(line_builder_.event.first_color);
    line_builder_.expectation = PolylineExpectation::Vertex;
    parser_kind_ = ParserKind::Polyline;
    return WriteResult::ok();
  }

  if (opcode >= 0x60u && opcode <= 0x7fu) {
    const bool textured = (opcode & 0x04u) != 0;
    const bool variable = ((opcode >> 3u) & 0x03u) == 0;
    expected_words_ = 2u + (textured ? 1u : 0u) + (variable ? 1u : 0u);
    parser_kind_ = ParserKind::Fixed;
    return WriteResult::ok();
  }

  if (opcode >= 0x80u && opcode <= 0x9fu) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 4;
    return WriteResult::ok();
  }

  if (opcode >= 0xa0u && opcode <= 0xbfu) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 3;
    return WriteResult::ok();
  }

  if (opcode >= 0xc0u && opcode <= 0xdfu) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 3;
    return WriteResult::ok();
  }

  if (is_environment_opcode(opcode)) {
    parser_kind_ = ParserKind::Fixed;
    expected_words_ = 1;
    return finish_fixed();
  }

  abandon_packet();
  return WriteResult::fail(WriteError::UnknownGp0Command);
}

WriteResult FrontEnd::continue_fixed(std::uint32_t word) {
  if (packet_words_.size() >= expected_words_) {
    abandon_packet();
    return WriteResult::fail(WriteError::MalformedPacket);
  }
  packet_words_.push_back(word);
  if (packet_words_.size() != expected_words_) {
    return WriteResult::ok();
  }
  return finish_fixed();
}

WriteResult FrontEnd::continue_polyline(std::uint32_t word) {
  if (packet_words_.size() >= 2u + (kMaxPolylineVertices * 2u)) {
    abandon_packet();
    return WriteResult::fail(WriteError::PolylineTooLong);
  }
  packet_words_.push_back(word);

  if (line_builder_.expectation == PolylineExpectation::Vertex) {
    if (line_builder_.event.vertices.size() >= kMaxPolylineVertices) {
      abandon_packet();
      return WriteResult::fail(WriteError::PolylineTooLong);
    }
    line_builder_.event.vertices.push_back(decode_vertex(word));
    line_builder_.expectation = line_builder_.event.gouraud
                                    ? PolylineExpectation::ColorOrTerminator
                                    : PolylineExpectation::VertexOrTerminator;
    return WriteResult::ok();
  }

  if (line_builder_.expectation == PolylineExpectation::ColorOrTerminator) {
    if (is_polyline_terminator(word)) {
      if (line_builder_.event.vertices.size() < 2u) {
        abandon_packet();
        return WriteResult::fail(WriteError::PolylineTerminatorTooEarly);
      }
      line_builder_.event.terminator_word = word;
      return finish_polyline();
    }
    line_builder_.event.colors.push_back(decode_color(word));
    // A Gouraud terminator occupies the next color slot. The following vertex
    // word is always vertex data, even if its ignored bits match the terminator
    // mask.
    line_builder_.expectation = PolylineExpectation::Vertex;
    return WriteResult::ok();
  }

  if (is_polyline_terminator(word)) {
    if (line_builder_.event.vertices.size() < 2u) {
      abandon_packet();
      return WriteResult::fail(WriteError::PolylineTerminatorTooEarly);
    }
    line_builder_.event.terminator_word = word;
    return finish_polyline();
  }

  if (line_builder_.event.vertices.size() >= kMaxPolylineVertices) {
    abandon_packet();
    return WriteResult::fail(WriteError::PolylineTooLong);
  }
  line_builder_.event.vertices.push_back(decode_vertex(word));
  line_builder_.expectation = line_builder_.event.gouraud
                                  ? PolylineExpectation::ColorOrTerminator
                                  : PolylineExpectation::VertexOrTerminator;
  return WriteResult::ok();
}

WriteResult FrontEnd::continue_upload(std::uint32_t word) {
  if (upload_builder_.event.data_words.size() >=
      upload_builder_.expected_words) {
    abandon_packet();
    return WriteResult::fail(WriteError::MalformedPacket);
  }

  upload_builder_.event.data_words.push_back(word);

  if (upload_builder_.event.data_words.size() !=
      upload_builder_.expected_words) {
    return WriteResult::ok();
  }
  return finish_upload();
}

WriteResult FrontEnd::finish_fixed() {
  const std::uint8_t opcode = opcode_of(packet_words_.front());

  if (opcode == 0x01u) {
    CacheFlushEvent event{};
    event.raw_word = packet_words_.front();
    const WriteResult result = enqueue(event);
    abandon_packet();
    return result;
  }

  if (opcode == 0x1fu) {
    InterruptRequestEvent event{};
    event.raw_word = packet_words_.front();
    const WriteResult result = enqueue(event);
    if (result.accepted) {
      control_.irq1_pending = true;
    }
    abandon_packet();
    return result;
  }

  if (opcode == 0x02u) {
    VramFillEvent event{};
    event.raw_words[0] = packet_words_[0];
    event.raw_words[1] = packet_words_[1];
    event.raw_words[2] = packet_words_[2];
    event.color = decode_color(packet_words_[0]);
    event.origin.x = static_cast<std::uint16_t>(packet_words_[1] & 0x03f0u);
    event.origin.y = coordinate_y(packet_words_[1]);
    event.raw_width = transfer_width(packet_words_[2]);
    event.raw_height = transfer_height(packet_words_[2]);
    const std::uint32_t rounded_width =
        (static_cast<std::uint32_t>(event.raw_width & 0x03ffu) + 0x0fu) &
        ~0x0fu;
    event.extent.width = static_cast<std::uint16_t>(rounded_width);
    event.extent.height =
        static_cast<std::uint16_t>(event.raw_height & 0x01ffu);
    Event queued{event};
    if (!can_enqueue(queued)) {
      abandon_packet();
      return WriteResult::fail(WriteError::EventQueueFull);
    }
    apply_fill(std::get<VramFillEvent>(queued));
    const WriteResult result = enqueue(std::move(queued));
    abandon_packet();
    return result;
  }

  if (opcode >= 0x20u && opcode <= 0x3fu) {
    PolygonEvent event{};
    event.opcode = opcode;
    event.command_word = packet_words_[0];
    event.gouraud = (opcode & 0x10u) != 0;
    event.quad = (opcode & 0x08u) != 0;
    event.textured = (opcode & 0x04u) != 0;
    event.semi_transparent = (opcode & 0x02u) != 0;
    event.raw_texture = (opcode & 0x01u) != 0;
    event.vertex_count = static_cast<std::uint8_t>(event.quad ? 4u : 3u);
    event.first_color = decode_color(packet_words_[0]);
    event.colors[0] = event.first_color;
    event.color_present[0] = true;
    std::copy(packet_words_.begin(), packet_words_.end(),
              event.raw_words.begin());
    event.raw_word_count = static_cast<std::uint8_t>(packet_words_.size());

    std::size_t index = 1u;
    for (std::size_t vertex_index = 0u; vertex_index < event.vertex_count;
         ++vertex_index) {
      if (vertex_index != 0u && event.gouraud) {
        event.colors[vertex_index] = decode_color(packet_words_[index]);
        event.color_present[vertex_index] = true;
        ++index;
      }
      event.vertices[vertex_index] = decode_vertex(packet_words_[index]);
      ++index;
      if (event.textured) {
        event.texture_coordinates[vertex_index] =
            decode_texture_coordinate(packet_words_[index]);
        event.texture_coordinate_present[vertex_index] = true;
        ++index;
      }
    }
    if (event.textured) {
      event.clut = decode_clut(event.texture_coordinates[0].upper);
      event.inline_texture_page =
          decode_texture_page_attribute(event.texture_coordinates[1].upper);
      const DrawModeState previous_draw_mode = control_.render.draw_mode;
      apply_texture_page_attribute(*event.inline_texture_page);
      event.state = control_.render;
      const WriteResult result = enqueue(event);
      if (!result.accepted) {
        control_.render.draw_mode = previous_draw_mode;
      }
      abandon_packet();
      return result;
    }
    event.state = control_.render;
    const WriteResult result = enqueue(event);
    abandon_packet();
    return result;
  }

  if (opcode >= 0x40u && opcode <= 0x5fu) {
    LineEvent event{};
    event.opcode = opcode;
    event.command_word = packet_words_[0];
    event.gouraud = (opcode & 0x10u) != 0;
    event.polyline = false;
    event.semi_transparent = (opcode & 0x02u) != 0;
    event.first_color = decode_color(packet_words_[0]);
    event.state = control_.render;
    event.raw_words = packet_words_;
    event.vertices.reserve(2u);
    event.colors.reserve(event.gouraud ? 2u : 1u);
    event.colors.push_back(event.first_color);
    std::size_t index = 1u;
    event.vertices.push_back(decode_vertex(packet_words_[index]));
    ++index;
    if (event.gouraud) {
      event.colors.push_back(decode_color(packet_words_[index]));
      ++index;
    }
    event.vertices.push_back(decode_vertex(packet_words_[index]));
    const WriteResult result = enqueue(std::move(event));
    abandon_packet();
    return result;
  }

  if (opcode >= 0x60u && opcode <= 0x7fu) {
    RectangleEvent event{};
    event.opcode = opcode;
    event.command_word = packet_words_[0];
    event.size_code = static_cast<std::uint8_t>((opcode >> 3u) & 0x03u);
    event.textured = (opcode & 0x04u) != 0;
    event.semi_transparent = (opcode & 0x02u) != 0;
    event.raw_texture = (opcode & 0x01u) != 0;
    event.color = decode_color(packet_words_[0]);
    event.origin = decode_vertex(packet_words_[1]);
    event.state = control_.render;
    std::copy(packet_words_.begin(), packet_words_.end(),
              event.raw_words.begin());
    event.raw_word_count = static_cast<std::uint8_t>(packet_words_.size());
    std::size_t index = 2u;
    if (event.textured) {
      event.texture_coordinate =
          decode_texture_coordinate(packet_words_[index]);
      event.clut = decode_clut(event.texture_coordinate->upper);
      ++index;
    }
    if (event.size_code == 0u) {
      event.raw_extent_present = true;
      event.raw_width = low_half(packet_words_[index]);
      event.raw_height = high_half(packet_words_[index]);
      event.width = static_cast<std::uint16_t>(event.raw_width & 0x03ffu);
      event.height = static_cast<std::uint16_t>(event.raw_height & 0x01ffu);
    } else {
      event.width =
          event.size_code == 1u ? 1u : (event.size_code == 2u ? 8u : 16u);
      event.height = event.width;
    }
    const WriteResult result = enqueue(event);
    abandon_packet();
    return result;
  }

  if (opcode >= 0x80u && opcode <= 0x9fu) {
    VramCopyEvent event{};
    event.raw_words[0] = packet_words_[0];
    event.raw_words[1] = packet_words_[1];
    event.raw_words[2] = packet_words_[2];
    event.raw_words[3] = packet_words_[3];
    event.source = VramPoint{coordinate_x(packet_words_[1]),
                             coordinate_y(packet_words_[1])};
    event.destination = VramPoint{coordinate_x(packet_words_[2]),
                                  coordinate_y(packet_words_[2])};
    event.raw_width = transfer_width(packet_words_[3]);
    event.raw_height = transfer_height(packet_words_[3]);
    event.extent = VramExtent{normalize_copy_width(event.raw_width),
                              normalize_copy_height(event.raw_height)};
    event.mask = control_.render.mask;
    Event queued{event};
    if (!can_enqueue(queued)) {
      abandon_packet();
      return WriteResult::fail(WriteError::EventQueueFull);
    }
    apply_copy(std::get<VramCopyEvent>(queued));
    const WriteResult result = enqueue(std::move(queued));
    abandon_packet();
    return result;
  }

  if (opcode >= 0xa0u && opcode <= 0xbfu) {
    upload_builder_.event = VramUploadEvent{};
    upload_builder_.event.raw_words[0] = packet_words_[0];
    upload_builder_.event.raw_words[1] = packet_words_[1];
    upload_builder_.event.raw_words[2] = packet_words_[2];
    upload_builder_.event.destination = VramPoint{
        coordinate_x(packet_words_[1]), coordinate_y(packet_words_[1])};
    upload_builder_.event.raw_width = transfer_width(packet_words_[2]);
    upload_builder_.event.raw_height = transfer_height(packet_words_[2]);
    upload_builder_.event.extent =
        VramExtent{normalize_copy_width(upload_builder_.event.raw_width),
                   normalize_copy_height(upload_builder_.event.raw_height)};
    upload_builder_.event.pixel_count =
        static_cast<std::size_t>(upload_builder_.event.extent.width) *
        static_cast<std::size_t>(upload_builder_.event.extent.height);
    upload_builder_.expected_words =
        (upload_builder_.event.pixel_count + 1u) / 2u;
    upload_builder_.event.padded_word_count = upload_builder_.expected_words;
    upload_builder_.event.final_word_valid_halfwords =
        (upload_builder_.event.pixel_count & 1u) != 0u ? 1u : 2u;
    upload_builder_.event.mask = control_.render.mask;
    if (!can_enqueue_word_cost(upload_builder_.expected_words + 3u)) {
      abandon_packet();
      return WriteResult::fail(WriteError::EventQueueFull);
    }
    upload_builder_.event.data_words.reserve(upload_builder_.expected_words);
    packet_words_.clear();
    parser_kind_ = ParserKind::Upload;
    expected_words_ = upload_builder_.expected_words;
    return WriteResult::ok();
  }

  if (opcode >= 0xc0u && opcode <= 0xdfu) {
    const std::array<std::uint32_t, 3> header{
        packet_words_[0], packet_words_[1], packet_words_[2]};
    const std::size_t pixel_count =
        static_cast<std::size_t>(normalize_copy_width(low_half(header[2]))) *
        static_cast<std::size_t>(normalize_copy_height(high_half(header[2])));
    const std::size_t payload_words = (pixel_count + 1u) / 2u;
    if (!can_enqueue_word_cost(payload_words + header.size())) {
      abandon_packet();
      return WriteResult::fail(WriteError::EventQueueFull);
    }
    VramDownloadEvent event = make_download(header);
    Event queued{std::move(event)};
    if (!can_enqueue(queued)) {
      abandon_packet();
      return WriteResult::fail(WriteError::EventQueueFull);
    }
    const auto& download = std::get<VramDownloadEvent>(queued);
    download_words_ = download.data_words;
    download_final_word_valid_halfwords_ = download.final_word_valid_halfwords;
    download_index_ = 0;
    const WriteResult result = enqueue(std::move(queued));
    abandon_packet();
    return result;
  }

  if (is_environment_opcode(opcode)) {
    EnvironmentEvent event{};
    event.raw_word = packet_words_[0];
    const RenderStateSnapshot previous_render = control_.render;
    switch (opcode) {
      case 0xe1u:
        control_.render.draw_mode = decode_draw_mode(
            packet_words_[0], control_.render.draw_mode.texture_disable);
        event.environment = EnvironmentKind::DrawMode;
        break;
      case 0xe2u:
        control_.render.texture_window =
            decode_texture_window(packet_words_[0]);
        event.environment = EnvironmentKind::TextureWindow;
        break;
      case 0xe3u: {
        control_.render.draw_area.raw_top_left = packet_words_[0];
        control_.render.draw_area.left =
            static_cast<std::uint16_t>(packet_words_[0] & 0x03ffu);
        control_.render.draw_area.top = decode_draw_area_y(packet_words_[0]);
        event.environment = EnvironmentKind::DrawAreaTopLeft;
        break;
      }
      case 0xe4u: {
        control_.render.draw_area.raw_bottom_right = packet_words_[0];
        control_.render.draw_area.right =
            static_cast<std::uint16_t>(packet_words_[0] & 0x03ffu);
        control_.render.draw_area.bottom = decode_draw_area_y(packet_words_[0]);
        event.environment = EnvironmentKind::DrawAreaBottomRight;
        break;
      }
      case 0xe5u:
        control_.render.draw_offset = decode_draw_offset(packet_words_[0]);
        event.environment = EnvironmentKind::DrawOffset;
        break;
      case 0xe6u:
        control_.render.mask = decode_mask(packet_words_[0]);
        event.environment = EnvironmentKind::Mask;
        break;
      default:
        abandon_packet();
        return WriteResult::fail(WriteError::UnknownGp0Command);
    }
    event.state = control_.render;
    const WriteResult result = enqueue(event);
    if (!result.accepted) {
      control_.render = previous_render;
    }
    abandon_packet();
    return result;
  }

  abandon_packet();
  return WriteResult::fail(WriteError::UnknownGp0Command);
}

WriteResult FrontEnd::finish_polyline() {
  line_builder_.event.raw_words = packet_words_;
  const WriteResult result = enqueue(std::move(line_builder_.event));
  abandon_packet();
  return result;
}

WriteResult FrontEnd::finish_upload() {
  Event queued{std::move(upload_builder_.event)};
  if (!can_enqueue(queued)) {
    abandon_packet();
    return WriteResult::fail(WriteError::EventQueueFull);
  }
  apply_upload(std::get<VramUploadEvent>(queued));
  const WriteResult result = enqueue(std::move(queued));
  abandon_packet();
  return result;
}

WriteResult FrontEnd::finish_gp0_stream() {
  if (parser_kind_ == ParserKind::Idle) {
    return WriteResult::ok();
  }
  abandon_packet();
  return WriteResult::fail(WriteError::TruncatedPacket);
}

WriteResult FrontEnd::enqueue(Event event) {
  if (!can_enqueue(event)) {
    return WriteResult::fail(WriteError::EventQueueFull);
  }
  if (event_executor_ != nullptr && !event_executor_->execute(event)) {
    return WriteResult::fail(WriteError::MalformedPacket);
  }
  queued_event_words_ += event_word_cost(event);
  events_.push_back(std::move(event));
  return WriteResult::ok();
}

bool FrontEnd::has_event_slot() const noexcept {
  // Before seeing the next opcode, readiness can only promise capacity for
  // the smallest ordered event. Exact packet costs are checked transactionally
  // when the command is complete.
  return can_enqueue_word_cost(1u);
}

bool FrontEnd::can_enqueue_word_cost(std::size_t cost) const noexcept {
  return events_.size() < kEventQueueCapacity &&
         queued_event_words_ <= kMaxQueuedEventWords &&
         cost <= kMaxQueuedEventWords - queued_event_words_;
}

bool FrontEnd::can_enqueue(const Event& event) const noexcept {
  return can_enqueue_word_cost(event_word_cost(event));
}

std::size_t FrontEnd::event_word_cost(const Event& event) noexcept {
  return std::visit(
      [](const auto& item) -> std::size_t {
        std::size_t cost = 0;
        if constexpr (requires { item.raw_word_count; }) {
          cost += item.raw_word_count;
        } else if constexpr (requires { item.raw_words.size(); }) {
          cost += item.raw_words.size();
        }
        if constexpr (requires { item.raw_word; }) {
          ++cost;
        }
        if constexpr (requires { item.data_words.size(); }) {
          cost += item.data_words.size();
        }
        return cost;
      },
      event);
}

void FrontEnd::abandon_packet() noexcept {
  parser_kind_ = ParserKind::Idle;
  expected_words_ = 0;
  packet_words_.clear();
  line_builder_.event.vertices.clear();
  line_builder_.event.colors.clear();
  line_builder_.event.raw_words.clear();
  line_builder_.expectation = PolylineExpectation::Vertex;
  upload_builder_.event.data_words.clear();
  upload_builder_.expected_words = 0;
}

WriteResult FrontEnd::write_gp1(std::uint32_t word) {
  std::uint8_t command = opcode_of(word);
  if (command >= 0x40u) {
    command = static_cast<std::uint8_t>(command & 0x3fu);
  }
  return apply_gp1(command, word & 0x00ffffffu);
}

WriteResult FrontEnd::write_dma_word(std::uint32_t word) {
  if (control_.dma_direction != DmaDirection::Fifo &&
      control_.dma_direction != DmaDirection::CpuToGp0) {
    return WriteResult::fail(WriteError::DmaDirectionMismatch);
  }
  return write_gp0(word);
}

WriteResult FrontEnd::apply_gp1(std::uint8_t command, std::uint32_t parameter) {
  if (command == 0x00u) {
    const GpuRevision revision = control_.revision;
    const bool texture_disable_allowed = control_.texture_disable_allowed;
    clear_command_buffer();
    download_words_.clear();
    download_final_word_valid_halfwords_ = 2;
    download_index_ = 0;
    control_ = GpuControlState{};
    control_.revision = revision;
    control_.texture_disable_allowed = texture_disable_allowed;
    control_.display.display_enabled = false;
    control_.display.horizontal_start = 0x0200u;
    control_.display.horizontal_end = 0x0c00u;
    control_.display.vertical_start = 0x0010u;
    control_.display.vertical_end = 0x0100u;
    control_.display.in_vblank = true;
    control_.render.draw_mode = decode_draw_mode(0u, false);
    control_.render.texture_window = decode_texture_window(0xe2000000u);
    control_.render.draw_area = DrawArea{};
    control_.render.draw_offset = decode_draw_offset(0xe5000000u);
    control_.render.mask = decode_mask(0xe6000000u);
    return WriteResult::ok();
  }

  switch (command) {
    case 0x01u:
      clear_command_buffer();
      return WriteResult::ok();
    case 0x02u:
      control_.irq1_pending = false;
      return WriteResult::ok();
    case 0x03u:
      control_.display.display_enabled = (parameter & 1u) == 0u;
      return WriteResult::ok();
    case 0x04u:
      control_.dma_direction = static_cast<DmaDirection>(parameter & 3u);
      return WriteResult::ok();
    case 0x05u:
      control_.display.display_start_x =
          static_cast<std::uint16_t>(parameter & 0x03ffu);
      control_.display.display_start_y =
          static_cast<std::uint16_t>((parameter >> 10u) & 0x01ffu);
      return WriteResult::ok();
    case 0x06u:
      control_.display.horizontal_start =
          static_cast<std::uint16_t>(parameter & 0x0fffu);
      control_.display.horizontal_end =
          static_cast<std::uint16_t>((parameter >> 12u) & 0x0fffu);
      return WriteResult::ok();
    case 0x07u:
      control_.display.vertical_start =
          static_cast<std::uint16_t>(parameter & 0x03ffu);
      control_.display.vertical_end =
          static_cast<std::uint16_t>((parameter >> 10u) & 0x03ffu);
      return WriteResult::ok();
    case 0x08u:
      control_.display.horizontal_resolution =
          static_cast<std::uint8_t>(parameter & 3u);
      control_.display.vertical_resolution_480 = (parameter & 0x04u) != 0;
      control_.display.video_standard =
          (parameter & 0x08u) != 0 ? VideoStandard::Pal : VideoStandard::Ntsc;
      control_.display.display_depth_24 = (parameter & 0x10u) != 0;
      control_.display.interlaced = (parameter & 0x20u) != 0;
      control_.display.resolution_368 = (parameter & 0x40u) != 0;
      control_.display.reverse = (parameter & 0x80u) != 0;
      return WriteResult::ok();
    case 0x09u:
      if (control_.revision != GpuRevision::Version2) {
        return WriteResult::fail(WriteError::InvalidGp1Command);
      }
      control_.texture_disable_allowed = (parameter & 1u) != 0u;
      return WriteResult::ok();
    case 0x10u:
    case 0x11u:
    case 0x12u:
    case 0x13u:
    case 0x14u:
    case 0x15u:
    case 0x16u:
    case 0x17u:
    case 0x18u:
    case 0x19u:
    case 0x1au:
    case 0x1bu:
    case 0x1cu:
    case 0x1du:
    case 0x1eu:
    case 0x1fu:
      if (const std::optional<std::uint32_t> value = read_internal_register(
              static_cast<std::uint8_t>(parameter & 0xffu))) {
        control_.read_latch = *value;
        control_.read_latch_valid = true;
        control_.read_latch_valid_halfwords = 2;
      }
      return WriteResult::ok();
    default:
      return WriteResult::fail(WriteError::InvalidGp1Command);
  }
}

ReadResult FrontEnd::read_gpuread() {
  if (download_index_ < download_words_.size()) {
    const std::uint32_t word = download_words_[download_index_];
    const std::uint8_t valid_halfwords =
        download_index_ + 1u == download_words_.size()
            ? download_final_word_valid_halfwords_
            : 2u;
    ++download_index_;
    control_.read_latch = word;
    control_.read_latch_valid = true;
    control_.read_latch_valid_halfwords = valid_halfwords;
    if (download_index_ == download_words_.size()) {
      download_words_.clear();
      download_final_word_valid_halfwords_ = 2;
      download_index_ = 0;
    }
    return ReadResult{true, word, ReadError::None, valid_halfwords};
  }
  if (control_.read_latch_valid) {
    return ReadResult{true, control_.read_latch, ReadError::None,
                      control_.read_latch_valid_halfwords};
  }
  return ReadResult{false, 0, ReadError::NotReady, 0};
}

ReadResult FrontEnd::read_dma_word() {
  if (control_.dma_direction != DmaDirection::GpureadToCpu) {
    return ReadResult{false, 0, ReadError::DmaDirectionMismatch, 0};
  }
  return read_gpuread();
}

void FrontEnd::clear_command_buffer() noexcept {
  parser_kind_ = ParserKind::Idle;
  expected_words_ = 0;
  packet_words_.clear();
  line_builder_.event.vertices.clear();
  line_builder_.event.colors.clear();
  line_builder_.event.raw_words.clear();
  line_builder_.expectation = PolylineExpectation::Vertex;
  upload_builder_.event.data_words.clear();
  upload_builder_.expected_words = 0;
}

void FrontEnd::set_scanout_state(bool odd_field, bool in_vblank) noexcept {
  control_.display.odd_field = odd_field;
  control_.display.in_vblank = in_vblank;
}

void FrontEnd::set_event_executor(IEventExecutor* executor) noexcept {
  event_executor_ = executor;
}

std::optional<Event> FrontEnd::pop_event() noexcept {
  if (events_.empty()) {
    return std::nullopt;
  }
  queued_event_words_ -= event_word_cost(events_.front());
  Event event = std::move(events_.front());
  events_.pop_front();
  return event;
}

std::size_t FrontEnd::event_count() const noexcept { return events_.size(); }

std::size_t FrontEnd::staged_word_count() const noexcept {
  if (parser_kind_ == ParserKind::Upload) {
    return upload_builder_.event.data_words.size();
  }
  return packet_words_.size();
}

Gp0InputPhase FrontEnd::input_phase() const noexcept {
  if (download_index_ < download_words_.size()) {
    return Gp0InputPhase::VramToCpuData;
  }
  switch (parser_kind_) {
    case ParserKind::Idle:
      return Gp0InputPhase::Command;
    case ParserKind::Fixed:
      return Gp0InputPhase::Parameters;
    case ParserKind::Polyline:
      return Gp0InputPhase::Polyline;
    case ParserKind::Upload:
      return Gp0InputPhase::CpuToVramData;
  }
  return Gp0InputPhase::Command;
}

std::uint32_t FrontEnd::status() const noexcept { return build_status(); }

const GpuControlState& FrontEnd::control() const noexcept { return control_; }

Vram& FrontEnd::vram() noexcept { return *vram_; }

const Vram& FrontEnd::vram() const noexcept { return *vram_; }

std::uint64_t FrontEnd::state_digest() const noexcept {
  std::uint64_t hash = mix_word(kFnvOffset, build_status());
  hash = mix_word(hash, static_cast<std::uint32_t>(control_.revision));
  hash = mix_word(hash, static_cast<std::uint32_t>(control_.dma_direction));
  hash = mix_word(hash, control_.texture_disable_allowed ? 1u : 0u);
  hash = mix_word(hash, control_.irq1_pending ? 1u : 0u);
  hash = mix_render_state(hash, control_.render);
  hash = mix_word(hash, control_.read_latch);
  hash = mix_word(hash, control_.read_latch_valid ? 1u : 0u);
  hash = mix_word(hash, control_.read_latch_valid_halfwords);
  hash = mix_word(hash, control_.display.display_start_x);
  hash = mix_word(hash, control_.display.display_start_y);
  hash = mix_word(hash, control_.display.horizontal_start);
  hash = mix_word(hash, control_.display.horizontal_end);
  hash = mix_word(hash, control_.display.vertical_start);
  hash = mix_word(hash, control_.display.vertical_end);
  hash = mix_word(hash, control_.display.horizontal_resolution);
  hash = mix_word(hash, control_.display.resolution_368 ? 1u : 0u);
  hash = mix_word(hash, control_.display.vertical_resolution_480 ? 1u : 0u);
  hash = mix_word(hash,
                  static_cast<std::uint32_t>(control_.display.video_standard));
  hash = mix_word(hash, control_.display.display_depth_24 ? 1u : 0u);
  hash = mix_word(hash, control_.display.interlaced ? 1u : 0u);
  hash = mix_word(hash, control_.display.reverse ? 1u : 0u);
  hash = mix_word(hash, control_.display.odd_field ? 1u : 0u);
  hash = mix_word(hash, control_.display.in_vblank ? 1u : 0u);
  hash = mix_word(hash, static_cast<std::uint32_t>(events_.size()));
  hash = mix_word(hash, static_cast<std::uint32_t>(parser_kind_));
  hash = mix_word(hash, static_cast<std::uint32_t>(expected_words_));
  hash = mix_word(hash, static_cast<std::uint32_t>(packet_words_.size()));
  for (const std::uint32_t word : packet_words_) {
    hash = mix_word(hash, word);
  }
  if (parser_kind_ == ParserKind::Upload) {
    for (const std::uint32_t word : upload_builder_.event.raw_words) {
      hash = mix_word(hash, word);
    }
    hash = mix_mask_state(hash, upload_builder_.event.mask);
    hash = mix_word(hash, static_cast<std::uint32_t>(
                              upload_builder_.event.data_words.size()));
    for (const std::uint32_t word : upload_builder_.event.data_words) {
      hash = mix_word(hash, word);
    }
  }
  hash = mix_word(hash, static_cast<std::uint32_t>(download_index_));
  hash = mix_word(hash, download_final_word_valid_halfwords_);
  hash = mix_word(hash, static_cast<std::uint32_t>(download_words_.size()));
  for (const std::uint32_t word : download_words_) {
    hash = mix_word(hash, word);
  }
  for (const Event& event : events_) {
    hash = std::visit(
        [hash](const auto& item) mutable {
          hash = mix_word(hash, static_cast<std::uint32_t>(item.kind));
          if constexpr (requires { item.raw_word_count; }) {
            hash = mix_word(hash, item.raw_word_count);
            for (std::size_t index = 0; index < item.raw_word_count; ++index) {
              hash = mix_word(hash, item.raw_words[index]);
            }
          } else if constexpr (requires { item.raw_words; }) {
            hash = mix_word(hash,
                            static_cast<std::uint32_t>(item.raw_words.size()));
            for (const std::uint32_t word : item.raw_words) {
              hash = mix_word(hash, word);
            }
          }
          if constexpr (requires { item.raw_word; }) {
            hash = mix_word(hash, item.raw_word);
          }
          if constexpr (requires { item.data_words; }) {
            hash = mix_word(hash,
                            static_cast<std::uint32_t>(item.data_words.size()));
            for (const std::uint32_t word : item.data_words) {
              hash = mix_word(hash, word);
            }
          }
          if constexpr (requires { item.state; }) {
            hash = mix_render_state(hash, item.state);
          }
          if constexpr (requires { item.mask; }) {
            hash = mix_mask_state(hash, item.mask);
          }
          if constexpr (requires { item.final_word_valid_halfwords; }) {
            hash = mix_word(hash, item.final_word_valid_halfwords);
          }
          return hash;
        },
        event);
  }
  const std::uint64_t vram_digest = vram_->digest();
  hash =
      mix_word(hash, static_cast<std::uint32_t>(vram_digest & 0xffffffffull));
  return mix_word(hash, static_cast<std::uint32_t>(vram_digest >> 32u));
}

std::optional<std::uint32_t> FrontEnd::read_internal_register(
    std::uint8_t index) const noexcept {
  const std::uint8_t mask =
      control_.revision == GpuRevision::Version0 ? 0x07u : 0x0fu;
  switch (static_cast<std::uint8_t>(index & mask)) {
    case 0x02u:
      return control_.render.texture_window.raw & 0x000fffffu;
    case 0x03u:
      return static_cast<std::uint32_t>(control_.render.draw_area.left) |
             (static_cast<std::uint32_t>(control_.render.draw_area.top) << 10u);
    case 0x04u:
      return static_cast<std::uint32_t>(control_.render.draw_area.right) |
             (static_cast<std::uint32_t>(control_.render.draw_area.bottom)
              << 10u);
    case 0x05u: {
      const std::uint32_t x =
          static_cast<std::uint32_t>(control_.render.draw_offset.x) & 0x07ffu;
      const std::uint32_t y =
          static_cast<std::uint32_t>(control_.render.draw_offset.y) & 0x07ffu;
      return x | (y << 11u);
    }
    case 0x07u:
      if (control_.revision == GpuRevision::Version0) {
        return std::nullopt;
      }
      return static_cast<std::uint32_t>(control_.revision);
    case 0x08u:
      if (control_.revision == GpuRevision::Version0) {
        return std::nullopt;
      }
      return 0;
    default:
      return std::nullopt;
  }
}

std::uint32_t FrontEnd::build_status() const noexcept {
  const DrawModeState& mode = control_.render.draw_mode;
  std::uint32_t value = 0;
  value |= static_cast<std::uint32_t>(mode.x_base & 0x0fu);
  value |= (static_cast<std::uint32_t>(mode.y_base) >= 256u ? 1u : 0u) << 4u;
  value |= static_cast<std::uint32_t>(mode.semi_transparency) << 5u;
  value |= static_cast<std::uint32_t>(mode.depth) << 7u;
  value |= (mode.dither ? 1u : 0u) << 9u;
  value |= (mode.draw_to_display ? 1u : 0u) << 10u;
  value |= (control_.render.mask.set_mask_bit ? 1u : 0u) << 11u;
  value |= (control_.render.mask.check_mask_bit ? 1u : 0u) << 12u;
  const bool status_field = control_.display.interlaced
                                ? control_.display.odd_field
                                : control_.revision == GpuRevision::Version2;
  value |= (status_field ? 1u : 0u) << 13u;
  value |=
      (control_.revision == GpuRevision::Version1 && control_.display.reverse
           ? 1u
           : 0u)
      << 14u;
  value |= (mode.texture_disable ? 1u : 0u) << 15u;
  value |= (control_.display.resolution_368 ? 1u : 0u) << 16u;
  value |= static_cast<std::uint32_t>(control_.display.horizontal_resolution)
           << 17u;
  value |=
      (control_.display.vertical_resolution_480 && control_.display.interlaced
           ? 1u
           : 0u)
      << 19u;
  value |= (control_.display.video_standard == VideoStandard::Pal ? 1u : 0u)
           << 20u;
  value |= (control_.display.display_depth_24 ? 1u : 0u) << 21u;
  value |= (control_.display.interlaced ? 1u : 0u) << 22u;
  value |= (control_.display.display_enabled ? 0u : 1u) << 23u;
  value |= (control_.irq1_pending ? 1u : 0u) << 24u;

  const bool download_ready = download_index_ < download_words_.size();
  const bool queue_ready = has_event_slot();
  const bool ready_command =
      parser_kind_ == ParserKind::Idle && !download_ready && queue_ready;
  bool ready_dma = !download_ready && queue_ready;
  if (parser_kind_ == ParserKind::Polyline) {
    ready_dma = false;
  } else if (parser_kind_ == ParserKind::Fixed && !packet_words_.empty()) {
    const std::uint8_t opcode = opcode_of(packet_words_.front());
    if (opcode >= 0x20u && opcode <= 0x5fu) {
      ready_dma = false;
    }
  }
  bool data_request = false;
  switch (control_.dma_direction) {
    case DmaDirection::Off:
      data_request = false;
      break;
    case DmaDirection::Fifo:
    case DmaDirection::CpuToGp0:
      data_request = ready_dma;
      break;
    case DmaDirection::GpureadToCpu:
      data_request = download_ready;
      break;
  }
  value |= (data_request ? 1u : 0u) << 25u;
  value |= (ready_command ? 1u : 0u) << 26u;
  value |= (download_ready ? 1u : 0u) << 27u;
  value |= (ready_dma ? 1u : 0u) << 28u;
  value |= static_cast<std::uint32_t>(control_.dma_direction) << 29u;
  value |= (!control_.display.in_vblank && control_.display.odd_field ? 1u : 0u)
           << 31u;
  return value;
}

Color FrontEnd::decode_color(std::uint32_t word) noexcept {
  return Color{word, static_cast<std::uint8_t>(word & 0xffu),
               static_cast<std::uint8_t>((word >> 8u) & 0xffu),
               static_cast<std::uint8_t>((word >> 16u) & 0xffu)};
}

Vertex FrontEnd::decode_vertex(std::uint32_t word) noexcept {
  return Vertex{word, sign_extend_11(word), sign_extend_11(word >> 16u)};
}

TextureCoordinate FrontEnd::decode_texture_coordinate(
    std::uint32_t word) noexcept {
  return TextureCoordinate{word, static_cast<std::uint8_t>(word & 0xffu),
                           static_cast<std::uint8_t>((word >> 8u) & 0xffu),
                           static_cast<std::uint16_t>((word >> 16u) & 0xffffu)};
}

ClutAttribute FrontEnd::decode_clut(std::uint16_t upper) noexcept {
  return ClutAttribute{upper,
                       static_cast<std::uint16_t>((upper & 0x003fu) * 16u),
                       static_cast<std::uint16_t>((upper >> 6u) & 0x01ffu)};
}

TexturePageAttribute FrontEnd::decode_texture_page_attribute(
    std::uint16_t upper) noexcept {
  TexturePageAttribute page{};
  page.raw = upper;
  page.x_base = static_cast<std::uint8_t>(upper & 0x0fu);
  page.y_base = static_cast<std::uint16_t>(((upper >> 4u) & 1u) * 256u);
  page.semi_transparency = static_cast<SemiTransparencyMode>(
      enum_bits(static_cast<std::uint32_t>(upper), 5u));
  page.depth = static_cast<TextureDepth>(
      enum_bits(static_cast<std::uint32_t>(upper), 7u));
  page.texture_disable_requested = (upper & 0x0800u) != 0;
  return page;
}

DrawModeState FrontEnd::decode_draw_mode(std::uint32_t word,
                                         bool previous_disable) const noexcept {
  const TexturePageAttribute page = decode_texture_page_attribute(
      static_cast<std::uint16_t>(word & 0x0000ffffu));
  DrawModeState mode{};
  mode.raw_word = word;
  mode.x_base = page.x_base;
  mode.y_base = page.y_base;
  mode.semi_transparency = page.semi_transparency;
  mode.depth = page.depth;
  mode.dither = (word & 0x00000200u) != 0;
  mode.draw_to_display = (word & 0x00000400u) != 0;
  mode.texture_disable =
      page.texture_disable_requested
          ? (control_.texture_disable_allowed || previous_disable)
          : false;
  mode.rectangle_flip_x = (word & 0x00001000u) != 0;
  mode.rectangle_flip_y = (word & 0x00002000u) != 0;
  mode.effective_bits = pack_draw_mode_bits(mode);
  return mode;
}

void FrontEnd::apply_texture_page_attribute(
    const TexturePageAttribute& attribute) noexcept {
  DrawModeState& mode = control_.render.draw_mode;
  mode.x_base = attribute.x_base;
  mode.y_base = attribute.y_base;
  mode.semi_transparency = attribute.semi_transparency;
  mode.depth = attribute.depth;
  mode.texture_disable =
      attribute.texture_disable_requested
          ? (control_.texture_disable_allowed || mode.texture_disable)
          : false;
  refresh_draw_mode_bits();
}

void FrontEnd::refresh_draw_mode_bits() noexcept {
  DrawModeState& mode = control_.render.draw_mode;
  mode.effective_bits = pack_draw_mode_bits(mode);
}

TextureWindow FrontEnd::decode_texture_window(std::uint32_t word) noexcept {
  return TextureWindow{word, static_cast<std::uint8_t>(word & 0x1fu),
                       static_cast<std::uint8_t>((word >> 5u) & 0x1fu),
                       static_cast<std::uint8_t>((word >> 10u) & 0x1fu),
                       static_cast<std::uint8_t>((word >> 15u) & 0x1fu)};
}

std::uint16_t FrontEnd::decode_draw_area_y(std::uint32_t word) const noexcept {
  const std::uint32_t mask =
      control_.revision == GpuRevision::Version0 ? 0x01ffu : 0x03ffu;
  return static_cast<std::uint16_t>((word >> 10u) & mask);
}

DrawOffset FrontEnd::decode_draw_offset(std::uint32_t word) noexcept {
  return DrawOffset{word, sign_extend_11(word), sign_extend_11(word >> 11u)};
}

MaskState FrontEnd::decode_mask(std::uint32_t word) noexcept {
  return MaskState{word, (word & 1u) != 0, (word & 2u) != 0};
}

std::uint16_t FrontEnd::normalize_copy_width(std::uint16_t raw) noexcept {
  const std::uint32_t value =
      ((static_cast<std::uint32_t>(raw) - 1u) & 0x03ffu) + 1u;
  return static_cast<std::uint16_t>(value);
}

std::uint16_t FrontEnd::normalize_copy_height(std::uint16_t raw) noexcept {
  const std::uint32_t value =
      ((static_cast<std::uint32_t>(raw) - 1u) & 0x01ffu) + 1u;
  return static_cast<std::uint16_t>(value);
}

bool FrontEnd::is_polyline_terminator(std::uint32_t word) noexcept {
  return (word & 0xf000f000u) == 0x50005000u;
}

std::int16_t FrontEnd::sign_extend_11(std::uint32_t value) noexcept {
  std::uint16_t result = static_cast<std::uint16_t>(value & 0x07ffu);
  if ((result & 0x0400u) != 0) {
    result = static_cast<std::uint16_t>(result | 0xf800u);
  }
  return static_cast<std::int16_t>(result);
}

void FrontEnd::apply_fill(VramFillEvent& event) noexcept {
  const std::uint16_t value = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(event.color.red >> 3u)) |
      (static_cast<std::uint16_t>(event.color.green >> 3u) << 5u) |
      (static_cast<std::uint16_t>(event.color.blue >> 3u) << 10u));
  for (std::uint32_t y = 0; y < event.extent.height; ++y) {
    for (std::uint32_t x = 0; x < event.extent.width; ++x) {
      vram_->write_wrapped(static_cast<std::uint32_t>(event.origin.x) + x,
                           static_cast<std::uint32_t>(event.origin.y) + y,
                           value);
    }
  }
}

void FrontEnd::apply_copy(VramCopyEvent& event) {
  // Hardware captures show a scanline buffer rather than an atomic rectangle
  // snapshot: each source row is captured before that row is written, while a
  // later row can observe writes made by an earlier row. This distinction is
  // visible for downward-overlapping copies.
  std::array<std::uint16_t, kVramWidth> source_row{};
  for (std::uint32_t y = 0; y < event.extent.height; ++y) {
    for (std::uint32_t x = 0; x < event.extent.width; ++x) {
      source_row[static_cast<std::size_t>(x)] =
          vram_->read_wrapped(static_cast<std::uint32_t>(event.source.x) + x,
                              static_cast<std::uint32_t>(event.source.y) + y);
    }
    for (std::uint32_t x = 0; x < event.extent.width; ++x) {
      const std::uint16_t old_value = vram_->read_wrapped(
          static_cast<std::uint32_t>(event.destination.x) + x,
          static_cast<std::uint32_t>(event.destination.y) + y);
      if (event.mask.check_mask_bit && (old_value & 0x8000u) != 0) {
        continue;
      }
      std::uint16_t value = source_row[static_cast<std::size_t>(x)];
      if (event.mask.set_mask_bit) {
        value = static_cast<std::uint16_t>(value | 0x8000u);
      }
      vram_->write_wrapped(static_cast<std::uint32_t>(event.destination.x) + x,
                           static_cast<std::uint32_t>(event.destination.y) + y,
                           value);
    }
  }
}

void FrontEnd::apply_upload(VramUploadEvent& event) noexcept {
  std::size_t index = 0;
  for (std::uint32_t y = 0; y < event.extent.height; ++y) {
    for (std::uint32_t x = 0; x < event.extent.width; ++x) {
      const std::uint16_t old_value = vram_->read_wrapped(
          static_cast<std::uint32_t>(event.destination.x) + x,
          static_cast<std::uint32_t>(event.destination.y) + y);
      if (event.mask.check_mask_bit && (old_value & 0x8000u) != 0) {
        ++index;
        continue;
      }
      const std::uint32_t data_word = event.data_words[index / 2u];
      std::uint16_t value =
          (index & 1u) == 0u ? low_half(data_word) : high_half(data_word);
      if (event.mask.set_mask_bit) {
        value = static_cast<std::uint16_t>(value | 0x8000u);
      }
      vram_->write_wrapped(static_cast<std::uint32_t>(event.destination.x) + x,
                           static_cast<std::uint32_t>(event.destination.y) + y,
                           value);
      ++index;
    }
  }
}

VramDownloadEvent FrontEnd::make_download(
    const std::array<std::uint32_t, 3>& words) const {
  VramDownloadEvent event{};
  event.raw_words = words;
  event.source = VramPoint{coordinate_x(words[1]), coordinate_y(words[1])};
  event.raw_width = transfer_width(words[2]);
  event.raw_height = transfer_height(words[2]);
  event.extent = VramExtent{normalize_copy_width(event.raw_width),
                            normalize_copy_height(event.raw_height)};
  event.pixel_count = static_cast<std::size_t>(event.extent.width) *
                      static_cast<std::size_t>(event.extent.height);
  event.padded_word_count = (event.pixel_count + 1u) / 2u;
  event.final_word_valid_halfwords = (event.pixel_count & 1u) != 0u ? 1u : 2u;
  event.data_words.reserve(event.padded_word_count);
  std::uint32_t pending_word = 0;
  bool have_low_half = false;
  for (std::uint32_t y = 0; y < event.extent.height; ++y) {
    for (std::uint32_t x = 0; x < event.extent.width; ++x) {
      const std::uint32_t pixel =
          vram_->read_wrapped(static_cast<std::uint32_t>(event.source.x) + x,
                              static_cast<std::uint32_t>(event.source.y) + y);
      if (!have_low_half) {
        pending_word = pixel;
        have_low_half = true;
      } else {
        event.data_words.push_back(pending_word | (pixel << 16u));
        pending_word = 0;
        have_low_half = false;
      }
    }
  }
  if (have_low_half) {
    // The high half is deliberately a deterministic sentinel. Consumers must
    // honor final_word_valid_halfwords rather than treating it as captured
    // hardware/open-bus data.
    event.data_words.push_back(pending_word);
  }
  return event;
}

}  // namespace clean_aot::gpu
