#include "machine.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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
        require(receipt.spu_samples == 441,
                "100 Hz exact SPU samples per VBlank");
        require(receipt.cdda_sectors == expected_cdda,
                "100 Hz rational CDDA allocation");
        require(receipt.total_cpu_cycles == reference_timebase * 338'688,
                "100 Hz cumulative CPU cycles");
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
                    a.auxiliary == b.auxiliary,
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
    Machine machine(VBlankRate::Hz50);
    Machine fresh(VBlankRate::Hz50);
    (void)machine.step_frame();
    require(machine.set_vblank_rate(VBlankRate::Hz100),
            "queue mode switch before reset");
    machine.reset();

    require(machine.vblank_rate() == VBlankRate::Hz50,
            "reset restores construction rate");
    require(machine.frame_count() == 0, "reset restores frame count");
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

    clean_aot_machine* machine =
        clean_aot_machine_create(CLEAN_AOT_MACHINE_VBLANK_100);
    require(machine != nullptr, "C ABI creates machine");
    require(clean_aot_machine_get_vblank_rate(machine) == 100,
            "C ABI reports initial rate");

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
}

} // namespace

int main() {
    test_100_hz_3600_frames();
    test_fixed_modes();
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
