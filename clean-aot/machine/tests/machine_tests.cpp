#include "machine.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using clean_aot::machine::Event;
using clean_aot::machine::EventKind;
using clean_aot::machine::FrameReceipt;
using clean_aot::machine::Machine;
using clean_aot::machine::VBlankRate;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct EventLog {
    std::vector<Event> events;

    static void collect(const Event& event, void* user) {
        auto* log = static_cast<EventLog*>(user);
        log->events.push_back(event);
    }
};

std::uint32_t timebase_ticks(VBlankRate rate) {
    switch (rate) {
    case VBlankRate::Hz25:
        return 4;
    case VBlankRate::Hz50:
        return 2;
    case VBlankRate::Hz100:
        return 1;
    }
    return 0;
}

void test_100_hz_3600_frames() {
    Machine machine(VBlankRate::Hz100);
    EventLog log;
    std::uint64_t reference_timebase = 0;
    std::uint64_t reference_cdda_numerator = 0;
    std::uint64_t reference_cdda_total = 0;

    for (std::uint64_t frame = 0; frame < 3'600; ++frame) {
        const FrameReceipt receipt =
            machine.step_frame(EventLog::collect, &log);
        reference_timebase += 1;
        reference_cdda_numerator += 75;
        const std::uint64_t next_cdda_total =
            reference_cdda_numerator / 100;
        const std::uint64_t expected_cdda =
            next_cdda_total - reference_cdda_total;
        reference_cdda_total = next_cdda_total;

        require(receipt.frame_index == frame, "100 Hz frame index");
        require(receipt.vblank_hz == CLEAN_AOT_MACHINE_VBLANK_100,
                "100 Hz mode receipt");
        require(receipt.timebase_ticks == 1, "100 Hz timebase quantum");
        require(receipt.cpu_cycles == 338'688,
                "100 Hz exact CPU cycles per VBlank");
        require(receipt.cpu_multiplier_numerator == 1 &&
                    receipt.cpu_multiplier_denominator == 1,
                "stock CPU multiplier is explicit and canonical");
        require(receipt.spu_samples == 441,
                "100 Hz exact SPU samples per VBlank");
        require(receipt.cdda_sectors == expected_cdda,
                "100 Hz rational CDDA allocation");
        require(receipt.total_cpu_cycles == reference_timebase * 338'688,
                "100 Hz cumulative CPU cycles");
        require(receipt.cpu_retirement_total == receipt.total_cpu_cycles,
                "100 Hz CPU retirement total is explicit");
        require(receipt.total_spu_samples == reference_timebase * 441,
                "100 Hz cumulative SPU samples");
        require(receipt.total_cdda_sectors == reference_cdda_total,
                "100 Hz cumulative CDDA sectors");
        require(receipt.cdda_phase == reference_cdda_numerator % 100,
                "100 Hz CDDA phase");
        require(receipt.elapsed_timebase_ticks == reference_timebase,
                "100 Hz elapsed rational time");
        require(receipt.event_sequence_end - receipt.event_sequence_begin ==
                    3,
                "four deterministic domain events per frame");
    }

    require(reference_cdda_total == 2'700,
            "3,600 frames produce exactly 2,700 CDDA sectors");
    require(log.events.size() == 14'400,
            "100 Hz event count after 3,600 frames");
    for (std::size_t block = 0; block < 900; ++block) {
        const std::size_t first = block * 16;
        require(log.events[first + 0].kind == CLEAN_AOT_MACHINE_EVENT_VBLANK,
                "VBlank event order");
        require(log.events[first + 1].kind ==
                    CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES,
                "CPU event order");
        require(log.events[first + 2].kind ==
                    CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES,
                "SPU event order");
        require(log.events[first + 3].kind ==
                    CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS,
                "CDDA event order");
        const std::uint64_t cdda_block_total =
            log.events[first + 3].amount +
            log.events[first + 7].amount +
            log.events[first + 11].amount +
            log.events[first + 15].amount;
        require(cdda_block_total == 3,
                "exactly three CDDA sectors per four 100 Hz frames");
    }

    require(machine.frame_count() == 3'600,
            "machine frame count after long run");
    std::cout << "EVIDENCE 100HZ frames=" << machine.frame_count()
              << " cpu=" << reference_timebase * 338'688
              << " spu=" << reference_timebase * 441
              << " cdda=" << reference_cdda_total << " phase="
              << (reference_cdda_numerator % 100) << " digest=0x" << std::hex
              << machine.receipt_digest() << std::dec << '\n';
}

void test_fixed_modes() {
    Machine at_50(VBlankRate::Hz50);
    for (std::uint64_t frame = 0; frame < 4; ++frame) {
        const FrameReceipt receipt = at_50.step_frame();
        require(receipt.timebase_ticks == 2, "50 Hz timebase quantum");
        require(receipt.cpu_cycles == 677'376,
                "50 Hz exact CPU cycles per VBlank");
        require(receipt.cpu_multiplier_numerator == 1 &&
                    receipt.cpu_multiplier_denominator == 1,
                "50 Hz stock multiplier is explicit");
        require(receipt.spu_samples == 882,
                "50 Hz exact SPU samples per VBlank");
        require(receipt.cdda_sectors == (frame % 2 == 0 ? 1 : 2),
                "50 Hz rational CDDA allocation");
    }

    Machine at_25(VBlankRate::Hz25);
    const FrameReceipt receipt = at_25.step_frame();
    require(receipt.timebase_ticks == 4, "25 Hz timebase quantum");
    require(receipt.cpu_cycles == 1'354'752,
            "25 Hz exact CPU cycles per VBlank");
    require(receipt.spu_samples == 1'764,
            "25 Hz exact SPU samples per VBlank");
    require(receipt.cdda_sectors == 3,
            "25 Hz exact CDDA sectors per VBlank");
}

void test_nine_x_independent_clocks() {
    Machine machine(VBlankRate::Hz100, 9, 1);
    EventLog log;
    const FrameReceipt first = machine.step_frame(EventLog::collect, &log);

    require(first.cpu_multiplier_numerator == 9 &&
                first.cpu_multiplier_denominator == 1,
            "9x multiplier is canonical in the receipt");
    require(first.cpu_cycles == 3'048'192 &&
                first.total_cpu_cycles == 3'048'192 &&
                first.cpu_retirement_total == 3'048'192,
            "9x 100 Hz CPU retirement is exactly 3,048,192 cycles");
    require(first.spu_samples == 441,
            "9x CPU multiplier does not accelerate SPU samples");
    require(first.cdda_sectors == 0 && first.cdda_phase == 75,
            "9x CPU multiplier does not accelerate CDDA");
    require(log.events.size() == 4, "9x frame event count");
    for (const Event& event : log.events) {
        require(event.cpu_multiplier_numerator == 9 &&
                    event.cpu_multiplier_denominator == 1,
                "9x multiplier is explicit on every event");
    }
    require(log.events[0].kind == CLEAN_AOT_MACHINE_EVENT_VBLANK &&
                log.events[0].cpu_retirement_total == 0,
            "frame-start VBlank observes prior CPU retirement");
    for (std::size_t index = 1; index < log.events.size(); ++index) {
        require(log.events[index].cpu_retirement_total == 3'048'192,
                "post-budget events observe new CPU retirement");
    }

    const FrameReceipt second = machine.step_frame();
    require(second.cpu_cycles == 3'048'192 && second.cdda_sectors == 1,
            "9x second 100 Hz frame keeps independent clock budgets");

    Machine stock(VBlankRate::Hz100);
    require(stock.step_frame().digest != first.digest,
            "CPU multiplier participates in deterministic digest");
}

void test_fractional_multiplier_carry_and_one_hour() {
    constexpr std::uint64_t seconds = 3'600;
    constexpr std::uint64_t frames = seconds * 100;
    constexpr std::uint64_t multiplier_numerator = 133;
    constexpr std::uint64_t multiplier_denominator = 100;
    const std::uint64_t cycles_per_tick = Machine::CpuCyclesPerTimebaseTick;
    Machine machine(VBlankRate::Hz100, 266, 200);
    const Machine::CpuMultiplier multiplier = machine.cpu_multiplier();
    require(multiplier.numerator == multiplier_numerator &&
                multiplier.denominator == multiplier_denominator,
            "fractional multiplier is reduced at construction");

    std::uint64_t exact_numerator = 0;
    std::uint64_t expected_total = 0;
    std::uint64_t expected_cdda_numerator = 0;
    std::uint64_t expected_cdda_total = 0;
    FrameReceipt receipt{};
    bool saw_fractional_carry = false;

    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        receipt = machine.step_frame();
        exact_numerator += cycles_per_tick * multiplier_numerator;
        const std::uint64_t next_total =
            exact_numerator / multiplier_denominator;
        const std::uint64_t expected_cpu = next_total - expected_total;
        expected_total = next_total;
        expected_cdda_numerator += 75;
        const std::uint64_t next_cdda_total = expected_cdda_numerator / 100;
        const std::uint64_t expected_cdda =
            next_cdda_total - expected_cdda_total;
        expected_cdda_total = next_cdda_total;

        require(receipt.cpu_cycles == expected_cpu,
                "fractional CPU multiplier uses exact carry");
        require(receipt.total_cpu_cycles == expected_total &&
                    receipt.cpu_retirement_total == expected_total,
                "fractional CPU cumulative total is exact");
        require(receipt.spu_samples == 441,
                "fractional CPU multiplier leaves SPU at 441 samples");
        require(receipt.cdda_sectors == expected_cdda,
                "fractional CPU multiplier leaves CDDA on native phase");
        if (receipt.cpu_cycles != cycles_per_tick * multiplier_numerator /
                                      multiplier_denominator) {
            saw_fractional_carry = true;
        }
    }

    require(saw_fractional_carry,
            "fractional multiplier produces a non-integral carry case");
    require(receipt.total_cpu_cycles == cycles_per_tick *
                                             multiplier_numerator * 3'600,
            "fractional one-hour CPU total has no drift");
    require(receipt.total_spu_samples == Machine::SpuHz * seconds,
            "fractional one-hour SPU total has no drift");
    require(receipt.total_cdda_sectors ==
                Machine::CddaSectorsPerSecond * seconds,
            "fractional one-hour CDDA total has no drift");
    require(receipt.cdda_phase == 0 &&
                receipt.elapsed_timebase_ticks == frames,
            "fractional one-hour native timeline has no drift");
}

void test_invalid_cpu_multipliers() {
    Machine::CpuMultiplier canonical{};
    require(!Machine::canonicalize_cpu_multiplier(0, 1, canonical),
            "zero CPU multiplier numerator is rejected");
    require(!Machine::canonicalize_cpu_multiplier(1, 0, canonical),
            "zero CPU multiplier denominator is rejected");
    require(!Machine::canonicalize_cpu_multiplier(11, 1, canonical),
            "CPU multiplier above 10x is rejected");
    require(!Machine::canonicalize_cpu_multiplier(1'001, 100, canonical),
            "fractional CPU multiplier above 10x is rejected");
    require(Machine::canonicalize_cpu_multiplier(20, 2, canonical) &&
                canonical.numerator == 10 && canonical.denominator == 1,
            "inclusive 10x boundary is accepted and canonicalized");
    require(Machine::canonicalize_cpu_multiplier(
                std::numeric_limits<std::uint64_t>::max(),
                std::numeric_limits<std::uint64_t>::max(), canonical) &&
                canonical.numerator == 1 && canonical.denominator == 1,
            "large reducible CPU multiplier is canonicalized safely");

    bool threw = false;
    try {
        Machine invalid(VBlankRate::Hz100, 0, 1);
        (void)invalid;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "C++ constructor rejects invalid CPU multiplier");
}

void test_full_width_cpu_multiplier_carry() {
    constexpr std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    Machine::CpuMultiplier canonical{};
    require(Machine::canonicalize_cpu_multiplier(maximum - 1, maximum,
                                                 canonical) &&
                canonical.numerator == maximum - 1 &&
                canonical.denominator == maximum,
            "full-width canonical multiplier below 1x is accepted");

    Machine almost_one(VBlankRate::Hz100, maximum - 1, maximum);
    const FrameReceipt first = almost_one.step_frame();
    const FrameReceipt second = almost_one.step_frame();
    require(first.cpu_cycles == Machine::CpuCyclesPerTimebaseTick - 1 &&
                second.cpu_cycles == Machine::CpuCyclesPerTimebaseTick &&
                second.total_cpu_cycles ==
                    Machine::CpuCyclesPerTimebaseTick * 2 - 1,
            "full-width multiplier advances with exact carry");
    require(first.spu_samples == 441 && first.cdda_sectors == 0 &&
                second.spu_samples == 441 && second.cdda_sectors == 1,
            "full-width CPU multiplier leaves native clocks unchanged");
    require(first.cpu_multiplier_numerator == maximum - 1 &&
                first.cpu_multiplier_denominator == maximum,
            "full-width canonical multiplier is preserved in receipts");

    require(Machine::canonicalize_cpu_multiplier(1, maximum, canonical) &&
                canonical.numerator == 1 &&
                canonical.denominator == maximum,
            "smallest full-width positive multiplier is accepted");
    Machine tiny(VBlankRate::Hz100, 1, maximum);
    const FrameReceipt tiny_receipt = tiny.step_frame();
    require(tiny_receipt.cpu_cycles == 0 &&
                tiny_receipt.cpu_retirement_total == 0 &&
                tiny_receipt.spu_samples == 441 &&
                tiny_receipt.cdda_sectors == 0,
            "sub-cycle retirement carry does not stall native devices");

    Machine maximum_rate(VBlankRate::Hz100, 20, 2);
    const FrameReceipt maximum_receipt = maximum_rate.step_frame();
    require(maximum_receipt.cpu_cycles ==
                Machine::CpuCyclesPerTimebaseTick * 10 &&
                maximum_receipt.spu_samples == 441 &&
                maximum_receipt.cdda_sectors == 0,
            "inclusive 10x multiplier scales retirement only");
}

void test_multiplier_mode_switches() {
    Machine machine(VBlankRate::Hz100, 9, 1);
    const VBlankRate rates[] = {VBlankRate::Hz100, VBlankRate::Hz50,
                                VBlankRate::Hz25, VBlankRate::Hz100};
    const std::uint32_t quanta[] = {1, 2, 4, 1};
    std::uint64_t elapsed = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        require(machine.set_vblank_rate(rates[index]),
                "9x mode switch is accepted");
        const FrameReceipt receipt = machine.step_frame();
        elapsed += quanta[index];
        require(receipt.timebase_ticks == quanta[index],
                "mode switch preserves native timebase quantum");
        require(receipt.total_cpu_cycles == elapsed * 338'688 * 9,
                "mode switch scales CPU only");
        require(receipt.total_spu_samples == elapsed * 441,
                "mode switch keeps SPU on native clock");
        require(receipt.cpu_multiplier_numerator == 9 &&
                    receipt.cpu_multiplier_denominator == 1,
                "mode switch cannot mutate immutable CPU multiplier");
    }
}

void test_one_hour_has_no_drift() {
    constexpr std::uint64_t seconds = 3'600;
    constexpr std::uint64_t frames = seconds * 100;
    Machine machine(VBlankRate::Hz100);
    FrameReceipt receipt{};

    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        receipt = machine.step_frame();
    }

    require(receipt.frame_index == frames - 1,
            "one-hour final frame index");
    require(receipt.elapsed_timebase_ticks == frames,
            "one-hour exact elapsed timebase");
    require(receipt.total_cpu_cycles == Machine::CpuHz * seconds,
            "one-hour CPU total has no drift");
    require(receipt.total_spu_samples == Machine::SpuHz * seconds,
            "one-hour SPU total has no drift");
    require(receipt.total_cdda_sectors ==
                Machine::CddaSectorsPerSecond * seconds,
            "one-hour CDDA total has no drift");
    require(receipt.cdda_phase == 0,
            "one-hour CDDA phase returns to zero");
}

struct ModeSegment {
    VBlankRate rate;
    std::uint64_t frames;
};

std::vector<FrameReceipt> run_mode_schedule(EventLog& log) {
    constexpr ModeSegment schedule[] = {
        {VBlankRate::Hz100, 900},
        {VBlankRate::Hz50, 900},
        {VBlankRate::Hz25, 900},
        {VBlankRate::Hz100, 900},
    };

    Machine machine(VBlankRate::Hz100);
    std::vector<FrameReceipt> receipts;
    receipts.reserve(3'600);
    std::uint64_t expected_timebase = 0;
    std::uint64_t expected_cdda_numerator = 0;
    std::uint64_t expected_cdda_total = 0;
    std::uint64_t frame = 0;

    for (const ModeSegment& segment : schedule) {
        require(machine.set_vblank_rate(segment.rate),
                "accept supported mode switch");
        for (std::uint64_t offset = 0; offset < segment.frames; ++offset) {
            const FrameReceipt receipt =
                machine.step_frame(EventLog::collect, &log);
            const std::uint32_t quantum = timebase_ticks(segment.rate);
            expected_timebase += quantum;
            expected_cdda_numerator += 75 * quantum;
            const std::uint64_t next_cdda_total =
                expected_cdda_numerator / 100;
            const std::uint64_t expected_cdda =
                next_cdda_total - expected_cdda_total;
            expected_cdda_total = next_cdda_total;

            require(receipt.frame_index == frame, "scheduled frame index");
            require(receipt.timebase_ticks == quantum,
                    "scheduled rational timebase quantum");
            require(receipt.cdda_sectors == expected_cdda,
                    "scheduled CDDA accumulator");
            require(receipt.total_cpu_cycles == expected_timebase * 338'688,
                    "scheduled CPU total has no drift");
            require(receipt.total_spu_samples == expected_timebase * 441,
                    "scheduled SPU total has no drift");
            require(receipt.total_cdda_sectors == expected_cdda_total,
                    "scheduled CDDA total has no drift");
            receipts.push_back(receipt);
            ++frame;
        }
    }

    require(receipts.size() == 3'600, "scheduled run frame count");
    require(receipts.back().elapsed_timebase_ticks == 7'200,
            "mode switching preserves elapsed rational time");
    require(receipts.back().total_cpu_cycles == std::uint64_t{7'200} *
                                                       338'688,
            "mode switching CPU total");
    require(receipts.back().total_spu_samples == std::uint64_t{7'200} * 441,
            "mode switching SPU total");
    require(receipts.back().total_cdda_sectors == 5'400,
            "mode switching CDDA total");
    require(receipts.back().cdda_phase == 0,
            "mode switching returns CDDA phase exactly");
    return receipts;
}

void test_mode_switching_is_repeatable() {
    EventLog first_log;
    EventLog second_log;
    const std::vector<FrameReceipt> first = run_mode_schedule(first_log);
    const std::vector<FrameReceipt> second = run_mode_schedule(second_log);

    require(first.size() == second.size(), "repeatable receipt count");
    for (std::size_t index = 0; index < first.size(); ++index) {
        require(first[index].digest == second[index].digest,
                "repeatable per-frame receipt digest");
        require(first[index].total_cdda_sectors ==
                    second[index].total_cdda_sectors,
                "repeatable per-frame CDDA total");
    }
    require(first_log.events.size() == 14'403,
            "three mode switches plus four events per frame");
    require(second_log.events.size() == first_log.events.size(),
            "repeatable event count");
    for (std::size_t index = 0; index < first_log.events.size(); ++index) {
        const Event& a = first_log.events[index];
        const Event& b = second_log.events[index];
        require(a.sequence == b.sequence && a.frame_index == b.frame_index &&
                    a.kind == b.kind && a.vblank_hz == b.vblank_hz &&
                    a.amount == b.amount && a.cumulative == b.cumulative &&
                    a.auxiliary == b.auxiliary &&
                    a.cpu_multiplier_numerator ==
                        b.cpu_multiplier_numerator &&
                    a.cpu_multiplier_denominator ==
                        b.cpu_multiplier_denominator &&
                    a.cpu_retirement_total == b.cpu_retirement_total,
                "repeatable event receipt");
    }

    std::size_t mode_switches = 0;
    for (const Event& event : first_log.events) {
        if (event.kind == CLEAN_AOT_MACHINE_EVENT_MODE_SWITCH) {
            ++mode_switches;
            require(event.amount == event.vblank_hz,
                    "mode event new rate");
            require(event.auxiliary != event.vblank_hz,
                    "mode event old rate");
        }
    }
    require(mode_switches == 3, "exactly three scheduled mode switches");
    std::cout << "EVIDENCE SWITCH frames=" << first.size()
              << " timebase=" << first.back().elapsed_timebase_ticks
              << " cpu=" << first.back().total_cpu_cycles
              << " spu=" << first.back().total_spu_samples
              << " cdda=" << first.back().total_cdda_sectors
              << " digest=0x" << std::hex << first.back().digest << std::dec
              << " events=" << first_log.events.size() << '\n';
}

void test_unobserved_mode_change_is_collapsed() {
    Machine machine(VBlankRate::Hz100);
    EventLog log;
    require(machine.set_vblank_rate(VBlankRate::Hz50),
            "accept transient mode change");
    require(machine.set_vblank_rate(VBlankRate::Hz25),
            "accept second transient mode change");
    require(machine.set_vblank_rate(VBlankRate::Hz100),
            "accept reversion before a frame boundary");
    const FrameReceipt receipt =
        machine.step_frame(EventLog::collect, &log);
    require(receipt.vblank_hz == CLEAN_AOT_MACHINE_VBLANK_100,
            "reverted mode remains the sampled mode");
    require(log.events.size() == 4,
            "unobserved transient modes do not create fake events");
}

void test_mode_switch_preserves_fractional_phase_and_event_order() {
    Machine machine(VBlankRate::Hz100);
    FrameReceipt receipt = machine.step_frame();
    require(receipt.cdda_sectors == 0 && receipt.cdda_phase == 75,
            "100 Hz establishes nonzero CDDA phase");

    EventLog log;
    require(machine.set_vblank_rate(VBlankRate::Hz25),
            "switch to 25 Hz at nonzero phase");
    receipt = machine.step_frame(EventLog::collect, &log);
    require(receipt.cdda_sectors == 3 && receipt.cdda_phase == 75,
            "25 Hz preserves incoming CDDA phase");
    require(log.events.size() == 5,
            "switch frame contains one mode and four frame events");
    require(receipt.event_sequence_end == receipt.event_sequence_begin + 4,
            "switch receipt spans five inclusive event sequences");
    require(log.events[0].kind == CLEAN_AOT_MACHINE_EVENT_MODE_SWITCH &&
                log.events[0].auxiliary == CLEAN_AOT_MACHINE_VBLANK_100 &&
                log.events[0].amount == CLEAN_AOT_MACHINE_VBLANK_25,
            "mode switch is first and records old/new rates");
    require(log.events[1].kind == CLEAN_AOT_MACHINE_EVENT_VBLANK &&
                log.events[2].kind == CLEAN_AOT_MACHINE_EVENT_CPU_CYCLES &&
                log.events[3].kind == CLEAN_AOT_MACHINE_EVENT_SPU_SAMPLES &&
                log.events[4].kind == CLEAN_AOT_MACHINE_EVENT_CDDA_SECTORS,
            "switch frame retains documented event order");
    require(log.events[0].cpu_retirement_total == 338'688 &&
                log.events[1].cpu_retirement_total == 338'688 &&
                log.events[2].cpu_retirement_total == 1'693'440 &&
                log.events[3].cpu_retirement_total == 1'693'440 &&
                log.events[4].cpu_retirement_total == 1'693'440,
            "switch and VBlank precede the switched frame CPU budget");

    require(machine.set_vblank_rate(VBlankRate::Hz50),
            "switch to 50 Hz at nonzero phase");
    receipt = machine.step_frame();
    require(receipt.cdda_sectors == 2 && receipt.cdda_phase == 25,
            "50 Hz carries CDDA phase exactly");
    require(machine.set_vblank_rate(VBlankRate::Hz100),
            "switch to 100 Hz at nonzero phase");
    receipt = machine.step_frame();
    require(receipt.cdda_sectors == 1 && receipt.cdda_phase == 0,
            "mixed-rate schedule closes CDDA phase exactly");
}

void test_reset_restores_initial_state() {
    Machine machine(VBlankRate::Hz50, 266, 200);
    Machine fresh(VBlankRate::Hz50, 133, 100);
    (void)machine.step_frame();
    require(machine.set_vblank_rate(VBlankRate::Hz100),
            "queue mode switch before reset");
    machine.reset();

    require(machine.vblank_rate() == VBlankRate::Hz50,
            "reset restores construction rate");
    require(machine.frame_count() == 0, "reset restores frame count");
    require(machine.cpu_multiplier().numerator == 133 &&
                machine.cpu_multiplier().denominator == 100,
            "reset preserves immutable CPU multiplier");
    require(machine.receipt_digest() == fresh.receipt_digest(),
            "reset restores digest seed");

    const FrameReceipt reset_receipt = machine.step_frame();
    const FrameReceipt fresh_receipt = fresh.step_frame();
    require(reset_receipt.digest == fresh_receipt.digest &&
                reset_receipt.event_sequence_begin == 0 &&
                reset_receipt.event_sequence_end == 3,
            "reset removes pending events and reproduces fresh state");
}

struct CEventLog {
    std::vector<clean_aot_machine_event> events;

    static void collect(const clean_aot_machine_event* event, void* user) {
        auto* log = static_cast<CEventLog*>(user);
        log->events.push_back(*event);
    }
};

void test_c_abi() {
    clean_aot_machine_frame_receipt null_receipt{};
    require(clean_aot_machine_create(13) == nullptr,
            "C ABI rejects unsupported initial rate");
    require(clean_aot_machine_set_vblank_rate(nullptr, 100) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects null machine for rate change");
    require(clean_aot_machine_step_frame(nullptr, nullptr, nullptr,
                                         &null_receipt) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects null machine for step");
    require(clean_aot_machine_get_vblank_rate(nullptr) == 0 &&
                clean_aot_machine_frame_count(nullptr) == 0 &&
                clean_aot_machine_receipt_digest(nullptr) == 0,
            "C ABI null queries are stable");
    clean_aot_machine_reset(nullptr);
    clean_aot_machine_destroy(nullptr);

    std::uint64_t canonical_numerator = 99;
    std::uint64_t canonical_denominator = 99;
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                266, 200, &canonical_numerator, &canonical_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_OK &&
                canonical_numerator == 133 && canonical_denominator == 100,
            "C ABI canonicalizes an exact fractional multiplier");
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                0, 1, &canonical_numerator, &canonical_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_CPU_MULTIPLIER &&
                canonical_numerator == 0 && canonical_denominator == 0,
            "C ABI rejects zero CPU multiplier");
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                1, 0, &canonical_numerator, &canonical_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_CPU_MULTIPLIER,
            "C ABI rejects zero CPU denominator");
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                11, 1, &canonical_numerator, &canonical_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_CPU_MULTIPLIER,
            "C ABI rejects CPU multiplier above 10x");
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                1, 1, nullptr, &canonical_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects null canonical output");
    require(clean_aot_machine_canonicalize_cpu_multiplier(
                1, 1, &canonical_numerator, &canonical_numerator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects aliased canonical outputs");
    require(clean_aot_machine_create_with_cpu_multiplier(100, 0, 1) ==
                nullptr,
            "C ABI multiplier factory rejects zero numerator");
    require(clean_aot_machine_create_with_cpu_multiplier(100, 11, 1) ==
                nullptr,
            "C ABI multiplier factory rejects out-of-range multiplier");
    require(clean_aot_machine_create_with_cpu_multiplier(13, 1, 1) ==
                nullptr,
            "C ABI multiplier factory rejects invalid rate");

    clean_aot_machine* machine =
        clean_aot_machine_create(CLEAN_AOT_MACHINE_VBLANK_100);
    require(machine != nullptr, "C ABI creates machine");
    require(clean_aot_machine_get_vblank_rate(machine) == 100,
            "C ABI reports initial rate");
    std::uint64_t machine_numerator = 0;
    std::uint64_t machine_denominator = 0;
    require(clean_aot_machine_get_cpu_multiplier(
                machine, &machine_numerator, &machine_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_OK &&
                machine_numerator == 1 && machine_denominator == 1,
            "C ABI stock factory reports 1/1 multiplier");

    CEventLog log;
    clean_aot_machine_frame_receipt receipt{};
    require(clean_aot_machine_step_frame(machine, nullptr, nullptr, nullptr) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects null receipt");
    require(clean_aot_machine_step_frame(machine, CEventLog::collect, &log,
                                         &receipt) ==
                CLEAN_AOT_MACHINE_STATUS_OK,
            "C ABI advances frame");
    require(receipt.cpu_cycles == 338'688 && receipt.spu_samples == 441,
            "C ABI exact 100 Hz receipt");
    require(log.events.size() == 4, "C ABI receives domain events");
    require(clean_aot_machine_set_vblank_rate(machine, 13) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_RATE,
            "C ABI rejects unsupported mode switch");
    require(clean_aot_machine_set_vblank_rate(machine, 50) ==
                CLEAN_AOT_MACHINE_STATUS_OK,
            "C ABI accepts supported mode switch");
    require(clean_aot_machine_step_frame(machine, CEventLog::collect, &log,
                                         &receipt) ==
                CLEAN_AOT_MACHINE_STATUS_OK,
            "C ABI advances switched frame");
    require(receipt.vblank_hz == 50 && receipt.timebase_ticks == 2,
            "C ABI switched receipt");
    require(log.events.size() == 9,
            "C ABI reports pending mode switch before next frame");
    require(clean_aot_machine_frame_count(machine) == 2,
            "C ABI frame count");
    require(clean_aot_machine_receipt_digest(machine) == receipt.digest,
            "C ABI digest query");
    clean_aot_machine_reset(machine);
    require(clean_aot_machine_frame_count(machine) == 0,
            "C ABI reset frame count");
    clean_aot_machine_destroy(machine);

    clean_aot_machine* nine_x =
        clean_aot_machine_create_with_cpu_multiplier(100, 18, 2);
    require(nine_x != nullptr, "C ABI creates canonicalized 9x machine");
    require(clean_aot_machine_get_cpu_multiplier(
                nine_x, &machine_numerator, &machine_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_OK &&
                machine_numerator == 9 && machine_denominator == 1,
            "C ABI reports reduced 9x multiplier");
    clean_aot_machine_frame_receipt nine_receipt{};
    CEventLog nine_log;
    require(clean_aot_machine_step_frame(nine_x, CEventLog::collect, &nine_log,
                                         &nine_receipt) ==
                CLEAN_AOT_MACHINE_STATUS_OK,
            "C ABI advances 9x machine");
    require(nine_receipt.cpu_cycles == 3'048'192 &&
                nine_receipt.spu_samples == 441 &&
                nine_receipt.cdda_sectors == 0 &&
                nine_receipt.cpu_retirement_total == 3'048'192 &&
                nine_receipt.cpu_multiplier_numerator == 9 &&
                nine_receipt.cpu_multiplier_denominator == 1,
            "C ABI exposes independent 9x receipt clocks");
    require(nine_log.events.size() == 4 &&
                nine_log.events[1].cpu_multiplier_numerator == 9 &&
                nine_log.events[1].cpu_multiplier_denominator == 1 &&
                nine_log.events[1].cpu_retirement_total == 3'048'192,
            "C ABI event exposes 9x CPU retirement metadata");
    require(clean_aot_machine_get_cpu_multiplier(nine_x, nullptr,
                                                 &machine_denominator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects null multiplier query output");
    require(clean_aot_machine_get_cpu_multiplier(
                nine_x, &machine_numerator, &machine_numerator) ==
                CLEAN_AOT_MACHINE_STATUS_INVALID_ARGUMENT,
            "C ABI rejects aliased multiplier query outputs");
    clean_aot_machine_destroy(nine_x);
}

} // namespace

int main() {
    test_100_hz_3600_frames();
    test_fixed_modes();
    test_nine_x_independent_clocks();
    test_fractional_multiplier_carry_and_one_hour();
    test_invalid_cpu_multipliers();
    test_full_width_cpu_multiplier_carry();
    test_multiplier_mode_switches();
    test_one_hour_has_no_drift();
    test_mode_switching_is_repeatable();
    test_unobserved_mode_change_is_collapsed();
    test_mode_switch_preserves_fractional_phase_and_event_order();
    test_reset_restores_initial_state();
    test_c_abi();
    std::cout << "PASS: clean deterministic machine timing core; "
              << "3,600-frame 100 Hz run, one-hour drift run, "
              << "3,600-frame switched run, C and C++ ABI\n";
    return EXIT_SUCCESS;
}
