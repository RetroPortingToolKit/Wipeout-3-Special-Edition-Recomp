#include "machine.hpp"

#include <new>
#include <stdexcept>
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
                  const clean_aot_machine_event& event) noexcept {
    digest_u64(digest, event.sequence);
    digest_u64(digest, event.frame_index);
    digest_u32(digest, event.kind);
    digest_u32(digest, event.vblank_hz);
    digest_u64(digest, event.amount);
    digest_u64(digest, event.cumulative);
    digest_u64(digest, event.auxiliary);
    digest_u64(digest, event.cpu_multiplier_numerator);
    digest_u64(digest, event.cpu_multiplier_denominator);
    digest_u64(digest, event.cpu_retirement_total);
}

bool add_rational_parts(std::uint64_t denominator,
                        std::uint64_t right_whole,
                        std::uint64_t right_remainder,
                        std::uint64_t& left_whole,
                        std::uint64_t& left_remainder) noexcept {
    if (denominator == 0 || left_remainder >= denominator ||
        right_remainder >= denominator) {
        return false;
    }
    if (right_whole >
        std::numeric_limits<std::uint64_t>::max() - left_whole) {
        return false;
    }

    std::uint64_t sum_whole = left_whole + right_whole;
    std::uint64_t sum_remainder = 0;
    const std::uint64_t distance_to_denominator =
        denominator - right_remainder;
    if (left_remainder >= distance_to_denominator) {
        if (sum_whole == std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        ++sum_whole;
        sum_remainder = left_remainder - distance_to_denominator;
    } else {
        /* This addition is bounded by denominator - 1. */
        sum_remainder = left_remainder + right_remainder;
    }

    left_whole = sum_whole;
    left_remainder = sum_remainder;
    return true;
}

void forward_c_callback(const clean_aot_machine_event& event, void* context) {
    auto* callback_context =
        static_cast<std::pair<clean_aot_machine_event_callback, void*>*>(
            context);
    callback_context->first(&event, callback_context->second);
}

} // namespace

namespace clean_aot::machine {

Machine::RationalAccumulator::RationalAccumulator(
    std::uint64_t numerator,
    std::uint64_t denominator) noexcept
    : rate_numerator(numerator),
      rate_denominator(denominator),
      remainder(0),
      total(0) {}

bool Machine::RationalAccumulator::preview(
    std::uint64_t units,
    AdvanceResult& result) const noexcept {
    if (rate_denominator == 0 || remainder >= rate_denominator) {
        return false;
    }

    /*
     * Accumulate units * rate_numerator / rate_denominator without ever
     * forming the potentially overflowing numerator product. Each pair is an
     * exact whole/remainder value with a shared denominator. Binary doubling
     * makes the work logarithmic in units and remains portable to MSVC, where
     * unsigned __int128 is unavailable.
     */
    std::uint64_t accumulated_whole = 0;
    std::uint64_t accumulated_remainder = remainder;
    std::uint64_t term_whole = rate_numerator / rate_denominator;
    std::uint64_t term_remainder = rate_numerator % rate_denominator;
    std::uint64_t remaining_units = units;

    while (remaining_units != 0) {
        if ((remaining_units & 1U) != 0 &&
            !add_rational_parts(rate_denominator, term_whole, term_remainder,
                                accumulated_whole,
                                accumulated_remainder)) {
            return false;
        }
        remaining_units >>= 1U;
        if (remaining_units != 0) {
            const std::uint64_t prior_whole = term_whole;
            const std::uint64_t prior_remainder = term_remainder;
            if (!add_rational_parts(rate_denominator, prior_whole,
                                    prior_remainder, term_whole,
                                    term_remainder)) {
                return false;
            }
        }
    }

    if (accumulated_whole >
        std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    result = AdvanceResult{accumulated_whole, accumulated_remainder,
                           total + accumulated_whole};
    return true;
}

void Machine::RationalAccumulator::commit(const AdvanceResult& result) noexcept {
    remainder = result.remainder;
    total = result.total;
}

void Machine::RationalAccumulator::reset() noexcept {
    remainder = 0;
    total = 0;
}

std::uint32_t Machine::rate_value(VBlankRate rate) noexcept {
    return static_cast<std::uint32_t>(rate);
}

bool Machine::supported_rate(std::uint32_t rate) noexcept {
    return rate == CLEAN_AOT_MACHINE_VBLANK_25 ||
           rate == CLEAN_AOT_MACHINE_VBLANK_50 ||
           rate == CLEAN_AOT_MACHINE_VBLANK_100;
}

std::uint32_t Machine::timebase_ticks_for_rate(
    std::uint32_t rate) noexcept {
    if (!supported_rate(rate)) {
        return 0;
    }
    return static_cast<std::uint32_t>(TimebaseHz / rate);
}

bool Machine::canonicalize_cpu_multiplier(
    std::uint64_t input_numerator,
    std::uint64_t input_denominator,
    CpuMultiplier& canonical) noexcept {
    if (input_numerator == 0 || input_denominator == 0) {
        return false;
    }

    std::uint64_t numerator = input_numerator;
    std::uint64_t denominator = input_denominator;
    while (denominator != 0) {
        const std::uint64_t remainder = numerator % denominator;
        numerator = denominator;
        denominator = remainder;
    }
    const std::uint64_t divisor = numerator;
    numerator = input_numerator / divisor;
    denominator = input_denominator / divisor;

    const std::uint64_t whole = numerator / denominator;
    const std::uint64_t fractional = numerator % denominator;
    if (whole > MaxCpuMultiplierWhole ||
        (whole == MaxCpuMultiplierWhole && fractional != 0)) {
        return false;
    }

    canonical = CpuMultiplier{numerator, denominator};
    return true;
}

Machine::CpuMultiplier Machine::checked_cpu_multiplier(
    std::uint64_t input_numerator,
    std::uint64_t input_denominator) {
    CpuMultiplier canonical{};
    if (!canonicalize_cpu_multiplier(input_numerator, input_denominator,
                                     canonical)) {
        throw std::invalid_argument("invalid CPU multiplier");
    }
    return canonical;
}

Machine::Machine(VBlankRate initial_rate)
    : Machine(initial_rate, CpuMultiplier{1, 1}) {}

Machine::Machine(VBlankRate initial_rate,
                 std::uint64_t cpu_multiplier_numerator,
                 std::uint64_t cpu_multiplier_denominator)
    : Machine(initial_rate,
              CpuMultiplier{cpu_multiplier_numerator,
                            cpu_multiplier_denominator}) {}

Machine::Machine(VBlankRate initial_rate, CpuMultiplier requested_multiplier)
    : initial_rate_hz_(supported_rate(rate_value(initial_rate))
                           ? rate_value(initial_rate)
                           : CLEAN_AOT_MACHINE_VBLANK_100),
      cpu_multiplier_(checked_cpu_multiplier(requested_multiplier.numerator,
                                             requested_multiplier.denominator)),
      current_rate_hz_(initial_rate_hz_),
      pending_mode_switch_(false),
      pending_mode_switch_from_hz_(0),
      frame_count_(0),
      elapsed_timebase_ticks_(0),
      next_event_sequence_(0),
      digest_(kFnvOffsetBasis),
      cpu_clock_(cpu_multiplier_.numerator,
                 cpu_multiplier_.denominator),
      spu_clock_(SpuHz, TimebaseHz),
      cdda_clock_(CddaSectorsPerSecond, TimebaseHz) {}

Machine::CpuMultiplier Machine::cpu_multiplier() const noexcept {
    return cpu_multiplier_;
}

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
    event.cpu_multiplier_numerator = cpu_multiplier_.numerator;
    event.cpu_multiplier_denominator = cpu_multiplier_.denominator;
    event.cpu_retirement_total = cpu_clock_.total;
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
    const std::uint64_t event_count = pending_mode_switch_ ? 5 : 4;

    RationalAccumulator::AdvanceResult cpu_preview{};
    RationalAccumulator::AdvanceResult spu_preview{};
    RationalAccumulator::AdvanceResult cdda_preview{};
    const std::uint64_t stock_cpu_cycles =
        CpuCyclesPerTimebaseTick * timebase_ticks;
    const bool counters_fit =
        cpu_clock_.preview(stock_cpu_cycles, cpu_preview) &&
        spu_clock_.preview(timebase_ticks, spu_preview) &&
        cdda_clock_.preview(timebase_ticks, cdda_preview);
    if (!counters_fit || frame_count_ == std::numeric_limits<std::uint64_t>::max() ||
        elapsed_timebase_ticks_ >
            std::numeric_limits<std::uint64_t>::max() - timebase_ticks ||
        next_event_sequence_ >
            std::numeric_limits<std::uint64_t>::max() - event_count) {
        throw std::overflow_error("machine timing counter overflow");
    }

    FrameReceipt receipt{};
    receipt.frame_index = frame_index;
    receipt.vblank_hz = current_rate_hz_;
    receipt.timebase_ticks = timebase_ticks;
    receipt.event_sequence_begin = next_event_sequence_;

    if (pending_mode_switch_) {
        Event mode_event{};
        mode_event.frame_index = frame_index;
        mode_event.kind = CLEAN_AOT_MACHINE_EVENT_MODE_SWITCH;
        mode_event.vblank_hz = current_rate_hz_;
        mode_event.amount = current_rate_hz_;
        mode_event.cumulative = frame_count_;
        mode_event.auxiliary = pending_mode_switch_from_hz_;
        emit(mode_event, sink, user);
        pending_mode_switch_ = false;
        pending_mode_switch_from_hz_ = 0;
    }

    const std::uint64_t cpu_cycles = cpu_preview.amount;
    const std::uint64_t spu_samples = spu_preview.amount;
    const std::uint64_t cdda_sectors = cdda_preview.amount;

    ++frame_count_;
    elapsed_timebase_ticks_ += timebase_ticks;

    Event vblank_event{};
    vblank_event.frame_index = frame_index;
    vblank_event.kind = CLEAN_AOT_MACHINE_EVENT_VBLANK;
    vblank_event.vblank_hz = current_rate_hz_;
    vblank_event.amount = 1;
    vblank_event.cumulative = frame_count_;
    vblank_event.auxiliary = timebase_ticks;
    emit(vblank_event, sink, user);

    cpu_clock_.commit(cpu_preview);
    Event cpu_event{};
    cpu_event.frame_index = frame_index;
    cpu_event.kind = CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES;
    cpu_event.vblank_hz = current_rate_hz_;
    cpu_event.amount = cpu_cycles;
    cpu_event.cumulative = cpu_clock_.total;
    emit(cpu_event, sink, user);

    spu_clock_.commit(spu_preview);
    Event spu_event{};
    spu_event.frame_index = frame_index;
    spu_event.kind = CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES;
    spu_event.vblank_hz = current_rate_hz_;
    spu_event.amount = spu_samples;
    spu_event.cumulative = spu_clock_.total;
    emit(spu_event, sink, user);

    cdda_clock_.commit(cdda_preview);
    Event cdda_event{};
    cdda_event.frame_index = frame_index;
    cdda_event.kind = CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS;
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
    receipt.cpu_multiplier_numerator = cpu_multiplier_.numerator;
    receipt.cpu_multiplier_denominator = cpu_multiplier_.denominator;
    receipt.cpu_retirement_total = cpu_clock_.total;
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

} // namespace clean_aot::machine

struct clean_aot_machine {
    explicit clean_aot_machine(std::uint32_t rate)
        : impl(static_cast<clean_aot::machine::VBlankRate>(rate)) {}

    clean_aot_machine(std::uint32_t rate,
                      std::uint64_t cpu_multiplier_numerator,
                      std::uint64_t cpu_multiplier_denominator)
        : impl(static_cast<clean_aot::machine::VBlankRate>(rate),
               cpu_multiplier_numerator,
               cpu_multiplier_denominator) {}

    clean_aot::machine::Machine impl;
};

extern "C" {

clean_aot_machine* clean_aot_machine_create(
    std::uint32_t initial_vblank_hz) {
    if (initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_25 &&
        initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_50 &&
        initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_100) {
        return nullptr;
    }
    try {
        return new clean_aot_machine(initial_vblank_hz);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

clean_aot_machine* clean_aot_machine_create_with_cpu_multiplier(
    std::uint32_t initial_vblank_hz,
    std::uint64_t cpu_multiplier_numerator,
    std::uint64_t cpu_multiplier_denominator) {
    if (initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_25 &&
        initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_50 &&
        initial_vblank_hz != CLEAN_AOT_MACHINE_VBLANK_100) {
        return nullptr;
    }
    clean_aot::machine::Machine::CpuMultiplier canonical{};
    if (!clean_aot::machine::Machine::canonicalize_cpu_multiplier(
            cpu_multiplier_numerator, cpu_multiplier_denominator,
            canonical)) {
        return nullptr;
    }
    try {
        return new clean_aot_machine(initial_vblank_hz, canonical.numerator,
                                     canonical.denominator);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void clean_aot_machine_destroy(clean_aot_machine* machine) {
    delete machine;
}

clean_aot_machine_status clean_aot_machine_canonicalize_cpu_multiplier(
    std::uint64_t input_numerator,
    std::uint64_t input_denominator,
    std::uint64_t* canonical_numerator,
    std::uint64_t* canonical_denominator) {
    if (canonical_numerator == nullptr || canonical_denominator == nullptr ||
        canonical_numerator == canonical_denominator) {
        return CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    *canonical_numerator = 0;
    *canonical_denominator = 0;
    clean_aot::machine::Machine::CpuMultiplier canonical{};
    if (!clean_aot::machine::Machine::canonicalize_cpu_multiplier(
            input_numerator, input_denominator, canonical)) {
        return CLEAN_AOT_MACHINE_STATUS_INVALID_CPU_MULTIPLIER;
    }
    *canonical_numerator = canonical.numerator;
    *canonical_denominator = canonical.denominator;
    return CLEAN_AOT_MACHINE_STATUS_OK;
}

clean_aot_machine_status clean_aot_machine_get_cpu_multiplier(
    const clean_aot_machine* machine,
    std::uint64_t* numerator,
    std::uint64_t* denominator) {
    if (machine == nullptr || numerator == nullptr || denominator == nullptr ||
        numerator == denominator) {
        return CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    const clean_aot::machine::Machine::CpuMultiplier multiplier =
        machine->impl.cpu_multiplier();
    *numerator = multiplier.numerator;
    *denominator = multiplier.denominator;
    return CLEAN_AOT_MACHINE_STATUS_OK;
}

clean_aot_machine_status clean_aot_machine_set_vblank_rate(
    clean_aot_machine* machine,
    std::uint32_t vblank_hz) {
    if (machine == nullptr) {
        return CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    const bool accepted = machine->impl.set_vblank_rate(
        static_cast<clean_aot::machine::VBlankRate>(vblank_hz));
    return accepted ? CLEAN_AOT_MACHINE_STATUS_OK
                    : CLEAN_AOT_MACHINE_STATUS_INVALID_RATE;
}

std::uint32_t clean_aot_machine_get_vblank_rate(
    const clean_aot_machine* machine) {
    if (machine == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(machine->impl.vblank_rate());
}

clean_aot_machine_status clean_aot_machine_step_frame(
    clean_aot_machine* machine,
    clean_aot_machine_event_callback callback,
    void* user,
    clean_aot_machine_frame_receipt* receipt) {
    if (machine == nullptr || receipt == nullptr) {
        return CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT;
    }
    if (callback == nullptr) {
        try {
            *receipt = machine->impl.step_frame();
            return CLEAN_AOT_MACHINE_STATUS_OK;
        } catch (const std::overflow_error&) {
            return CLEAN_AOT_MACHINE_STATUS_OVERFLOW;
        }
    }
    std::pair<clean_aot_machine_event_callback, void*> callback_context{
        callback, user};
    try {
        *receipt = machine->impl.step_frame(forward_c_callback, &callback_context);
        return CLEAN_AOT_MACHINE_STATUS_OK;
    } catch (const std::overflow_error&) {
        return CLEAN_AOT_MACHINE_STATUS_OVERFLOW;
    }
}

void clean_aot_machine_reset(clean_aot_machine* machine) {
    if (machine != nullptr) {
        machine->impl.reset();
    }
}

std::uint64_t clean_aot_machine_frame_count(
    const clean_aot_machine* machine) {
    return machine == nullptr ? 0 : machine->impl.frame_count();
}

std::uint64_t clean_aot_machine_receipt_digest(
    const clean_aot_machine* machine) {
    return machine == nullptr ? 0 : machine->impl.receipt_digest();
}

} // extern "C"
