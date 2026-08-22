#pragma once

#include "clean_aot/gpu/gpu.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace clean_aot::raster {

inline constexpr std::uint16_t kVramWidth = gpu::kVramWidth;
inline constexpr std::uint16_t kVramHeight = gpu::kVramHeight;
inline constexpr std::size_t kVramWordCount = gpu::kVramWordCount;
inline constexpr std::uint32_t kNativeBackendAbiVersion = 1u;
inline constexpr std::uint64_t kDefaultMaxWorkUnits =
    static_cast<std::uint64_t>(kVramWordCount) * 8u;

using VramWord = std::uint16_t;

struct VramRect {
  std::uint16_t x = 0;
  std::uint16_t y = 0;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

enum class EventStatus : std::uint8_t {
  Applied,
  Ignored,
  Rejected,
};

enum class RasterError : std::uint8_t {
  None,
  WorkLimitExceeded,
  MalformedEvent,
  UnsupportedEvent,
  InvalidTexture,
  TransferDataMismatch,
  InvalidPresentationRequest,
  InvalidScanoutRequest,
  ScanoutBufferTooSmall,
};

struct WorkBudget {
  std::size_t max_events = gpu::kEventQueueCapacity;
  std::uint64_t max_work_units = kDefaultMaxWorkUnits;
};

struct RasterLimits {
  std::size_t max_polyline_vertices = gpu::kMaxPolylineVertices;
  std::uint64_t max_work_units_per_event = kDefaultMaxWorkUnits;
};

struct ReplacementTexture final {
  std::uint64_t handle = 0;
  VramRect source{};
  gpu::TextureDepth depth = gpu::TextureDepth::Indexed4;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::span<const std::uint8_t> rgba8{};
};

// Validated, projection-adjusted polygon vertex. The producer must supply a
// current storage and origin generation; the rasterizer fails closed when a
// triangle contains partial or invalid precise state.
struct PreciseVertex final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double w = 1.0;
  double u = 0.0;
  double v = 0.0;
  std::uint32_t color_rgba = 0U;
  std::uint64_t storage_generation = 0U;
  std::uint64_t origin_generation = 0U;
};

enum class PreciseCullingPolicy : std::uint8_t {
  NativeHardware,
  ProjectedBounds,
};

struct EventResult {
  EventStatus status = EventStatus::Rejected;
  RasterError error = RasterError::MalformedEvent;
  std::uint64_t work_units = 0;
  // This view borrows the immutable VramDownloadEvent supplied to consume().
  // It is empty for every other event and remains valid only while that event
  // object remains alive and unchanged.
  std::span<const std::uint32_t> download_words{};

  [[nodiscard]] constexpr bool accepted() const noexcept {
    return status != EventStatus::Rejected;
  }
};

struct BatchResult {
  bool completed = false;
  RasterError error = RasterError::None;
  std::size_t events_processed = 0;
  std::uint64_t work_units = 0;
};

enum class PresentationMode : std::uint8_t {
  // The source is exposed as a 4:3 logical surface. No title-specific crop,
  // stretch, or widescreen policy is implied by this mode.
  Identity4x3,
  Generic16x9,
  Generic21x9,
  Generic32x9,
  Custom,
};

struct PresentationRequest {
  std::uint32_t output_width = 0;
  std::uint32_t output_height = 0;
  VramRect source{0, 0, 320, 240};
  PresentationMode mode = PresentationMode::Identity4x3;
  std::uint32_t custom_aspect_width = 4;
  std::uint32_t custom_aspect_height = 3;
};

struct PresentationPlan {
  VramRect source{};
  std::uint32_t destination_x = 0;
  std::uint32_t destination_y = 0;
  std::uint32_t destination_width = 0;
  std::uint32_t destination_height = 0;
  std::uint32_t aspect_width = 4;
  std::uint32_t aspect_height = 3;
  bool identity_4x3 = false;
};

struct PresentationResult {
  bool accepted = false;
  RasterError error = RasterError::InvalidPresentationRequest;
  PresentationPlan plan{};
};

class PresentationMapper final {
public:
  [[nodiscard]] static PresentationResult
  map(const PresentationRequest &request) noexcept;
};

enum class ScanoutPixelFormat : std::uint8_t {
  Rgb555,
  PackedRgb888,
};

enum class ScanoutField : std::uint8_t {
  Progressive,
  Even,
  Odd,
};

struct ScanoutRequest {
  gpu::DisplayTiming display{};
  ScanoutField field = ScanoutField::Progressive;
  std::uint32_t output_width = 0;
  std::uint32_t output_height = 0;
  PresentationMode presentation_mode = PresentationMode::Identity4x3;
  std::uint32_t custom_aspect_width = 4;
  std::uint32_t custom_aspect_height = 3;
};

struct ScanoutPlan {
  bool display_enabled = false;
  ScanoutPixelFormat pixel_format = ScanoutPixelFormat::Rgb555;
  ScanoutField field = ScanoutField::Progressive;
  std::uint16_t vram_start_x_words = 0;
  std::uint16_t vram_start_y = 0;
  std::uint16_t logical_width = 0;
  std::uint16_t logical_height = 0;
  std::uint16_t source_line_step = 1;
  std::uint16_t source_field_offset = 0;
  std::uint16_t horizontal_start = 0;
  std::uint16_t horizontal_end = 0;
  std::uint16_t vertical_start = 0;
  std::uint16_t vertical_end = 0;
  std::size_t required_rgb_bytes = 0;
  PresentationPlan presentation{};
};

struct ScanoutResult {
  bool accepted = false;
  RasterError error = RasterError::InvalidScanoutRequest;
  ScanoutPlan plan{};
};

class ScanoutMapper final {
public:
  [[nodiscard]] static ScanoutResult plan(const ScanoutRequest &request) noexcept;
  [[nodiscard]] static EventResult decode_rgb888(
      const ScanoutPlan &plan, std::span<const VramWord> vram,
      std::span<std::uint8_t> destination) noexcept;
};

using BackendConsumeEvent = EventResult (*)(void *context,
                                            const gpu::Event *event,
                                            WorkBudget *budget) noexcept;
using BackendReadVram = std::span<const VramWord> (*)(void *context) noexcept;
using BackendDigest = std::uint64_t (*)(void *context) noexcept;
using BackendPlanPresentation = PresentationResult (*)(
    void *context, const PresentationRequest *request) noexcept;

// This is deliberately a non-owning, same-process C++ function table rather
// than a stable C or shared-library ABI. Producer and consumer must agree on
// the compiler/STL ABI and the exact gpu::Event definition. Events cross the
// seam by const pointer, and the caller owns the context, lifetime, and work
// budget. Returned spans borrow backend storage. A hardware backend can
// implement the same table without inheriting from the reference rasterizer or
// depending on a window-system library.
struct NativeBackendV1 {
  std::uint32_t abi_version = kNativeBackendAbiVersion;
  void *context = nullptr;
  BackendConsumeEvent consume_event = nullptr;
  BackendReadVram read_vram = nullptr;
  BackendDigest digest = nullptr;
  BackendPlanPresentation plan_presentation = nullptr;
};

[[nodiscard]] bool
native_backend_compatible(const NativeBackendV1 &backend) noexcept;

class ReferenceRasterizer final {
public:
  explicit ReferenceRasterizer(RasterLimits limits = {});
  ~ReferenceRasterizer() noexcept;

  ReferenceRasterizer(const ReferenceRasterizer &) = delete;
  ReferenceRasterizer &operator=(const ReferenceRasterizer &) = delete;
  ReferenceRasterizer(ReferenceRasterizer &&) = delete;
  ReferenceRasterizer &operator=(ReferenceRasterizer &&) = delete;

  void reset(VramWord value = 0) noexcept;

  [[nodiscard]] EventResult consume(const gpu::Event &event) noexcept;
  [[nodiscard]] EventResult consume(
      const gpu::Event &event,
      const ReplacementTexture *replacement) noexcept;
  [[nodiscard]] EventResult consume_precise_polygon(
      const gpu::PolygonEvent &event,
      std::span<const PreciseVertex> triangle_vertices,
      const ReplacementTexture *replacement = nullptr,
      PreciseCullingPolicy culling =
          PreciseCullingPolicy::NativeHardware) noexcept;
  [[nodiscard]] EventResult consume(const gpu::Event &event,
                                    WorkBudget &budget) noexcept;
  [[nodiscard]] BatchResult
  consume(std::span<const gpu::Event> events) noexcept;
  [[nodiscard]] BatchResult consume(std::span<const gpu::Event> events,
                                    WorkBudget budget) noexcept;

  [[nodiscard]] VramWord read_vram(std::uint32_t x,
                                   std::uint32_t y) const noexcept;
  [[nodiscard]] std::span<const VramWord> vram_words() const noexcept;
  [[nodiscard]] std::uint64_t digest() const noexcept;
  [[nodiscard]] std::uint64_t vram_generation() const noexcept;
  [[nodiscard]] const RasterLimits &limits() const noexcept;

  [[nodiscard]] NativeBackendV1 native_backend() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace clean_aot::raster
