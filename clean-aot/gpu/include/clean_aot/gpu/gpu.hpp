#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace clean_aot::gpu {

// These are the physical dimensions of the one-megabyte graphics memory
// configuration modelled by this package.  They are deliberately expressed as
// GPU architectural dimensions, not as a host renderer surface size.
inline constexpr std::uint16_t kVramWidth = 1024;
inline constexpr std::uint16_t kVramHeight = 512;
inline constexpr std::size_t kVramWordCount =
    static_cast<std::size_t>(kVramWidth) *
    static_cast<std::size_t>(kVramHeight);
inline constexpr std::size_t kMaxFixedPrimitiveWords = 12;
inline constexpr std::size_t kMaxPolylineVertices = 256;
inline constexpr std::size_t kEventQueueCapacity = 256;
inline constexpr std::size_t kMaxQueuedEventWords = (kVramWordCount / 2u) + 16u;

enum class GpuRevision : std::uint8_t {
  Version0 = 0,
  Version1 = 1,
  Version2 = 2,
};

enum class DmaDirection : std::uint8_t {
  Off = 0,
  Fifo = 1,
  CpuToGp0 = 2,
  GpureadToCpu = 3,
};

enum class VideoStandard : std::uint8_t {
  Ntsc = 0,
  Pal = 1,
};

enum class TextureDepth : std::uint8_t {
  Indexed4 = 0,
  Indexed8 = 1,
  Direct15 = 2,
  Reserved = 3,
};

enum class SemiTransparencyMode : std::uint8_t {
  HalfBackgroundHalfForeground = 0,
  BackgroundPlusForeground = 1,
  BackgroundMinusForeground = 2,
  BackgroundPlusQuarterForeground = 3,
};

enum class EnvironmentKind : std::uint8_t {
  DrawMode,
  TextureWindow,
  DrawAreaTopLeft,
  DrawAreaBottomRight,
  DrawOffset,
  Mask,
};

enum class EventKind : std::uint8_t {
  CacheFlush,
  InterruptRequest,
  Polygon,
  Line,
  Rectangle,
  VramFill,
  VramCopy,
  VramUpload,
  VramDownload,
  Environment,
};

enum class Gp0InputPhase : std::uint8_t {
  Command,
  Parameters,
  Polyline,
  CpuToVramData,
  VramToCpuData,
};

enum class WriteError : std::uint8_t {
  None,
  EventQueueFull,
  UnknownGp0Command,
  MalformedPacket,
  PolylineTerminatorTooEarly,
  PolylineTooLong,
  TruncatedPacket,
  TransferBusy,
  InvalidGp1Command,
  DmaDirectionMismatch,
};

enum class ReadError : std::uint8_t {
  None,
  NotReady,
  DmaDirectionMismatch,
};

struct WriteResult {
  bool accepted = false;
  WriteError error = WriteError::None;

  [[nodiscard]] static constexpr WriteResult ok() noexcept {
    return WriteResult{true, WriteError::None};
  }

  [[nodiscard]] static constexpr WriteResult fail(WriteError value) noexcept {
    return WriteResult{false, value};
  }
};

struct ReadResult {
  bool ready = false;
  std::uint32_t word = 0;
  ReadError error = ReadError::NotReady;
  // Register reads and ordinary transfer words contain two valid halfwords.
  // The final word of an odd-pixel VRAM download contains one. Its upper
  // halfword is a deterministic host sentinel, not a claim about open-bus
  // hardware data.
  std::uint8_t valid_halfwords = 0;
};

struct Color {
  // The complete bus word is retained, including ignored upper bits on
  // non-command Gouraud color parameters.
  std::uint32_t raw = 0;
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
};

struct Vertex {
  std::uint32_t raw = 0;
  std::int16_t x = 0;
  std::int16_t y = 0;
};

struct TextureCoordinate {
  std::uint32_t raw = 0;
  std::uint8_t u = 0;
  std::uint8_t v = 0;
  std::uint16_t upper = 0;
};

struct ClutAttribute {
  std::uint32_t raw = 0;
  std::uint16_t x = 0;
  std::uint16_t y = 0;
};

struct TexturePageAttribute {
  std::uint16_t raw = 0;
  std::uint8_t x_base = 0;
  std::uint16_t y_base = 0;
  SemiTransparencyMode semi_transparency =
      SemiTransparencyMode::HalfBackgroundHalfForeground;
  TextureDepth depth = TextureDepth::Indexed4;
  bool texture_disable_requested = false;
};

struct DrawModeState {
  // Exact most-recent GP0(E1) word. Textured-polygon attributes update the
  // effective fields below without manufacturing a replacement E1 word.
  std::uint32_t raw_word = 0;
  std::uint16_t effective_bits = 0;
  std::uint8_t x_base = 0;
  std::uint16_t y_base = 0;
  SemiTransparencyMode semi_transparency =
      SemiTransparencyMode::HalfBackgroundHalfForeground;
  TextureDepth depth = TextureDepth::Indexed4;
  bool dither = false;
  bool draw_to_display = false;
  bool texture_disable = false;
  bool rectangle_flip_x = false;
  bool rectangle_flip_y = false;
};

struct TextureWindow {
  std::uint32_t raw = 0;
  std::uint8_t mask_x = 0;
  std::uint8_t mask_y = 0;
  std::uint8_t offset_x = 0;
  std::uint8_t offset_y = 0;
};

struct DrawArea {
  std::uint32_t raw_top_left = 0;
  std::uint32_t raw_bottom_right = 0;
  std::uint16_t left = 0;
  std::uint16_t top = 0;
  std::uint16_t right = 0;
  std::uint16_t bottom = 0;
};

struct DrawOffset {
  std::uint32_t raw = 0;
  std::int16_t x = 0;
  std::int16_t y = 0;
};

struct MaskState {
  std::uint32_t raw = 0;
  bool set_mask_bit = false;
  bool check_mask_bit = false;
};

struct RenderStateSnapshot {
  DrawModeState draw_mode{};
  TextureWindow texture_window{};
  DrawArea draw_area{};
  DrawOffset draw_offset{};
  MaskState mask{};
};

struct DisplayTiming {
  bool display_enabled = false;
  std::uint16_t display_start_x = 0;
  std::uint16_t display_start_y = 0;
  std::uint16_t horizontal_start = 0;
  std::uint16_t horizontal_end = 0;
  std::uint16_t vertical_start = 0;
  std::uint16_t vertical_end = 0;
  std::uint8_t horizontal_resolution = 0;
  bool resolution_368 = false;
  bool vertical_resolution_480 = false;
  VideoStandard video_standard = VideoStandard::Ntsc;
  bool display_depth_24 = false;
  bool interlaced = false;
  bool reverse = false;
  bool odd_field = false;
  bool in_vblank = true;
};

struct GpuControlState {
  GpuRevision revision = GpuRevision::Version2;
  DmaDirection dma_direction = DmaDirection::Off;
  // GP1(09) allows bit 11 of GP0(E1)/polygon texture-page attributes to set
  // the late-GPU texture-disable state. It is not an upper-VRAM-bank switch.
  bool texture_disable_allowed = false;
  bool irq1_pending = false;
  bool read_latch_valid = false;
  std::uint32_t read_latch = 0;
  std::uint8_t read_latch_valid_halfwords = 0;
  DisplayTiming display{};
  RenderStateSnapshot render{};
};

struct PolygonEvent {
  EventKind kind = EventKind::Polygon;
  std::uint8_t opcode = 0;
  std::uint32_t command_word = 0;
  bool gouraud = false;
  bool quad = false;
  bool textured = false;
  bool semi_transparent = false;
  bool raw_texture = false;
  std::uint8_t vertex_count = 0;
  Color first_color{};
  std::array<Vertex, 4> vertices{};
  std::array<Color, 4> colors{};
  std::array<bool, 4> color_present{};
  std::array<TextureCoordinate, 4> texture_coordinates{};
  std::array<bool, 4> texture_coordinate_present{};
  std::optional<ClutAttribute> clut;
  std::optional<TexturePageAttribute> inline_texture_page;
  RenderStateSnapshot state{};
  std::array<std::uint32_t, kMaxFixedPrimitiveWords> raw_words{};
  std::uint8_t raw_word_count = 0;
};

struct LineEvent {
  EventKind kind = EventKind::Line;
  std::uint8_t opcode = 0;
  std::uint32_t command_word = 0;
  bool gouraud = false;
  bool polyline = false;
  bool semi_transparent = false;
  Color first_color{};
  std::vector<Vertex> vertices;
  // Flat lines contain only the command color. Gouraud lines contain one color
  // for every vertex, preserving the command's interleaved color words.
  std::vector<Color> colors;
  std::uint32_t terminator_word = 0;
  RenderStateSnapshot state{};
  std::vector<std::uint32_t> raw_words;
};

struct RectangleEvent {
  EventKind kind = EventKind::Rectangle;
  std::uint8_t opcode = 0;
  std::uint32_t command_word = 0;
  std::uint8_t size_code = 0;
  bool textured = false;
  bool semi_transparent = false;
  bool raw_texture = false;
  Color color{};
  Vertex origin{};
  std::optional<TextureCoordinate> texture_coordinate;
  std::optional<ClutAttribute> clut;
  bool raw_extent_present = false;
  std::uint16_t raw_width = 0;
  std::uint16_t raw_height = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  RenderStateSnapshot state{};
  std::array<std::uint32_t, kMaxFixedPrimitiveWords> raw_words{};
  std::uint8_t raw_word_count = 0;
};

struct VramPoint {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
};

struct VramExtent {
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

struct VramFillEvent {
  EventKind kind = EventKind::VramFill;
  std::array<std::uint32_t, 3> raw_words{};
  Color color{};
  VramPoint origin{};
  VramExtent extent{};
  std::uint16_t raw_width = 0;
  std::uint16_t raw_height = 0;
};

struct VramCopyEvent {
  EventKind kind = EventKind::VramCopy;
  std::array<std::uint32_t, 4> raw_words{};
  VramPoint source{};
  VramPoint destination{};
  VramExtent extent{};
  std::uint16_t raw_width = 0;
  std::uint16_t raw_height = 0;
  MaskState mask{};
};

struct VramUploadEvent {
  EventKind kind = EventKind::VramUpload;
  std::array<std::uint32_t, 3> raw_words{};
  VramPoint destination{};
  VramExtent extent{};
  std::uint16_t raw_width = 0;
  std::uint16_t raw_height = 0;
  std::size_t pixel_count = 0;
  std::size_t padded_word_count = 0;
  std::uint8_t final_word_valid_halfwords = 2;
  std::vector<std::uint32_t> data_words;
  MaskState mask{};
};

struct VramDownloadEvent {
  EventKind kind = EventKind::VramDownload;
  std::array<std::uint32_t, 3> raw_words{};
  VramPoint source{};
  VramExtent extent{};
  std::uint16_t raw_width = 0;
  std::uint16_t raw_height = 0;
  std::size_t pixel_count = 0;
  std::size_t padded_word_count = 0;
  std::uint8_t final_word_valid_halfwords = 2;
  std::vector<std::uint32_t> data_words;
};

struct EnvironmentEvent {
  EventKind kind = EventKind::Environment;
  EnvironmentKind environment = EnvironmentKind::DrawMode;
  std::uint32_t raw_word = 0;
  RenderStateSnapshot state{};
};

struct CacheFlushEvent {
  EventKind kind = EventKind::CacheFlush;
  std::uint32_t raw_word = 0;
};

struct InterruptRequestEvent {
  EventKind kind = EventKind::InterruptRequest;
  std::uint32_t raw_word = 0;
};

using Event =
    std::variant<CacheFlushEvent, InterruptRequestEvent, PolygonEvent,
                 LineEvent, RectangleEvent, VramFillEvent, VramCopyEvent,
                 VramUploadEvent, VramDownloadEvent, EnvironmentEvent>;

class Vram final {
 public:
  Vram() noexcept;

  void reset(std::uint16_t value = 0) noexcept;

  [[nodiscard]] std::uint16_t read_wrapped(std::uint32_t x,
                                           std::uint32_t y) const noexcept;
  void write_wrapped(std::uint32_t x, std::uint32_t y,
                     std::uint16_t value) noexcept;

  [[nodiscard]] std::span<const std::uint16_t> words() const noexcept;
  [[nodiscard]] std::uint64_t digest() const noexcept;

 private:
  std::array<std::uint16_t, kVramWordCount> words_{};
};

class FrontEnd final {
 public:
  explicit FrontEnd(GpuRevision revision = GpuRevision::Version2);
  // Ownership-complete hardware snapshot. Copies include VRAM, partial GP0
  // parsing, GPUREAD progress, queued events, and all control latches.
  FrontEnd(const FrontEnd& other);
  FrontEnd& operator=(const FrontEnd& other);
  FrontEnd(FrontEnd&&) noexcept = default;
  FrontEnd& operator=(FrontEnd&&) noexcept = default;

  // This deterministic cold reset clears VRAM. Physical power-on VRAM contents
  // are unspecified; callers that model boot hardware must account for that.
  // GP1(00) preserves VRAM and the GP1(09) texture-disable permission latch.
  void power_on_reset() noexcept;

  [[nodiscard]] WriteResult write_gp0(std::uint32_t word);
  [[nodiscard]] WriteResult write_gp1(std::uint32_t word);
  [[nodiscard]] WriteResult write_dma_word(std::uint32_t word);
  [[nodiscard]] WriteResult finish_gp0_stream();
  [[nodiscard]] ReadResult read_gpuread();
  [[nodiscard]] ReadResult read_dma_word();

  void clear_command_buffer() noexcept;
  void set_scanout_state(bool odd_field, bool in_vblank) noexcept;

  [[nodiscard]] std::optional<Event> pop_event() noexcept;
  [[nodiscard]] std::size_t event_count() const noexcept;
  // Number of words retained by the transactional parser. This is deliberately
  // not exposed as physical 16-word FIFO occupancy; execution timing belongs
  // to the SoC scheduler.
  [[nodiscard]] std::size_t staged_word_count() const noexcept;
  [[nodiscard]] Gp0InputPhase input_phase() const noexcept;
  [[nodiscard]] std::uint32_t status() const noexcept;
  [[nodiscard]] const GpuControlState& control() const noexcept;
  [[nodiscard]] const Vram& vram() const noexcept;
  [[nodiscard]] std::uint64_t state_digest() const noexcept;

 private:
  enum class ParserKind : std::uint8_t {
    Idle,
    Fixed,
    Polyline,
    Upload,
  };

  enum class PolylineExpectation : std::uint8_t {
    Vertex,
    ColorOrTerminator,
    VertexOrTerminator,
  };

  struct LineBuilder {
    LineEvent event{};
    PolylineExpectation expectation = PolylineExpectation::Vertex;
  };

  struct UploadBuilder {
    VramUploadEvent event{};
    std::size_t expected_words = 0;
  };

  [[nodiscard]] WriteResult begin_gp0(std::uint32_t word);
  [[nodiscard]] WriteResult continue_fixed(std::uint32_t word);
  [[nodiscard]] WriteResult continue_polyline(std::uint32_t word);
  [[nodiscard]] WriteResult continue_upload(std::uint32_t word);
  [[nodiscard]] WriteResult finish_fixed();
  [[nodiscard]] WriteResult finish_polyline();
  [[nodiscard]] WriteResult finish_upload();

  [[nodiscard]] WriteResult enqueue(Event event);
  [[nodiscard]] bool has_event_slot() const noexcept;
  [[nodiscard]] bool can_enqueue_word_cost(std::size_t cost) const noexcept;
  [[nodiscard]] bool can_enqueue(const Event& event) const noexcept;
  [[nodiscard]] static std::size_t event_word_cost(const Event& event) noexcept;
  void abandon_packet() noexcept;

  [[nodiscard]] WriteResult apply_gp1(std::uint8_t command,
                                      std::uint32_t parameter);
  [[nodiscard]] std::optional<std::uint32_t> read_internal_register(
      std::uint8_t index) const noexcept;
  [[nodiscard]] std::uint32_t build_status() const noexcept;

  [[nodiscard]] static Color decode_color(std::uint32_t word) noexcept;
  [[nodiscard]] static Vertex decode_vertex(std::uint32_t word) noexcept;
  [[nodiscard]] static TextureCoordinate decode_texture_coordinate(
      std::uint32_t word) noexcept;
  [[nodiscard]] static ClutAttribute decode_clut(std::uint16_t upper) noexcept;
  [[nodiscard]] static TexturePageAttribute decode_texture_page_attribute(
      std::uint16_t upper) noexcept;
  [[nodiscard]] DrawModeState decode_draw_mode(
      std::uint32_t word, bool previous_disable) const noexcept;
  void apply_texture_page_attribute(
      const TexturePageAttribute& attribute) noexcept;
  void refresh_draw_mode_bits() noexcept;
  [[nodiscard]] static TextureWindow decode_texture_window(
      std::uint32_t word) noexcept;
  [[nodiscard]] std::uint16_t decode_draw_area_y(
      std::uint32_t word) const noexcept;
  [[nodiscard]] static DrawOffset decode_draw_offset(
      std::uint32_t word) noexcept;
  [[nodiscard]] static MaskState decode_mask(std::uint32_t word) noexcept;
  [[nodiscard]] static std::uint16_t normalize_copy_width(
      std::uint16_t raw) noexcept;
  [[nodiscard]] static std::uint16_t normalize_copy_height(
      std::uint16_t raw) noexcept;
  [[nodiscard]] static bool is_polyline_terminator(std::uint32_t word) noexcept;
  [[nodiscard]] static std::int16_t sign_extend_11(
      std::uint32_t value) noexcept;

  void apply_fill(VramFillEvent& event) noexcept;
  void apply_copy(VramCopyEvent& event);
  void apply_upload(VramUploadEvent& event) noexcept;
  [[nodiscard]] VramDownloadEvent make_download(
      const std::array<std::uint32_t, 3>& words) const;
  GpuControlState control_{};
  std::unique_ptr<Vram> vram_;
  ParserKind parser_kind_ = ParserKind::Idle;
  std::size_t expected_words_ = 0;
  std::vector<std::uint32_t> packet_words_;
  LineBuilder line_builder_{};
  UploadBuilder upload_builder_{};
  std::vector<std::uint32_t> download_words_;
  std::uint8_t download_final_word_valid_halfwords_ = 2;
  std::size_t download_index_ = 0;
  std::deque<Event> events_;
  std::size_t queued_event_words_ = 0;
};

}  // namespace clean_aot::gpu
