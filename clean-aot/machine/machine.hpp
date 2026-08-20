#ifndef CLEAN_AOT_MACHINE_HPP
#define CLEAN_AOT_MACHINE_HPP

#include "machine_c.h"

#include <cstdint>
#include <limits>

namespace clean_aot::machine {

enum class VBlankRate : std::uint32_t {
    Hz25 = CLEAN_AOT_MACHINE_VBLANK_25,
    Hz50 = CLEAN_AOT_MACHINE_VBLANK_50,
    Hz100 = CLEAN_AOT_MACHINE_VBLANK_100,
};

enum class EventKind : std::uint32_t {
    ModeSwitch = CLEAN_AOT_MACHINE_EVENT_MODE_SWITCH,
    VBlank = CLEAN_AOT_MACHINE_EVENT_VBLANK,
    CpuCycles = CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES,
    SpuSamples = CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES,
    CddaSectors = CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS,
};

using Event = clean_aot_machine_event;
using FrameReceipt = clean_aot_machine_frame_receipt;

/* The callback must not throw, retain the event pointer, or re-enter Machine. */
using EventSink = void (*)(const Event& event, void* user);

class Machine final {
public:
    static constexpr std::uint64_t CpuHz = 33'868'800;
    static constexpr std::uint64_t SpuHz = 44'100;
    static constexpr std::uint64_t CddaSectorsPerSecond = 75;
    static constexpr std::uint64_t TimebaseHz = 100;
    static constexpr std::uint64_t MaxTimebaseTicksPerFrame = 4;

    explicit Machine(VBlankRate initial_rate = VBlankRate::Hz100);

    bool set_vblank_rate(VBlankRate rate);
    VBlankRate vblank_rate() const noexcept;

    /* Emits a frame-start VBlank then its domain budgets. No host pacing. */
    FrameReceipt step_frame(EventSink sink = nullptr, void* user = nullptr);

    void reset() noexcept;
    std::uint64_t frame_count() const noexcept;
    std::uint64_t receipt_digest() const noexcept;

private:
    struct RationalAccumulator {
        std::uint64_t frequency_hz;
        std::uint64_t denominator_hz;
        std::uint64_t remainder;
        std::uint64_t total;

        RationalAccumulator(std::uint64_t frequency,
                            std::uint64_t denominator) noexcept;
        std::uint64_t advance(std::uint64_t timebase_ticks) noexcept;
        void reset() noexcept;
    };

    static std::uint32_t rate_value(VBlankRate rate) noexcept;
    static bool supported_rate(std::uint32_t rate) noexcept;
    static std::uint32_t timebase_ticks_for_rate(std::uint32_t rate) noexcept;

    void emit(Event event, EventSink sink, void* user) noexcept;

    const std::uint32_t initial_rate_hz_;
    std::uint32_t current_rate_hz_;
    bool pending_mode_switch_;
    std::uint32_t pending_mode_switch_from_hz_;
    std::uint64_t frame_count_;
    std::uint64_t elapsed_timebase_ticks_;
    std::uint64_t next_event_sequence_;
    std::uint64_t digest_;
    RationalAccumulator cpu_clock_;
    RationalAccumulator spu_clock_;
    RationalAccumulator cdda_clock_;
};

static_assert(Machine::CpuHz % Machine::TimebaseHz == 0,
              "CPU clock must be integral at the machine timebase");
static_assert(Machine::SpuHz % Machine::TimebaseHz == 0,
              "SPU clock must be integral at the machine timebase");
static_assert(Machine::CddaSectorsPerSecond < Machine::TimebaseHz,
              "CDDA must retain a fractional accumulator at 100 Hz");
static_assert(
    Machine::CpuHz <=
        (std::numeric_limits<std::uint64_t>::max() -
         (Machine::TimebaseHz - 1)) /
            Machine::MaxTimebaseTicksPerFrame,
    "CPU per-frame rational numerator must not overflow");
static_assert(
    Machine::SpuHz <=
        (std::numeric_limits<std::uint64_t>::max() -
         (Machine::TimebaseHz - 1)) /
            Machine::MaxTimebaseTicksPerFrame,
    "SPU per-frame rational numerator must not overflow");
static_assert(
    Machine::CddaSectorsPerSecond <=
        (std::numeric_limits<std::uint64_t>::max() -
         (Machine::TimebaseHz - 1)) /
            Machine::MaxTimebaseTicksPerFrame,
    "CDDA per-frame rational numerator must not overflow");

} // namespace clean_aot::machine

#endif // CLEAN_AOT_MACHINE_HPP
