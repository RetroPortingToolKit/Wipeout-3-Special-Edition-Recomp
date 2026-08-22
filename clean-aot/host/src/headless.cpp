#include "clean_aot/host/headless.hpp"

#include <limits>

namespace clean_aot::host {
namespace {

[[nodiscard]] std::uint64_t fnv1a(
    std::uint64_t digest,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    constexpr std::uint64_t kPrime = 1'099'511'628'211ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        digest ^= bytes[index];
        digest *= kPrime;
    }
    return digest;
}

[[nodiscard]] bool add_fits(
    std::uint64_t left,
    std::uint64_t right) noexcept {
    return right <= std::numeric_limits<std::uint64_t>::max() - left;
}

} // namespace

bool HeadlessBackend::valid_frame(const FrameSubmission& frame) noexcept {
    const std::uint32_t bytes = bytes_per_pixel(frame.surface.format);
    if (frame.surface.pixels == nullptr || frame.surface.byte_size == 0U ||
        frame.surface.width == 0U || frame.surface.height == 0U ||
        bytes == 0U) {
        return false;
    }
    const std::uint64_t minimum_pitch =
        static_cast<std::uint64_t>(frame.surface.width) * bytes;
    const std::uint64_t prior_rows =
        static_cast<std::uint64_t>(frame.surface.height) - 1U;
    if (prior_rows != 0U &&
        frame.surface.pitch >
            (std::numeric_limits<std::uint64_t>::max() - minimum_pitch) /
                prior_rows) {
        return false;
    }
    const std::uint64_t required_bytes =
        static_cast<std::uint64_t>(frame.surface.pitch) *
        prior_rows + minimum_pitch;
    return frame.surface.pitch >= minimum_pitch &&
        required_bytes <= frame.surface.byte_size;
}

bool HeadlessBackend::valid_audio(const StereoAudioBatch& audio) noexcept {
    return audio.interleaved_stereo != nullptr &&
        audio.sample_frames == kAudioFramesPerUpdate;
}

HostStatus HeadlessBackend::submit_frame(
    const FrameSubmission& frame) noexcept {
    if (shutting_down_) {
        return HostStatus{HostCode::shutting_down};
    }
    if (!valid_frame(frame)) {
        return HostStatus{HostCode::invalid_argument};
    }
    if (!frames_.try_push(frame)) {
        ++stats_.queue_full_errors;
        return HostStatus{HostCode::queue_full};
    }
    ++stats_.frames_submitted;
    return HostStatus{};
}

HostStatus HeadlessBackend::submit_audio(
    const StereoAudioBatch& audio) noexcept {
    if (shutting_down_) {
        return HostStatus{HostCode::shutting_down};
    }
    if (!valid_audio(audio)) {
        return HostStatus{HostCode::invalid_argument};
    }
    if (!audio_.try_push(audio)) {
        ++stats_.queue_full_errors;
        return HostStatus{HostCode::queue_full};
    }
    ++stats_.audio_batches_submitted;
    return HostStatus{};
}

HostStatus HeadlessBackend::submit_controller(
    const ControllerState& state) noexcept {
    if (shutting_down_) {
        return HostStatus{HostCode::shutting_down};
    }
    if (!controllers_.try_push(state)) {
        ++stats_.queue_full_errors;
        return HostStatus{HostCode::queue_full};
    }
    return HostStatus{};
}

HostStatus HeadlessBackend::request_window(
    const WindowRequest& request) noexcept {
    if (shutting_down_) {
        return HostStatus{HostCode::shutting_down};
    }
    if (request.state.pixel_width == 0U || request.state.pixel_height == 0U) {
        return HostStatus{HostCode::invalid_argument};
    }
    const HostEvent event{
        request.state.fullscreen == FullscreenMode::windowed
            ? HostEventKind::window_resized
            : HostEventKind::fullscreen_changed,
        request.state,
        HostCode::ok};
    if (!events_.try_push(event)) {
        ++stats_.queue_full_errors;
        return HostStatus{HostCode::queue_full};
    }
    stats_.window = request.state;
    return HostStatus{};
}

HostStatus HeadlessBackend::service_frame() noexcept {
    FrameSubmission frame{};
    if (!frames_.try_pop(frame)) {
        return HostStatus{HostCode::not_ready};
    }
    if (frame.sequence != next_frame_sequence_) {
        ++stats_.sequence_errors;
        next_frame_sequence_ = frame.sequence;
        const HostStatus result{HostCode::sequence_error};
        ++next_frame_sequence_;
        return result;
    }

    std::uint64_t digest = stats_.frame_digest;
    const std::size_t visible_row_bytes = static_cast<std::size_t>(
        frame.surface.width) * bytes_per_pixel(frame.surface.format);
    for (std::uint32_t row = 0U; row < frame.surface.height; ++row) {
        const std::uint8_t* row_start = frame.surface.pixels +
            static_cast<std::size_t>(row) * frame.surface.pitch;
        digest = fnv1a(digest, row_start, visible_row_bytes);
    }
    stats_.frame_digest = digest;
    ++stats_.frames_presented;
    ++next_frame_sequence_;
    return HostStatus{};
}

HostStatus HeadlessBackend::service_audio() noexcept {
    StereoAudioBatch audio{};
    if (!audio_.try_pop(audio)) {
        return HostStatus{HostCode::not_ready};
    }
    if (audio.update_index != next_audio_update_) {
        ++stats_.sequence_errors;
        next_audio_update_ = audio.update_index;
        ++next_audio_update_;
        return HostStatus{HostCode::sequence_error};
    }

    const std::size_t byte_count = static_cast<std::size_t>(
        audio.sample_frames) * kAudioChannels * sizeof(std::int16_t);
    stats_.audio_digest = fnv1a(
        stats_.audio_digest,
        reinterpret_cast<const std::uint8_t*>(audio.interleaved_stereo),
        byte_count);
    if (!add_fits(stats_.audio_sample_frames, audio.sample_frames)) {
        return HostStatus{HostCode::internal_error};
    }
    stats_.audio_sample_frames += audio.sample_frames;
    ++stats_.audio_batches_consumed;
    ++next_audio_update_;
    return HostStatus{};
}

HostStatus HeadlessBackend::service_controller() noexcept {
    ControllerState state{};
    if (!controllers_.try_pop(state)) {
        return HostStatus{HostCode::not_ready};
    }
    stats_.controller = state;
    return HostStatus{};
}

HostStatus HeadlessBackend::service() noexcept {
    if (shutting_down_) {
        return HostStatus{HostCode::shutting_down};
    }
    HostStatus result{HostCode::not_ready};
    const HostStatus frame_result = service_frame();
    if (frame_result.code != HostCode::not_ready) {
        result = frame_result;
    }
    const HostStatus audio_result = service_audio();
    if (audio_result.code != HostCode::not_ready &&
        result.code == HostCode::not_ready) {
        result = audio_result;
    }
    const HostStatus controller_result = service_controller();
    if (controller_result.code != HostCode::not_ready &&
        result.code == HostCode::not_ready) {
        result = controller_result;
    }
    return result;
}

bool HeadlessBackend::poll_event(HostEvent& event) noexcept {
    return events_.try_pop(event);
}

HostStatus HeadlessBackend::upload_replacement(
    const ReplacementImage& replacement,
    std::uint64_t& handle) noexcept {
    handle = 0U;
    const std::uint64_t required = static_cast<std::uint64_t>(replacement.width) *
        replacement.height * 4U;
    if (shutting_down_ || replacement.width == 0U || replacement.height == 0U ||
        required != replacement.rgba8.size()) {
        return HostStatus{shutting_down_ ? HostCode::shutting_down
                                        : HostCode::invalid_argument};
    }
    stats_.replacement_digest = fnv1a(stats_.replacement_digest,
        replacement.rgba8.data(), replacement.rgba8.size());
    ++stats_.replacements_uploaded;
    handle = stats_.replacements_uploaded;
    return HostStatus{};
}

void HeadlessBackend::shutdown() noexcept {
    if (shutting_down_) {
        return;
    }
    shutting_down_ = true;
    frames_.clear();
    audio_.clear();
    controllers_.clear();
    events_.clear();
}

} // namespace clean_aot::host
