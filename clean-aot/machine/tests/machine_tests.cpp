#include "machine.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using w3::machine::Event;
using w3::machine::EventKind;
using w3::machine::FrameReceipt;
using w3::machine::Machine;
using w3::machine::VBlankRate;

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
        require(receipt.vblank_hz == W3_MACHINE_VBLANK_100,
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
        require(log.events[first + 0].kind == W3_MACHINE_EVENT_VBLANK,
                "VBlank event order");
        require(log.events[first + 1].kind == W3_MACHINE_EVENT_CPU_CYCLES,
                "CPU event order");
        require(log.events[first + 2].kind == W3_MACHINE_EVENT_SPU_SAMPLES,
                "SPU event order");
        require(log.events[first + 3].kind == W3_MACHINE_EVENT_CDDA_SECTORS,
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
        if (event.kind == W3_MACHINE_EVENT_MODE_SWITCH) {
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
    require(receipt.vblank_hz == W3_MACHINE_VBLANK_100,
            "reverted mode remains the sampled mode");
    require(log.events.size() == 4,
            "unobserved transient modes do not create fake events");
}

struct CEventLog {
    std::vector<w3_machine_event> events;

    static void collect(const w3_machine_event* event, void* user) {
        auto* log = static_cast<CEventLog*>(user);
        log->events.push_back(*event);
    }
};

void test_c_abi() {
    require(w3_machine_create(13) == nullptr,
            "C ABI rejects unsupported initial rate");
    w3_machine* machine = w3_machine_create(W3_MACHINE_VBLANK_100);
    require(machine != nullptr, "C ABI creates machine");
    require(w3_machine_get_vblank_rate(machine) == 100,
            "C ABI reports initial rate");

    CEventLog log;
    w3_machine_frame_receipt receipt{};
    require(w3_machine_step_frame(machine, CEventLog::collect, &log,
                                  &receipt) == W3_MACHINE_STATUS_OK,
            "C ABI advances frame");
    require(receipt.cpu_cycles == 338'688 && receipt.spu_samples == 441,
            "C ABI exact 100 Hz receipt");
    require(log.events.size() == 4, "C ABI receives domain events");
    require(w3_machine_set_vblank_rate(machine, 13) ==
                W3_MACHINE_STATUS_INVALID_RATE,
            "C ABI rejects unsupported mode switch");
    require(w3_machine_set_vblank_rate(machine, 50) ==
                W3_MACHINE_STATUS_OK,
            "C ABI accepts supported mode switch");
    require(w3_machine_step_frame(machine, CEventLog::collect, &log,
                                  &receipt) == W3_MACHINE_STATUS_OK,
            "C ABI advances switched frame");
    require(receipt.vblank_hz == 50 && receipt.timebase_ticks == 2,
            "C ABI switched receipt");
    require(log.events.size() == 9,
            "C ABI reports pending mode switch before next frame");
    require(w3_machine_frame_count(machine) == 2,
            "C ABI frame count");
    require(w3_machine_receipt_digest(machine) == receipt.digest,
            "C ABI digest query");
    w3_machine_reset(machine);
    require(w3_machine_frame_count(machine) == 0,
            "C ABI reset frame count");
    w3_machine_destroy(machine);
}

} // namespace

int main() {
    test_100_hz_3600_frames();
    test_fixed_modes();
    test_mode_switching_is_repeatable();
    test_unobserved_mode_change_is_collapsed();
    test_c_abi();
    std::cout << "PASS: clean deterministic machine timing core; "
              << "3,600-frame 100 Hz run, 3,600-frame switched run, C ABI\n";
    return EXIT_SUCCESS;
}
