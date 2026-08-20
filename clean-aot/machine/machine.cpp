#include "machine.hpp"

#include <new>
#include <utility>

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

void digest_byte(std::uint64_t& digest, std::uint8_t byte) noexcept {
    digest ^= byte;
    digest *= kFnvPrime;
}

void digest_u32(std::uint64_t& digest, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        digest_byte(digest, static_cast<std::uint8_t>(value >> shift));
    }
}

void digest_u64(std::uint64_t& digest, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        digest_byte(digest, static_cast<std::uint8_t>(value >> shift));
    }
}

void digest_event(std::uint64_t& digest,
                  const w3_machine_event& event) noexcept {
    digest_u64(digest, event.sequence);
    digest_u64(digest, event.frame_index);
    digest_u32(digest, event.kind);
    digest_u32(digest, event.vblank_hz);
    digest_u64(digest, event.amount);
    digest_u64(digest, event.cumulative);
    digest_u64(digest, event.auxiliary);
}

void forward_c_callback(const w3_machine_event& event, void* context) {
    auto* callback_context =
        static_cast<std::pair<w3_machine_event_callback, void*>*>(context);
    callback_context->first(&event, callback_context->second);
}

} // namespace

namespace w3::machine {

Machine::RationalAccumulator::RationalAccumulator(
    std::uint64_t frequency,
    std::uint64_t denominator) noexcept
    : frequency_hz(frequency),
      denominator_hz(denominator),
      remainder(0),
      total(0) {}

std::uint64_t Machine::RationalAccumulator::advance(
    std::uint64_t timebase_ticks) noexcept {
    /* The configured frequencies and timebase make this product bounded well
     * inside uint64_t. Keeping the remainder separate makes the result exact
     * and makes changing VBlank denominators lossless. */
    const std::uint64_t numerator =
        frequency_hz * timebase_ticks + remainder;
    const std::uint64_t whole = numerator / denominator_hz;
    remainder = numerator % denominator_hz;
    total += whole;
    return whole;
}

void Machine::RationalAccumulator::reset() noexcept {
    remainder = 0;
    total = 0;
}

std::uint32_t Machine::rate_value(VBlankRate rate) noexcept {
    return static_cast<std::uint32_t>(rate);
}

bool Machine::supported_rate(std::uint32_t rate) noexcept {
    return rate == W3_MACHINE_VBLANK_25 ||
           rate == W3_MACHINE_VBLANK_50 ||
           rate == W3_MACHINE_VBLANK_100;
}

std::uint32_t Machine::timebase_ticks_for_rate(
    std::uint32_t rate) noexcept {
    if (!supported_rate(rate)) {
        return 0;
    }
    return static_cast<std::uint32_t>(TimebaseHz / rate);
}

Machine::Machine(VBlankRate initial_rate)
    : initial_rate_hz_(supported_rate(rate_value(initial_rate))
                           ? rate_value(initial_rate)
                           : W3_MACHINE_VBLANK_100),
      current_rate_hz_(initial_rate_hz_),
      pending_mode_switch_(false),
      pending_mode_switch_from_hz_(0),
      frame_count_(0),
      elapsed_timebase_ticks_(0),
      next_event_sequence_(0),
      digest_(kFnvOffsetBasis),
      cpu_clock_(CpuHz, TimebaseHz),
      spu_clock_(SpuHz, TimebaseHz),
      cdda_clock_(CddaSectorsPerSecond, TimebaseHz) {}

bool Machine::set_vblank_rate(VBlankRate rate) {
    const std::uint32_t new_rate = rate_value(rate);
    if (!supported_rate(new_rate)) {
        return false;
    }
    if (new_rate == current_rate_hz_) {
        return true;
    }
    if (pending_mode_switch_ && new_rate == pending_mode_switch_from_hz_) {
        /* The caller changed its mind before the next VBlank. No machine
         * boundary observed the transient mode, so collapse the pending
         * transition rather than emitting a false old==new event. */
        current_rate_hz_ = new_rate;
        pending_mode_switch_ = false;
        pending_mode_switch_from_hz_ = 0;
        return true;
    }
    if (!pending_mode_switch_) {
        pending_mode_switch_from_hz_ = current_rate_hz_;
        pending_mode_switch_ = true;
    }
    current_rate_hz_ = new_rate;
    return true;
}

VBlankRate Machine::vblank_rate() const noexcept {
    return static_cast<VBlankRate>(current_rate_hz_);
}

void Machine::emit(Event event, EventSink sink, void* user) noexcept {
    event.sequence = next_event_sequence_++;
    digest_event(digest_, event);
    if (sink != nullptr) {
        sink(event, user);
    }
}

FrameReceipt Machine::step_frame(EventSink sink, void* user) {
    const std::uint64_t frame_index = frame_count_;
    const std::uint32_t timebase_ticks =
        timebase_ticks_for_rate(current_rate_hz_);

    FrameReceipt receipt{};
    receipt.frame_index = frame_index;
    receipt.vblank_hz = current_rate_hz_;
    receipt.timebase_ticks = timebase_ticks;
    receipt.event_sequence_begin = next_event_sequence_;

    if (pending_mode_switch_) {
        Event mode_event{};
        mode_event.frame_index = frame_index;
        mode_event.kind = W3_MACHINE_EVENT_MODE_SWITCH;
        mode_event.vblank_hz = current_rate_hz_;
        mode_event.amount = current_rate_hz_;
        mode_event.cumulative = frame_count_;
        mode_event.auxiliary = pending_mode_switch_from_hz_;
        emit(mode_event, sink, user);
        pending_mode_switch_ = false;
        pending_mode_switch_from_hz_ = 0;
    }

    const std::uint64_t cpu_cycles = cpu_clock_.advance(timebase_ticks);
    const std::uint64_t spu_samples = spu_clock_.advance(timebase_ticks);
    const std::uint64_t cdda_sectors = cdda_clock_.advance(timebase_ticks);

    ++frame_count_;
    elapsed_timebase_ticks_ += timebase_ticks;

    Event vblank_event{};
    vblank_event.frame_index = frame_index;
    vblank_event.kind = W3_MACHINE_EVENT_VBLANK;
    vblank_event.vblank_hz = current_rate_hz_;
    vblank_event.amount = 1;
    vblank_event.cumulative = frame_count_;
    vblank_event.auxiliary = timebase_ticks;
    emit(vblank_event, sink, user);

    Event cpu_event{};
    cpu_event.frame_index = frame_index;
    cpu_event.kind = W3_MACHINE_EVENT_CPU_CYCLES;
    cpu_event.vblank_hz = current_rate_hz_;
    cpu_event.amount = cpu_cycles;
    cpu_event.cumulative = cpu_clock_.total;
    emit(cpu_event, sink, user);

    Event spu_event{};
    spu_event.frame_index = frame_index;
    spu_event.kind = W3_MACHINE_EVENT_SPU_SAMPLES;
    spu_event.vblank_hz = current_rate_hz_;
    spu_event.amount = spu_samples;
    spu_event.cumulative = spu_clock_.total;
    emit(spu_event, sink, user);

    Event cdda_event{};
    cdda_event.frame_index = frame_index;
    cdda_event.kind = W3_MACHINE_EVENT_CDDA_SECTORS;
    cdda_event.vblank_hz = current_rate_hz_;
    cdda_event.amount = cdda_sectors;
    cdda_event.cumulative = cdda_clock_.total;
    cdda_event.auxiliary = cdda_clock_.remainder;
    emit(cdda_event, sink, user);

    receipt.cpu_cycles = cpu_cycles;
    receipt.spu_samples = spu_samples;
    receipt.cdda_sectors = cdda_sectors;
    receipt.total_cpu_cycles = cpu_clock_.total;
    receipt.total_spu_samples = spu_clock_.total;
    receipt.total_cdda_sectors = cdda_clock_.total;
    receipt.cdda_phase = cdda_clock_.remainder;
    receipt.elapsed_timebase_ticks = elapsed_timebase_ticks_;
    receipt.event_sequence_end = next_event_sequence_ - 1;
    receipt.digest = digest_;
    return receipt;
}

void Machine::reset() noexcept {
    current_rate_hz_ = initial_rate_hz_;
    pending_mode_switch_ = false;
    pending_mode_switch_from_hz_ = 0;
    frame_count_ = 0;
    elapsed_timebase_ticks_ = 0;
    next_event_sequence_ = 0;
    digest_ = kFnvOffsetBasis;
    cpu_clock_.reset();
    spu_clock_.reset();
    cdda_clock_.reset();
}

std::uint64_t Machine::frame_count() const noexcept {
    return frame_count_;
}

std::uint64_t Machine::receipt_digest() const noexcept {
    return digest_;
}

} // namespace w3::machine

struct w3_machine {
    explicit w3_machine(std::uint32_t rate)
        : impl(static_cast<w3::machine::VBlankRate>(rate)) {}

    w3::machine::Machine impl;
};

extern "C" {

w3_machine* w3_machine_create(std::uint32_t initial_vblank_hz) {
    if (initial_vblank_hz != W3_MACHINE_VBLANK_25 &&
        initial_vblank_hz != W3_MACHINE_VBLANK_50 &&
        initial_vblank_hz != W3_MACHINE_VBLANK_100) {
        return nullptr;
    }
    try {
        return new w3_machine(initial_vblank_hz);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void w3_machine_destroy(w3_machine* machine) {
    delete machine;
}

w3_machine_status w3_machine_set_vblank_rate(w3_machine* machine,
                                               std::uint32_t vblank_hz) {
    if (machine == nullptr) {
        return W3_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    const bool accepted = machine->impl.set_vblank_rate(
        static_cast<w3::machine::VBlankRate>(vblank_hz));
    return accepted ? W3_MACHINE_STATUS_OK : W3_MACHINE_STATUS_INVALID_RATE;
}

std::uint32_t w3_machine_get_vblank_rate(const w3_machine* machine) {
    if (machine == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(machine->impl.vblank_rate());
}

w3_machine_status w3_machine_step_frame(w3_machine* machine,
                                         w3_machine_event_callback callback,
                                         void* user,
                                         w3_machine_frame_receipt* receipt) {
    if (machine == nullptr || receipt == nullptr) {
        return W3_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    if (callback == nullptr) {
        *receipt = machine->impl.step_frame();
        return W3_MACHINE_STATUS_OK;
    }
    std::pair<w3_machine_event_callback, void*> callback_context{callback,
                                                                  user};
    *receipt = machine->impl.step_frame(forward_c_callback, &callback_context);
    return W3_MACHINE_STATUS_OK;
}

void w3_machine_reset(w3_machine* machine) {
    if (machine != nullptr) {
        machine->impl.reset();
    }
}

std::uint64_t w3_machine_frame_count(const w3_machine* machine) {
    return machine == nullptr ? 0 : machine->impl.frame_count();
}

std::uint64_t w3_machine_receipt_digest(const w3_machine* machine) {
    return machine == nullptr ? 0 : machine->impl.receipt_digest();
}

} // extern "C"
