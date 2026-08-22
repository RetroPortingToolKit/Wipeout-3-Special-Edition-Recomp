#include "clean_aot/host/headless.hpp"
#include "clean_aot/host/pacing.hpp"
#include "clean_aot/host/viewport.hpp"
#include "clean_aot/host/render_interfaces.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <variant>
#include <vector>

namespace {

using clean_aot::host::AspectPolicy;
using clean_aot::host::FrameSchedule;
using clean_aot::host::FrameSubmission;
using clean_aot::host::HeadlessBackend;
using clean_aot::host::HostCode;
using clean_aot::host::IMonotonicClock;
using clean_aot::host::MonotonicPacer;
using clean_aot::host::PaceDecision;
using clean_aot::host::PixelFormat;
using clean_aot::host::ScaleMode;
using clean_aot::host::StereoAudioBatch;
using clean_aot::host::Viewport;
using clean_aot::host::ViewportRequest;

struct TestClock final : IMonotonicClock {
    std::uint64_t now = 10'000U;
    std::uint64_t sleeps = 0U;
    bool advance_on_sleep = true;

    [[nodiscard]] std::uint64_t now_ns() const noexcept override {
        return now;
    }

    void sleep_until_ns(std::uint64_t deadline_ns) noexcept override {
        ++sleeps;
        if (advance_on_sleep && deadline_ns > now) {
            now = deadline_ns;
        }
    }
};

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_pacing() {
    for (const std::uint32_t rate : {25U, 50U, 100U}) {
        TestClock clock;
        MonotonicPacer pacer{FrameSchedule::from_rate(rate)};
        expect(pacer.start(clock.now).success(), "pacer starts once");
        for (std::uint64_t index = 0U; index < 400U; ++index) {
            const auto result = pacer.wait(clock);
            expect(result.decision == PaceDecision::ready,
                   "exact fake clock is ready at each deadline");
            const std::uint64_t ideal = 10'000U +
                (index * 1'000'000'000ULL) / rate;
            expect(result.deadline_ns == ideal,
                   "rational deadline has no accumulated drift");
            expect(pacer.commit().success(), "pacer commits exactly once");
        }
        expect(clock.sleeps == 399U, "pacer sleeps only before future deadlines");
        expect(pacer.frame_index() == 400U, "pacer frame index is exact");
    }

    MonotonicPacer invalid{FrameSchedule::from_rate(0U)};
    expect(!invalid.start(0U).success(), "zero-rate pacer is rejected");

    TestClock protocol_clock;
    MonotonicPacer protocol{FrameSchedule::from_rate(100U)};
    expect(protocol.commit().code == HostCode::not_ready,
           "unstarted pacer cannot commit");
    expect(protocol.start(protocol_clock.now).success(),
           "protocol pacer starts");
    expect(protocol.commit().code == HostCode::sequence_error,
           "pacer cannot commit before a successful wait");
    expect(protocol.wait(protocol_clock).decision == PaceDecision::ready,
           "protocol wait authorizes one commit");
    expect(protocol.wait(protocol_clock).decision == PaceDecision::ready,
           "repeated wait is idempotent before commit");
    expect(protocol.commit().success(), "authorized commit succeeds");
    expect(protocol.commit().code == HostCode::sequence_error,
           "pacer rejects a duplicate commit");
    protocol_clock.now += 20'000'000U;
    expect(protocol.wait(protocol_clock).decision == PaceDecision::late,
           "late protocol wait reports lateness");
    expect(protocol.wait(protocol_clock).decision == PaceDecision::late,
           "idempotent wait preserves the original decision");
    expect(protocol.commit().success(), "late frame can commit once");

    TestClock stalled_clock;
    stalled_clock.advance_on_sleep = false;
    MonotonicPacer stalled{FrameSchedule::from_rate(100U)};
    expect(stalled.start(stalled_clock.now).success(),
           "stalled-clock pacer starts");
    expect(stalled.wait(stalled_clock).decision == PaceDecision::ready,
           "stalled-clock pacer reaches its origin");
    expect(stalled.commit().success(), "stalled-clock pacer advances");
    expect(stalled.wait(stalled_clock).decision == PaceDecision::clock_stalled,
           "pacer rejects a sleep that did not reach its deadline");
}

void test_headless_frame_schedules() {
    constexpr std::uint64_t kFNVPrime = 1'099'511'628'211ULL;
    for (const std::uint32_t rate : {25U, 50U, 100U}) {
        HeadlessBackend backend;
        TestClock clock;
        MonotonicPacer pacer{FrameSchedule::from_rate(rate)};
        std::array<std::array<std::uint8_t, 16U>, 300U> frame_pixels{};
        std::uint64_t expected_digest = 1469598103934665603ULL;
        expect(pacer.start(clock.now).success(), "schedule starts");

        for (std::uint64_t index = 0U; index < 300U; ++index) {
            expect(pacer.wait(clock).decision != PaceDecision::not_started,
                   "frame is not submitted before its deadline");
            for (std::size_t byte = 0U; byte < frame_pixels[index].size();
                 ++byte) {
                frame_pixels[index][byte] = static_cast<std::uint8_t>(
                    (index * 17U + byte) & 0xffU);
            }
            for (const std::uint8_t byte : frame_pixels[index]) {
                expected_digest ^= byte;
                expected_digest *= kFNVPrime;
            }
            const FrameSubmission submission{
                index,
                index,
                {frame_pixels[index].data(),
                 static_cast<std::uint32_t>(frame_pixels[index].size()), 2U,
                 2U, 8U, PixelFormat::rgba8_unorm}};
            HostCode submit_code = backend.submit_frame(submission).code;
            if (submit_code == HostCode::queue_full) {
                expect(backend.service().success(),
                       "backpressure drains the oldest queued frame");
                submit_code = backend.submit_frame(submission).code;
            }
            expect(submit_code == HostCode::ok,
                   "headless accepts or retries every ordered frame");
            expect(pacer.commit().success(), "frame schedule advances once");

            if (index % 11U == 7U) {
                expect(backend.service().success(),
                       "adversarial burst drains its first frame");
                expect(backend.service().success(),
                       "adversarial burst drains its second frame");
            }
        }
        for (std::size_t drain = 0U; drain < 300U &&
             backend.stats().frames_presented < 300U; ++drain) {
            expect(backend.service().success(),
                   "all backlogged frames drain in order");
        }
        expect(backend.stats().frames_submitted == 300U,
               "headless submitted every scheduled frame");
        expect(backend.stats().frames_presented == 300U,
               "headless presented every scheduled frame");
        expect(backend.stats().sequence_errors == 0U,
               "headless did not duplicate or drop ordered frames");
        expect(backend.stats().queue_full_errors != 0U,
               "adversarial schedule exercised bounded backpressure");
        expect(backend.stats().frame_digest == expected_digest,
               "presented frame bytes match every submitted frame exactly");
    }
}

void test_bounded_queue_and_audio() {
    HeadlessBackend backend;
    std::array<std::uint8_t, 16U> pixels{};
    const FrameSubmission frame{
        0U,
        0U,
        {pixels.data(), static_cast<std::uint32_t>(pixels.size()),
         2U, 2U, 8U, PixelFormat::rgba8_unorm}};
    for (std::uint64_t index = 0U; index < 4U; ++index) {
        FrameSubmission queued = frame;
        queued.sequence = index;
        queued.update_index = index;
        expect(backend.submit_frame(queued).success(),
               "bounded queue accepts its capacity");
    }
    FrameSubmission overflow = frame;
    overflow.sequence = 4U;
    overflow.update_index = 4U;
    expect(backend.submit_frame(overflow).code == HostCode::queue_full,
           "bounded queue rejects instead of overwriting");
    for (std::uint64_t index = 0U; index < 4U; ++index) {
        expect(backend.service().success(), "queued frame drains in order");
    }
    expect(backend.submit_frame(overflow).success(),
           "caller can retry rejected frame after backpressure");
    expect(backend.service().success(), "retried frame is presented");

    std::array<std::uint8_t, 12U> tight_last_row{
        1U, 2U, 3U, 4U, 99U, 98U, 97U, 96U, 5U, 6U, 7U, 8U};
    const FrameSubmission pitched{
        5U, 5U,
        {tight_last_row.data(),
         static_cast<std::uint32_t>(tight_last_row.size()),
         1U, 2U, 8U, PixelFormat::rgba8_unorm}};
    expect(backend.submit_frame(pitched).success(),
           "last row may contain only its visible bytes");
    expect(backend.service().success(),
           "pitched frame does not read padding beyond the final row");

    FrameSubmission skipped = frame;
    skipped.sequence = 9U;
    skipped.update_index = 9U;
    expect(backend.submit_frame(skipped).success(),
           "out-of-sequence frame enters the bounded queue");
    expect(backend.service().code == HostCode::sequence_error,
           "sequence gap is reported while consuming the bad frame");
    skipped.sequence = 10U;
    skipped.update_index = 10U;
    expect(backend.submit_frame(skipped).success(),
           "sequence can continue after a reported gap");
    expect(backend.service().success(),
           "sequence recovery does not wedge the pending frame");

    std::uint8_t one_pixel = 0U;
    const FrameSubmission impossible_frame{
        5U,
        5U,
        {&one_pixel,
         std::numeric_limits<std::uint32_t>::max(),
         std::numeric_limits<std::uint32_t>::max(),
         std::numeric_limits<std::uint32_t>::max(),
         std::numeric_limits<std::uint32_t>::max(),
         PixelFormat::rgba8_unorm}};
    expect(backend.submit_frame(impossible_frame).code ==
               HostCode::invalid_argument,
           "overflowing frame storage is rejected without arithmetic wrap");

    std::array<std::int16_t, 882U> samples{};
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        samples[index] = static_cast<std::int16_t>(index & 0x7fffU);
    }
    StereoAudioBatch bad{0U, samples.data(), 440U};
    expect(backend.submit_audio(bad).code == HostCode::invalid_argument,
           "short audio batch is rejected");
    bad.sample_frames = 442U;
    expect(backend.submit_audio(bad).code == HostCode::invalid_argument,
           "long audio batch is rejected");

    for (std::uint64_t update = 0U; update < 100U; ++update) {
        const StereoAudioBatch audio{update, samples.data(), 441U};
        expect(backend.submit_audio(audio).success(),
               "exact stereo audio batch is accepted");
        expect(backend.service().success(), "exact audio batch is consumed");
    }
    expect(backend.stats().audio_batches_consumed == 100U,
           "all audio batches are consumed");
    expect(backend.stats().audio_sample_frames == 44'100U,
           "100 Hz audio totals exactly 44.1 kHz");

    clean_aot::host::ControllerState controller{};
    controller.connected_mask = 1U;
    controller.buttons = 0x4000U;
    expect(backend.submit_controller(controller).success(),
           "controller state is accepted");
    expect(backend.service().success(), "controller state is serviced");
    expect(backend.stats().controller.buttons == 0x4000U,
           "controller state is not rewritten by presentation timing");

    clean_aot::host::WindowRequest window{{1920U, 1080U,
                                           clean_aot::host::FullscreenMode::borderless}};
    expect(backend.request_window(window).success(), "window request accepted");
    clean_aot::host::HostEvent event{};
    expect(backend.poll_event(event), "window event is observable");
    expect(event.kind == clean_aot::host::HostEventKind::fullscreen_changed,
           "fullscreen event has the correct kind");
    backend.shutdown();
    expect(backend.submit_frame(frame).code == HostCode::shutting_down,
           "shutdown closes all submission paths");
}

void test_viewports() {
    for (const AspectPolicy policy : {
             AspectPolicy::preserve_4x3,
             AspectPolicy::preserve_16x9,
             AspectPolicy::preserve_21x9,
             AspectPolicy::preserve_32x9}) {
        ViewportRequest request{3840U, 2160U, 320U, 240U, policy, {},
                                ScaleMode::fractional};
        Viewport viewport{};
        expect(clean_aot::host::calculate_viewport(request, viewport).success(),
               "wide viewport policy is valid");
        expect(viewport.width > 0U && viewport.height > 0U,
               "wide viewport remains visible");
        expect(viewport.x >= 0 && viewport.y >= 0,
               "wide viewport is centered without negative coordinates");
        expect(viewport.x + static_cast<std::int32_t>(viewport.width) <= 3840,
               "viewport fits horizontally");
        expect(viewport.y + static_cast<std::int32_t>(viewport.height) <= 2160,
               "viewport fits vertically");
    }

    ViewportRequest integer_request{1920U, 1080U, 320U, 240U,
                                    AspectPolicy::preserve_4x3, {},
                                    ScaleMode::integer};
    Viewport integer_viewport{};
    expect(clean_aot::host::calculate_viewport(integer_request,
                                               integer_viewport).success(),
           "integer viewport is valid");
    expect(integer_viewport.width == 1280U && integer_viewport.height == 960U,
           "integer scaling chooses the largest exact 4:3 multiple");
    expect(integer_viewport.x == 320 && integer_viewport.y == 60,
           "integer viewport is centered");

    ViewportRequest impossible_request{
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),
        AspectPolicy::custom,
        {std::numeric_limits<std::uint32_t>::max(), 1U},
        ScaleMode::fractional};
    Viewport impossible_viewport{};
    expect(!clean_aot::host::calculate_viewport(
                 impossible_request, impossible_viewport)
                 .success(),
           "viewport dimensions that cannot fit the contract fail closed");

    for (const auto exact : std::array{
             clean_aot::host::AspectRatio{4U, 3U},
             clean_aot::host::AspectRatio{16U, 9U},
             clean_aot::host::AspectRatio{21U, 9U},
             clean_aot::host::AspectRatio{32U, 9U},
             clean_aot::host::AspectRatio{64U, 27U}}) {
        const ViewportRequest request{3840U, 2160U, 320U, 239U,
                                      AspectPolicy::custom, exact,
                                      ScaleMode::fractional};
        Viewport viewport{};
        expect(clean_aot::host::calculate_viewport(request, viewport).success(),
               "fractional exact-ratio viewport is valid");
        expect(static_cast<std::uint64_t>(viewport.width) * exact.height ==
                   static_cast<std::uint64_t>(viewport.height) * exact.width,
               "fractional viewport preserves its exact rational ratio");
        expect(static_cast<std::uint64_t>(viewport.logical_width) * exact.height ==
                   static_cast<std::uint64_t>(viewport.logical_height) * exact.width,
               "logical viewport preserves its exact rational ratio");
    }
}

void test_precise_geometry_bridge_visual_digest() {
    psx::render::EmittedTriangle triangle{};
    for (std::size_t index = 0U; index < triangle.vertices.size(); ++index) {
        auto& vertex = triangle.vertices[index];
        vertex.native = psx::render::NativePosition{
            static_cast<std::int16_t>(10 + index),
            static_cast<std::int16_t>(20 + index)};
        vertex.precise = psx::render::PreciseCoordinate{
            static_cast<double>(index + 1U), static_cast<double>(index + 2U),
            0.5, 2.0};
        vertex.uv = psx::render::NativeUv{static_cast<std::uint8_t>(index * 127U),
                                          static_cast<std::uint8_t>(255U - index * 127U), 0U};
        vertex.color = psx::render::Rgba8{10U, 20U, 30U, 255U};
        vertex.identity = psx::render::PreciseVertexIdentity{
            {{psx::render::ProvenanceSourceKind::MemoryWord,
              static_cast<std::uint32_t>(0x100U + index * 4U)}, index + 10U},
            {{psx::render::ProvenanceSourceKind::GteRegister,
              static_cast<std::uint32_t>(index)}, index + 20U}};
    }
    psx::render::AssembledPolygon polygon{};
    polygon.triangles.push_back(triangle);
    const auto projection = psx::render::ProjectionPolicyPlanner::plan(
        {psx::render::ProjectionPolicy::ExpandHorizontal,
         psx::render::CullingPolicy::MatchExpandedProjection, 32U, 9U});
    expect(projection.accepted, "ultrawide projection plan is valid");
    std::array<clean_aot::host::RenderVertex, 3> vertices{};
    std::array<std::uint16_t, 3> indices{};
    const auto result = clean_aot::host::build_render_geometry(
        polygon, projection.plan, vertices, indices);
    expect(result.status.success() && result.geometry.vertex_count == 3U &&
               result.geometry.index_count == 3U,
           "precise triangle crosses the host geometry bridge");
    expect(vertices[0].x == 0.375F && vertices[0].y == 2.0F &&
               vertices[0].w == 2.0F && vertices[0].storage_generation == 10U &&
               vertices[0].origin_generation == 20U,
           "projection and provenance survive the host bridge");
    expect(result.culling == psx::render::CullingPolicy::MatchExpandedProjection,
           "expanded culling policy survives the host bridge");
    expect(result.visual_digest == 0x032a082a4e99fe27ULL,
           "precise geometry visual digest changed");

    auto fallback_polygon = polygon;
    fallback_polygon.triangles[0].fallback_reason =
        psx::render::FallbackReason::MissingProvenance;
    const auto rejected = clean_aot::host::build_render_geometry(
        fallback_polygon, projection.plan, vertices, indices);
    expect(rejected.status.code == HostCode::unsupported,
           "ultrawide geometry did not fail closed on native fallback");
}

void test_gte_memory_gpu_host_provenance_chain() {
    clean_aot::gpu::FrontEnd gpu;
    const std::array<std::uint32_t, 4> words{
        0x200a141eU,
        (20U << 16U) | 10U,
        (25U << 16U) | 30U,
        (40U << 16U) | 15U,
    };
    for (const std::uint32_t word : words) {
        expect(gpu.write_gp0(word).accepted, "GPU accepts precise-chain packet word");
    }
    auto event = gpu.pop_event();
    expect(event.has_value() &&
               std::holds_alternative<clean_aot::gpu::PolygonEvent>(*event),
           "GPU emits polygon for precise-chain packet");
    if (!event.has_value() ||
        !std::holds_alternative<clean_aot::gpu::PolygonEvent>(*event)) {
        return;
    }

    psx::render::ProvenanceState provenance;
    const std::array native{
        psx::render::NativePosition{10, 20},
        psx::render::NativePosition{30, 25},
        psx::render::NativePosition{15, 40},
    };
    std::array<psx::render::VertexCandidate, 3> candidates{};
    for (std::size_t index = 0U; index < native.size(); ++index) {
        const auto gte = provenance.write_gte(
            static_cast<psx::render::GteRegister>(index), native[index],
            psx::render::PreciseCoordinate{
                static_cast<double>(native[index].x) + 0.25,
                static_cast<double>(native[index].y) + 0.5,
                3.0 + static_cast<double>(index), 2.0});
        const std::uint32_t raw =
            static_cast<std::uint16_t>(native[index].x) |
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(native[index].y)) << 16U);
        expect(provenance.store_memory(
                   static_cast<std::uint32_t>(0x100U + index * 4U), raw,
                   gte.candidate().enhancement)
                   .carried_precise(),
               "GTE precision survives RAM store");
        candidates[index] = provenance.load_memory(
            static_cast<std::uint32_t>(0x100U + index * 4U))->candidate();
    }

    const auto baseline = psx::render::ProjectionPolicyPlanner::plan({});
    std::array<clean_aot::host::RenderVertex, 3> vertices{};
    std::array<std::uint16_t, 3> indices{};
    const auto built = clean_aot::host::build_gpu_polygon_geometry(
        std::get<clean_aot::gpu::PolygonEvent>(*event), candidates, &provenance,
        baseline.plan, vertices, indices);
    expect(built.status.success() && vertices[0].x == 10.25F &&
               vertices[0].y == 20.5F && vertices[0].storage_generation != 0U &&
               vertices[0].origin_generation != 0U,
           "GTE/RAM/GPU packet provenance reaches host geometry");
    expect(built.visual_digest == 0x49cebe6818887fe1ULL,
           "GTE/RAM/GPU/host visual digest changed");
}

void test_4k_scanout_frame_binding() {
    clean_aot::gpu::DisplayTiming display{};
    display.display_enabled = true;
    display.display_start_x = 0U;
    display.display_start_y = 0U;
    display.horizontal_start = 0x200U;
    display.horizontal_end = 0xc00U;
    display.vertical_start = 0x10U;
    display.vertical_end = 0x100U;
    display.horizontal_resolution = 1U;
    const auto plan = clean_aot::raster::ScanoutMapper::plan({
        display, clean_aot::raster::ScanoutField::Progressive,
        3840U, 2160U, clean_aot::raster::PresentationMode::Identity4x3, 4U, 3U});
    expect(plan.accepted && plan.plan.presentation.destination_x == 480U &&
               plan.plan.presentation.destination_width == 2880U &&
               plan.plan.presentation.destination_height == 2160U,
           "4K scanout preserves exact centered 4:3 presentation");
    std::vector<clean_aot::raster::VramWord> vram(
        clean_aot::raster::kVramWordCount, 0U);
    vram[0] = 0x001fU;
    std::vector<std::uint8_t> rgb(plan.plan.required_rgb_bytes, 0U);
    expect(clean_aot::raster::ScanoutMapper::decode_rgb888(plan.plan, vram, rgb)
               .accepted(),
           "scanout decoder emits the host RGB frame");
    FrameSubmission submission{};
    expect(clean_aot::host::bind_scanout_frame(
               plan.plan, rgb, 1U, 1U, submission).success(),
           "scanout output binds to a host frame submission");
    expect(submission.surface.format == PixelFormat::rgb8_unorm &&
               submission.surface.pitch == 960U &&
               submission.surface.byte_size == 320U * 240U * 3U &&
               submission.surface.pixels[0] == 255U,
           "host frame retains scanout format, bounds, and first pixel");
}

} // namespace

int main() {
    test_pacing();
    test_headless_frame_schedules();
    test_bounded_queue_and_audio();
    test_viewports();
    test_precise_geometry_bridge_visual_digest();
    test_gte_memory_gpu_host_provenance_chain();
    test_4k_scanout_frame_binding();
    if (failures != 0) {
        std::cerr << failures << " host tests failed\n";
        return 1;
    }
    std::cout << "clean_aot_host_tests: all checks passed\n";
    return 0;
}
