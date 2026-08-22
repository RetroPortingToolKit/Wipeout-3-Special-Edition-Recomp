#include "sio.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using clean_aot::sio::ByteTransfer;
using clean_aot::sio::Cycle;
using clean_aot::sio::MemoryCard;
using clean_aot::sio::MmioError;
using clean_aot::sio::MmioWidth;
using clean_aot::sio::PadButton;
using clean_aot::sio::PadDevice;
using clean_aot::sio::PadInput;
using clean_aot::sio::Port;
using clean_aot::sio::Sio0;

void require(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, expression);
        std::abort();
    }
}

#define CHECK(expression) require((expression), #expression, __LINE__)

struct IrqLog {
    unsigned rising_edges = 0;
    unsigned falling_edges = 0;
    bool level = false;
};

void irq_callback(void* opaque, bool level) noexcept {
    auto* log = static_cast<IrqLog*>(opaque);
    if (level && !log->level) {
        ++log->rising_edges;
    }
    if (!level && log->level) {
        ++log->falling_edges;
    }
    log->level = level;
}

std::uint8_t clock_byte(Sio0& sio, std::uint8_t value, Cycle& cycle) {
    const auto write = sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, value, cycle);
    CHECK(write.accepted);
    cycle += sio.cycles_per_byte();
    const auto read = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, cycle);
    CHECK(read.accepted);
    return static_cast<std::uint8_t>(read.value);
}

void drain_device_ack(Sio0& sio, Cycle& cycle) {
    unsigned event_count = 0U;
    while (sio.next_event_cycle() != clean_aot::sio::kNoEventCycle) {
        const Cycle next = sio.next_event_cycle();
        CHECK(next >= cycle);
        cycle = next;
        CHECK(sio.advance_to(cycle));
        ++event_count;
        CHECK(event_count <= 2U);
    }
}

std::uint8_t exchange_byte(Sio0& sio, std::uint8_t value, Cycle& cycle) {
    const std::uint8_t result = clock_byte(sio, value, cycle);
    drain_device_ack(sio, cycle);
    return result;
}

void transmit_without_read(Sio0& sio,
                           std::uint32_t value,
                           MmioWidth width,
                           Cycle& cycle) {
    CHECK(sio.write(clean_aot::sio::kDataOffset, width, value, cycle).accepted);
    cycle += sio.cycles_per_byte();
    CHECK(sio.advance_to(cycle));
    drain_device_ack(sio, cycle);
}

void begin_bus(Sio0& sio, Cycle cycle = 0U) {
    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU, cycle).accepted);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, cycle).accepted);
    const std::uint16_t control = static_cast<std::uint16_t>(
        clean_aot::sio::kControlTxEnable | clean_aot::sio::kControlRxEnable |
        clean_aot::sio::kControlDsrInterruptEnable);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword, control, cycle).accepted);
    CHECK(sio.select(Port::One, cycle).accepted);
}

void end_bus(Sio0& sio, Cycle cycle) {
    CHECK(sio.deselect(cycle).accepted);
}

void test_registers_and_timing() {
    IrqLog log{};
    Sio0 sio(Sio0::Callbacks{irq_callback, &log});

    CHECK(!sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x00U, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnsupportedMode);

    CHECK(!sio.read(clean_aot::sio::kDataOffset, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::ReceiveEmpty);
    CHECK(!sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnsupportedWidth);
    CHECK(!sio.read(clean_aot::sio::kModeOffset + 1U, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::Misaligned);
    CHECK(!sio.read(0x20U, MmioWidth::Byte, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnknownRegister);
    CHECK(!sio.read(0x0CU, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnknownRegister);

    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x00CEU, 0U).accepted);
    CHECK(sio.mode() == 0x000EU);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, 0U).accepted);
    CHECK(sio.cycles_per_bit() == 2176U);
    CHECK(sio.cycles_per_byte() == 17408U);

    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU, 0U).accepted);
    CHECK(sio.cycles_per_bit() == 136U);
    CHECK(sio.cycles_per_byte() == 1088U);
    const auto preserved_timer = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 0U);
    CHECK(preserved_timer.accepted);
    CHECK(((preserved_timer.value >> 11U) & 0x001FFFFFU) == 1088U);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(clean_aot::sio::kControlTxEnable |
                                                clean_aot::sio::kControlRxEnable), 0U)
              .accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0xA5U, 0U).accepted);
    const auto first_half_bit = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 50U);
    CHECK(first_half_bit.accepted);
    CHECK(((first_half_bit.value >> 11U) & 0x001FFFFFU) == 1038U);
    const auto in_flight = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 100U);
    CHECK(in_flight.accepted);
    CHECK((in_flight.value & 0x00000400U) == 0U);
    CHECK(((in_flight.value >> 11U) & 0x001FFFFFU) == 988U);
    CHECK(!sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 100U).accepted);
    CHECK(sio.last_error() == MmioError::ReceiveEmpty);
    CHECK(sio.advance_to(1088U));
    const auto idle_timer = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 1100U);
    CHECK(idle_timer.accepted);
    CHECK(((idle_timer.value >> 11U) & 0x001FFFFFU) == 56U);
    const auto reloaded_idle_timer = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 1156U);
    CHECK(reloaded_idle_timer.accepted);
    CHECK(((reloaded_idle_timer.value >> 11U) & 0x001FFFFFU) == 68U);
    CHECK((sio.control() & clean_aot::sio::kControlRxEnable) == 0U);
    const auto received = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 1156U);
    CHECK(received.accepted && received.value == 0xFFU);

    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(0x8000U), 1156U)
              .accepted == false);
    CHECK(sio.last_error() == MmioError::UnsupportedValue);
    CHECK(log.rising_edges == 0U);
}

void test_wide_data_register_access() {
    PadDevice pad;
    pad.set_input(PadInput{0xBEEFU, 0x80U, 0x80U, 0x80U, 0x80U});
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    Cycle cycle = 0U;
    transmit_without_read(sio, 0xAB01U, MmioWidth::Halfword, cycle);
    transmit_without_read(sio, 0x12340042U, MmioWidth::Word, cycle);
    transmit_without_read(sio, 0x00U, MmioWidth::Byte, cycle);
    transmit_without_read(sio, 0x00U, MmioWidth::Byte, cycle);
    transmit_without_read(sio, 0x00U, MmioWidth::Byte, cycle);

    const auto halfword = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Halfword, cycle);
    CHECK(halfword.accepted && halfword.value == 0x41FFU);
    const auto word = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Word, cycle);
    CHECK(word.accepted && word.value == 0xBEEF5A41U);
    CHECK((sio.status() & clean_aot::sio::kStatusRxReady) == 0U);
    CHECK(!sio.ack_collision());
    CHECK(sio.deselect(cycle).accepted);
}

void test_tx_holding_register_and_irq_restore() {
    PadDevice pad;
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, 0U).accepted);
    CHECK(sio.select(Port::One, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x00U, 0U).accepted);
    const auto replacement = sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x01U, 0U);
    CHECK(replacement.accepted && replacement.overwritten);
    CHECK((sio.status() & clean_aot::sio::kStatusTxReady) == 0U);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(sio.control() | clean_aot::sio::kControlTxEnable),
                    0U)
              .accepted);
    Cycle cycle = sio.cycles_per_byte();
    CHECK(sio.advance_to(cycle));
    const auto address_reply = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, cycle);
    CHECK(address_reply.accepted && address_reply.value == 0xFFU);
    CHECK(sio.next_event_cycle() == cycle + Sio0::kDefaultAckDelayCycles);
    drain_device_ack(sio, cycle);
    CHECK(!sio.ack_collision());
    CHECK(sio.deselect(cycle).accepted);

    IrqLog log{};
    Sio0 irq_sio(Sio0::Callbacks{irq_callback, &log});
    CHECK(irq_sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                        clean_aot::sio::kControlTxInterruptEnable, 0U)
              .accepted);
    CHECK(irq_sio.irq_pending());
    CHECK(log.rising_edges == 1U && log.falling_edges == 0U);
    const auto saved = irq_sio.snapshot();
    CHECK(irq_sio.restore(saved));
    CHECK(log.rising_edges == 1U && log.falling_edges == 0U);

    auto malformed = saved;
    malformed.selected_port = Port::Two;
    CHECK(!irq_sio.restore(malformed));
    CHECK(irq_sio.last_error() == MmioError::SnapshotInvalid);
    CHECK(irq_sio.cycle() == saved.cycle);
    malformed = saved;
    malformed.shift_active = true;
    malformed.shift_complete = 1U;
    malformed.shift_bit_period = 1U;
    malformed.tx_count = 0U;
    CHECK(!irq_sio.restore(malformed));
    malformed = saved;
    malformed.active_device = static_cast<clean_aot::sio::DeviceKind>(0xFFU);
    CHECK(!irq_sio.restore(malformed));
}

void test_address_ack_boundary_and_cycle_bounds() {
    PadDevice pad;
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    Cycle cycle = 0U;

    CHECK(clock_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(sio.next_event_cycle() == clean_aot::sio::kNoEventCycle);
    const auto inert = sio.snapshot();
    CHECK(!inert.addressing && inert.active_device == clean_aot::sio::DeviceKind::None);
    CHECK(sio.restore(inert));
    CHECK(clock_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(sio.next_event_cycle() == clean_aot::sio::kNoEventCycle);
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(clock_byte(sio, 0x01U, cycle) == 0xFFU);
    const Cycle ack_begin = sio.next_event_cycle();
    CHECK(ack_begin == cycle + Sio0::kDefaultAckDelayCycles);
    CHECK(sio.advance_to(ack_begin));
    CHECK(sio.dsr_active());
    cycle = ack_begin + 52U;
    CHECK(sio.advance_to(cycle));
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 6U, cycle).accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x42U, cycle).accepted);
    // The queued byte cannot start until the measured ACK pulse has ended.
    cycle += Sio0::kDefaultAckPulseCycles - 52U + sio.cycles_per_byte();
    CHECK(sio.advance_to(cycle));
    CHECK(!sio.dsr_active());
    CHECK(!sio.ack_collision());
    CHECK(sio.next_event_cycle() == cycle + Sio0::kDefaultAckDelayCycles);
    const auto id = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, cycle);
    CHECK(id.accepted && id.value == 0x41U);
    drain_device_ack(sio, cycle);
    end_bus(sio, cycle);

    Sio0 bounded;
    constexpr Cycle near_limit = clean_aot::sio::kNoEventCycle - 1000U;
    CHECK(bounded.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU,
                        near_limit)
              .accepted);
    CHECK(bounded.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U,
                        near_limit)
              .accepted);
    CHECK(bounded.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                        clean_aot::sio::kControlTxEnable, near_limit)
              .accepted);
    const auto before = bounded.snapshot();
    const auto rejected = bounded.write(clean_aot::sio::kDataOffset, MmioWidth::Byte,
                                        0x01U, near_limit);
    CHECK(!rejected.accepted && rejected.error == MmioError::CycleOverflow);
    const auto after = bounded.snapshot();
    CHECK(!after.shift_active && after.tx_count == before.tx_count);
    const Cycle before_sentinel = bounded.cycle();
    CHECK(!bounded.advance_to(clean_aot::sio::kNoEventCycle));
    CHECK(bounded.cycle() == before_sentinel &&
          bounded.last_error() == MmioError::CycleOverflow);

    auto impossible = before;
    impossible.shift_active = true;
    impossible.shift_bit_period = 136U;
    impossible.shift_complete = near_limit + 500U;
    impossible.tx_count = 1U;
    CHECK(!bounded.restore(impossible));
    CHECK(bounded.last_error() == MmioError::SnapshotInvalid);
}

void test_sio_snapshot_timing_and_error_guards() {
    PadDevice pad;
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x01U, 0U).accepted);

    const auto in_flight = sio.snapshot();
    CHECK(in_flight.shift_active && in_flight.shift_bit_period == sio.cycles_per_bit());
    auto impossible_period = in_flight;
    impossible_period.shift_bit_period = 0U;
    CHECK(!sio.restore(impossible_period));
    CHECK(sio.last_error() == MmioError::SnapshotInvalid);

    // MODE and BAUD changes apply to the next byte, not the byte already in
    // the shifter.  Such snapshots are reachable and must restore exactly.
    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword,
                    clean_aot::sio::kModeLengthMask | 0x02U, sio.cycle()).accepted);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 7U,
                    sio.cycle()).accepted);
    const auto retimed_in_flight = sio.snapshot();
    CHECK(retimed_in_flight.shift_bit_period == in_flight.shift_bit_period);
    CHECK(retimed_in_flight.shift_bit_period != sio.cycles_per_bit());
    CHECK(sio.restore(retimed_in_flight));

    CHECK(sio.advance_to(retimed_in_flight.shift_complete));
    const auto pending_ack = sio.snapshot();
    CHECK(pending_ack.ack_pending);
    auto short_ack = pending_ack;
    short_ack.ack_until = short_ack.ack_begin + clean_aot::sio::kAckPulseCycles - 1U;
    CHECK(!sio.restore(short_ack));
    auto wrong_ack_delay = pending_ack;
    wrong_ack_delay.ack_delay = clean_aot::sio::kControllerAckDelayCycles - 1U;
    CHECK(!sio.restore(wrong_ack_delay));
    auto forged_ack_start = pending_ack;
    --forged_ack_start.ack_begin;
    --forged_ack_start.ack_until;
    CHECK(!sio.restore(forged_ack_start));
    auto concurrent_shift_ack = pending_ack;
    concurrent_shift_ack.shift_active = true;
    concurrent_shift_ack.shift_bit_period = retimed_in_flight.shift_bit_period;
    concurrent_shift_ack.shift_complete = pending_ack.cycle + 1U;
    CHECK(!sio.restore(concurrent_shift_ack));
    auto impossible_parity = pending_ack;
    impossible_parity.parity_error = true;
    CHECK(!sio.restore(impossible_parity));
    auto impossible_stop = pending_ack;
    impossible_stop.bad_stop = true;
    CHECK(!sio.restore(impossible_stop));
    auto sticky_overrun = pending_ack;
    sticky_overrun.rx_overrun = true;
    sticky_overrun.rx_count = 0U;
    sticky_overrun.rx_head = 0U;
    CHECK(sio.restore(sticky_overrun));
    CHECK(sio.rx_overrun());
    CHECK(sio.restore(pending_ack));

    // The second byte may occupy the holding register while the first byte's
    // ACK is pending, but it cannot begin clocking until the pulse ends.
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x42U,
                    sio.cycle())
              .accepted);
    CHECK((sio.status() & clean_aot::sio::kStatusTxReady) == 0U);
    const Cycle ack_end = pending_ack.ack_until;
    CHECK(sio.advance_to(ack_end));
    CHECK((sio.status() & clean_aot::sio::kStatusTxReady) != 0U);
    CHECK(sio.snapshot().shift_active);
    CHECK(sio.deselect(ack_end).accepted);
}

void test_chip_select_aborts_stale_transfers() {
    PadDevice pad;
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x01U, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x42U, 0U).accepted);
    CHECK(sio.snapshot().shift_active && sio.snapshot().tx_count == 2U);

    CHECK(sio.deselect(1U).accepted);
    const auto deselected = sio.snapshot();
    CHECK(!deselected.shift_active && deselected.tx_count == 0U &&
          deselected.shift_complete == sio.cycle());
    CHECK(sio.select(Port::One, 2U).accepted);
    CHECK(sio.advance_to(10'000U));
    CHECK((sio.status() & clean_aot::sio::kStatusRxReady) == 0U);

    // A fresh address after reselect starts a fresh device transaction.
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x01U,
                    sio.cycle()).accepted);
    CHECK(sio.advance_to(sio.cycle() + sio.cycles_per_byte()));
    CHECK(sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, sio.cycle()).value == 0xFFU);
}

void test_fifo_irq_and_reset() {
    IrqLog log{};
    Sio0 sio(Sio0::Callbacks{irq_callback, &log});
    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, 0U).accepted);
    const std::uint16_t control = static_cast<std::uint16_t>(
        clean_aot::sio::kControlTxEnable | clean_aot::sio::kControlRxEnable |
        clean_aot::sio::kControlRxInterruptEnable | clean_aot::sio::kControlDtr);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword, control, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x11U, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x22U, 0U).accepted);
    const auto overwritten = sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x33U, 0U);
    CHECK(overwritten.accepted && overwritten.overwritten);
    CHECK((sio.status() & clean_aot::sio::kStatusTxReady) == 0U);
    CHECK(sio.advance_to(1088U));
    CHECK((sio.status() & clean_aot::sio::kStatusTxReady) != 0U);
    CHECK(sio.advance_to(2176U));
    const unsigned rising_before_rearm = log.rising_edges;
    const unsigned falling_before_rearm = log.falling_edges;
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(control | clean_aot::sio::kControlAcknowledge), 2176U)
              .accepted);
    CHECK(sio.irq_pending());
    CHECK(log.rising_edges == rising_before_rearm + 1U);
    CHECK(log.falling_edges == falling_before_rearm + 1U);
    CHECK(sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 2176U).accepted);
    CHECK(sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 2176U).accepted);
    CHECK(log.rising_edges >= 1U);

    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(control | clean_aot::sio::kControlAcknowledge), 2176U)
              .accepted);
    CHECK(!sio.irq_pending());
    sio.reset(4000U);
    CHECK(sio.cycle() == 4000U);
    CHECK(sio.status() == (clean_aot::sio::kStatusTxReady | clean_aot::sio::kStatusTxIdle));
    CHECK(!sio.irq_pending());

    Sio0 overflow;
    CHECK(overflow.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x000DU, 0U).accepted);
    CHECK(overflow.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, 0U).accepted);
    const std::uint16_t overflow_control = static_cast<std::uint16_t>(
        clean_aot::sio::kControlTxEnable | clean_aot::sio::kControlDtr);
    CHECK(overflow.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                         overflow_control, 0U)
              .accepted);
    Cycle overflow_cycle = 0U;
    for (std::uint8_t value = 0U; value < 9U; ++value) {
        CHECK(overflow.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, value,
                             overflow_cycle)
                  .accepted);
        overflow_cycle += overflow.cycles_per_byte();
        CHECK(overflow.advance_to(overflow_cycle));
    }
    CHECK(overflow.rx_overrun());
    CHECK(overflow.write(
                      clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                      static_cast<std::uint16_t>(overflow_control |
                                                 clean_aot::sio::kControlAcknowledge),
                      overflow_cycle)
              .accepted);
    CHECK(!overflow.rx_overrun());
}

void test_digital_and_dualshock_pad() {
    PadDevice pad;
    PadDevice second_pad;
    second_pad.set_input(PadInput{0xFFFFU, 0x55U, 0x66U, 0x77U, 0x88U});
    pad.set_input(PadInput{static_cast<std::uint16_t>(0xFFFFU ^
                                                       static_cast<std::uint16_t>(PadButton::Cross)),
                           0x10U, 0x20U, 0x30U, 0x40U});
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    CHECK(sio.attach_controller(Port::Two, second_pad));
    begin_bus(sio);
    Cycle cycle = 0U;
    CHECK(clock_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(!sio.dsr_active());
    CHECK(sio.next_event_cycle() == cycle + Sio0::kDefaultAckDelayCycles);
    const auto pending_ack_snapshot = sio.snapshot();
    const auto addressed_pad_snapshot = pad.snapshot();
    CHECK(sio.advance_to(cycle + Sio0::kDefaultAckDelayCycles - 1U));
    cycle += Sio0::kDefaultAckDelayCycles - 1U;
    CHECK(!sio.dsr_active());
    CHECK(sio.advance_to(cycle + 1U));
    cycle += 1U;
    CHECK(sio.dsr_active());
    CHECK(sio.advance_to(cycle + Sio0::kDefaultAckPulseCycles - 1U));
    cycle += Sio0::kDefaultAckPulseCycles - 1U;
    CHECK(sio.dsr_active());
    CHECK(sio.advance_to(cycle + 1U));
    ++cycle;
    CHECK(!sio.dsr_active());
    CHECK(sio.restore(pending_ack_snapshot));
    CHECK(pad.restore(addressed_pad_snapshot));
    cycle = pending_ack_snapshot.cycle;
    CHECK(sio.next_event_cycle() == cycle + Sio0::kDefaultAckDelayCycles);
    drain_device_ack(sio, cycle);
    CHECK(!sio.dsr_active());
    CHECK(exchange_byte(sio, 0x42U, cycle) == 0x41U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xBFU);
    CHECK(!sio.dsr_active());
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x43U, cycle) == 0x41U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xBFU);
    end_bus(sio, cycle);
    CHECK(pad.state().configuration_mode);

    begin_bus(sio, cycle);
    const std::array<std::uint8_t, 9> led_command = {0x01U, 0x44U, 0x00U, 0x01U, 0x03U,
                                                      0x00U, 0x00U, 0x00U, 0x00U};
    for (const std::uint8_t byte : led_command) {
        (void)exchange_byte(sio, byte, cycle);
    }
    end_bus(sio, cycle);
    CHECK(pad.state().analog_mode && pad.state().led_on && pad.state().mode_locked);

    CHECK(!pad.set_analog_mode(false));
    const auto pad_snapshot = pad.snapshot();
    CHECK(pad.restore(pad_snapshot));
    CHECK(pad.state().analog_mode);

    CHECK(sio.select(Port::Two, cycle).accepted);
    CHECK((sio.control() & clean_aot::sio::kControlPortTwo) != 0U);
    CHECK(exchange_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x42U, cycle) == 0x41U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    end_bus(sio, cycle);
}

void test_rumble_and_analog_poll() {
    PadDevice pad;
    pad.set_input(PadInput{0xFFFFU, 0x10U, 0x20U, 0x30U, 0x40U});
    Sio0 sio;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    Cycle cycle = 0U;
    const std::array<std::uint8_t, 5> enter = {0x01U, 0x43U, 0x00U, 0x01U, 0x00U};
    for (const std::uint8_t byte : enter) {
        (void)exchange_byte(sio, byte, cycle);
    }
    end_bus(sio, cycle);
    CHECK(pad.state().configuration_mode);

    begin_bus(sio, cycle);
    const std::array<std::uint8_t, 9> led = {0x01U, 0x44U, 0x00U, 0x01U, 0x00U,
                                              0x00U, 0x00U, 0x00U, 0x00U};
    for (const std::uint8_t byte : led) {
        (void)exchange_byte(sio, byte, cycle);
    }
    end_bus(sio, cycle);
    CHECK(pad.state().analog_mode);

    begin_bus(sio, cycle);
    const std::array<std::uint8_t, 9> mapping = {0x01U, 0x4DU, 0x00U, 0x00U, 0x01U,
                                                  0xFFU, 0xFFU, 0xFFU, 0xFFU};
    for (const std::uint8_t byte : mapping) {
        (void)exchange_byte(sio, byte, cycle);
    }
    end_bus(sio, cycle);
    CHECK(pad.state().rumble_configured);
    CHECK(pad.state().rumble_map[0] == 0U && pad.state().rumble_map[1] == 1U);

    begin_bus(sio, cycle);
    const std::array<std::uint8_t, 9> exit = {0x01U, 0x43U, 0x00U, 0x00U, 0x00U,
                                               0x00U, 0x00U, 0x00U, 0x00U};
    for (const std::uint8_t byte : exit) {
        (void)exchange_byte(sio, byte, cycle);
    }
    end_bus(sio, cycle);
    CHECK(!pad.state().configuration_mode);
    CHECK(pad.state().analog_mode);

    begin_bus(sio, cycle);
    const std::array<std::uint8_t, 9> poll = {0x01U, 0x42U, 0x00U, 0x01U, 0x77U,
                                               0x00U, 0x00U, 0x00U, 0x00U};
    std::array<std::uint8_t, 9> replies{};
    for (std::size_t index = 0; index < poll.size(); ++index) {
        replies[index] = exchange_byte(sio, poll[index], cycle);
    }
    end_bus(sio, cycle);
    CHECK(replies[1] == 0x73U && replies[2] == 0x5AU);
    CHECK(replies[5] == 0x10U && replies[8] == 0x40U);
    CHECK(pad.state().rumble_small == 1U && pad.state().rumble_large == 0x77U);
}

template <std::size_t Size>
std::array<ByteTransfer, Size> exchange_pad_packet(
    PadDevice& pad,
    const std::array<std::uint8_t, Size>& request,
    Cycle cycle = 0U) {
    std::array<ByteTransfer, Size> replies{};
    pad.on_select(cycle);
    for (std::size_t index = 0; index < request.size(); ++index) {
        replies[index] = pad.exchange(request[index], cycle + index);
    }
    pad.on_deselect(cycle + request.size());
    return replies;
}

void test_pad_protocol_edges() {
    PadDevice legacy_pad;
    const std::array<std::uint8_t, 5> legacy_on = {
        0x01U, 0x42U, 0x00U, 0x40U, 0x01U};
    (void)exchange_pad_packet(legacy_pad, legacy_on);
    CHECK(legacy_pad.state().rumble_small == 1U);
    const std::array<std::uint8_t, 5> invalid_legacy_x = {
        0x01U, 0x42U, 0x00U, 0x80U, 0x01U};
    (void)exchange_pad_packet(legacy_pad, invalid_legacy_x);
    CHECK(legacy_pad.state().rumble_small == 0U);
    const std::array<std::uint8_t, 5> invalid_legacy_y = {
        0x01U, 0x42U, 0x00U, 0x40U, 0x00U};
    (void)exchange_pad_packet(legacy_pad, invalid_legacy_y);
    CHECK(legacy_pad.state().rumble_small == 0U);

    PadDevice pad;
    CHECK(pad.set_analog_mode(true));
    pad.set_input(PadInput{0xA55AU, 0x10U, 0x20U, 0x30U, 0x40U});
    pad.on_select(0U);
    CHECK(pad.exchange(0x01U, 0U).acknowledged);
    CHECK(pad.exchange(0x43U, 1U).value == 0x73U);
    CHECK(pad.exchange(0x00U, 2U).value == 0x5AU);
    CHECK(pad.exchange(0x01U, 3U).value == 0x5AU);
    CHECK(!pad.state().configuration_mode);
    CHECK(pad.exchange(0x00U, 4U).value == 0xA5U);
    CHECK(!pad.state().configuration_mode);
    CHECK(pad.exchange(0x00U, 5U).value == 0x10U);
    CHECK(pad.exchange(0x00U, 6U).value == 0x20U);
    CHECK(pad.exchange(0x00U, 7U).value == 0x30U);
    const ByteTransfer enter_final = pad.exchange(0x00U, 8U);
    CHECK(enter_final.value == 0x40U && !enter_final.acknowledged);
    CHECK(pad.state().configuration_mode);
    CHECK(!pad.exchange(0x45U, 9U).acknowledged);
    pad.on_deselect(10U);

    const std::array<std::uint8_t, 9> query = {
        0x01U, 0x45U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const auto query_reply = exchange_pad_packet(pad, query);
    const std::array<std::uint8_t, 9> expected_query = {
        0xFFU, 0xF3U, 0x5AU, 0x01U, 0x02U, 0x01U, 0x02U, 0x01U, 0x00U};
    for (std::size_t index = 0; index < expected_query.size(); ++index) {
        CHECK(query_reply[index].value == expected_query[index]);
    }
    CHECK(!query_reply.back().acknowledged);

    const std::array<std::uint8_t, 9> command_48 = {
        0x01U, 0x48U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const auto command_48_reply = exchange_pad_packet(pad, command_48);
    const std::array<std::uint8_t, 9> expected_48 = {
        0xFFU, 0xF3U, 0x5AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U};
    for (std::size_t index = 0; index < expected_48.size(); ++index) {
        CHECK(command_48_reply[index].value == expected_48[index]);
    }

    const std::array<std::uint8_t, 9> lock = {
        0x01U, 0x44U, 0x00U, 0x01U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U};
    (void)exchange_pad_packet(pad, lock);
    CHECK(pad.state().mode_locked);
    CHECK(!pad.set_analog_mode(false));

    PadDevice mapped_pad;
    const std::array<std::uint8_t, 5> enter = {0x01U, 0x43U, 0x00U, 0x01U, 0x00U};
    (void)exchange_pad_packet(mapped_pad, enter);
    CHECK(mapped_pad.state().configuration_mode);
    const std::array<std::uint8_t, 9> map_late = {
        0x01U, 0x4DU, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x00U, 0x01U};
    (void)exchange_pad_packet(mapped_pad, map_late);
    const std::array<std::uint8_t, 9> exit = {
        0x01U, 0x43U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    (void)exchange_pad_packet(mapped_pad, exit);
    CHECK(!mapped_pad.state().configuration_mode);

    const std::array<std::uint8_t, 9> mapped_poll = {
        0x01U, 0x42U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x77U};
    mapped_pad.on_select(0U);
    std::array<ByteTransfer, 9> mapped_reply{};
    for (std::size_t index = 0; index < mapped_poll.size(); ++index) {
        mapped_reply[index] = mapped_pad.exchange(mapped_poll[index], index);
    }
    CHECK(mapped_reply[1].value == 0x43U);
    CHECK(mapped_reply[5].value == 0x00U && mapped_reply[8].value == 0x00U);
    CHECK(!mapped_reply.back().acknowledged);
    CHECK(mapped_pad.state().rumble_small == 1U);
    CHECK(mapped_pad.state().rumble_large == 0x77U);
    CHECK(!mapped_pad.exchange(0x42U, 10U).acknowledged);
    mapped_pad.on_deselect(11U);

    auto invalid_snapshot = mapped_pad.snapshot();
    invalid_snapshot.selected = true;
    invalid_snapshot.addressed = true;
    invalid_snapshot.command_valid = true;
    invalid_snapshot.transaction_done = false;
    invalid_snapshot.command = 0x42U;
    invalid_snapshot.phase = 0U;
    CHECK(!mapped_pad.restore(invalid_snapshot));

    const auto valid_pad_snapshot = mapped_pad.snapshot();
    auto impossible_phase = valid_pad_snapshot;
    impossible_phase.selected = true;
    impossible_phase.addressed = true;
    impossible_phase.command_valid = true;
    impossible_phase.transaction_done = false;
    impossible_phase.command = 0x42U;
    impossible_phase.phase = 7U;
    impossible_phase.state.analog_mode = false;
    impossible_phase.state.rumble_configured = false;
    impossible_phase.state.rumble_map.fill(0xFFU);
    CHECK(!mapped_pad.restore(impossible_phase));
    const auto after_impossible_phase = mapped_pad.snapshot();
    CHECK(after_impossible_phase.selected == valid_pad_snapshot.selected &&
          after_impossible_phase.addressed == valid_pad_snapshot.addressed &&
          after_impossible_phase.command == valid_pad_snapshot.command &&
          after_impossible_phase.phase == valid_pad_snapshot.phase &&
          after_impossible_phase.command_valid == valid_pad_snapshot.command_valid &&
          after_impossible_phase.transaction_done == valid_pad_snapshot.transaction_done &&
          after_impossible_phase.state.analog_mode ==
              valid_pad_snapshot.state.analog_mode &&
          after_impossible_phase.state.mode_locked ==
              valid_pad_snapshot.state.mode_locked &&
          after_impossible_phase.state.rumble_configured ==
              valid_pad_snapshot.state.rumble_configured);

    auto invalid_mapping = valid_pad_snapshot;
    invalid_mapping.state.rumble_configured = true;
    invalid_mapping.state.rumble_map[0] = 0x02U;
    CHECK(mapped_pad.restore(invalid_mapping));
    CHECK(mapped_pad.state().rumble_map[0] == 0x02U);
    for (unsigned mapping = 0x02U; mapping <= 0xFEU; ++mapping) {
        auto legal_mapping = valid_pad_snapshot;
        legal_mapping.state.rumble_configured = true;
        legal_mapping.state.rumble_map.fill(0xFFU);
        legal_mapping.state.rumble_map[0] = static_cast<std::uint8_t>(mapping);
        CHECK(mapped_pad.restore(legal_mapping));
        CHECK(mapped_pad.state().rumble_map[0] == static_cast<std::uint8_t>(mapping));
    }

    PadDevice unconfigured_pad;
    auto unconfigured_mapping = unconfigured_pad.snapshot();
    unconfigured_mapping.state.rumble_map[0] = 0x02U;
    CHECK(!unconfigured_pad.restore(unconfigured_mapping));

    PadDevice partial_mapping_pad;
    (void)exchange_pad_packet(partial_mapping_pad, enter);
    partial_mapping_pad.on_select(20U);
    CHECK(partial_mapping_pad.exchange(0x01U, 20U).acknowledged);
    CHECK(partial_mapping_pad.exchange(0x4DU, 21U).acknowledged);
    CHECK(partial_mapping_pad.exchange(0x00U, 22U).value == 0x5AU);
    CHECK(partial_mapping_pad.exchange(0x02U, 23U).value == 0xFFU);
    const auto partial_mapping_snapshot = partial_mapping_pad.snapshot();
    CHECK(partial_mapping_snapshot.command_valid &&
          partial_mapping_snapshot.command == 0x4DU &&
          partial_mapping_snapshot.phase == 3U &&
          partial_mapping_snapshot.state.rumble_map[0] == 0x02U &&
          !partial_mapping_snapshot.state.rumble_configured);
    CHECK(partial_mapping_pad.restore(partial_mapping_snapshot));
    auto forged_partial_tail = partial_mapping_snapshot;
    forged_partial_tail.state.rumble_map[5] = 0x01U;
    CHECK(!partial_mapping_pad.restore(forged_partial_tail));
    auto forged_partial_complete = partial_mapping_snapshot;
    forged_partial_complete.state.rumble_configured = true;
    CHECK(!partial_mapping_pad.restore(forged_partial_complete));
    partial_mapping_pad.on_deselect(24U);
}

void test_pad_watchdog_and_analog_button() {
    PadDevice watchdog_pad;
    const std::array<std::uint8_t, 5> enter_config = {
        0x01U, 0x43U, 0x00U, 0x01U, 0x00U};
    (void)exchange_pad_packet(watchdog_pad, enter_config, 0U);
    CHECK(watchdog_pad.state().configuration_mode);
    const auto configured = watchdog_pad.snapshot();
    CHECK(configured.watchdog_active && configured.configuration_used);
    auto forged_watchdog = configured;
    forged_watchdog.watchdog_deadline = clean_aot::sio::kNoEventCycle;
    CHECK(!watchdog_pad.restore(forged_watchdog));
    auto shortened_watchdog = configured;
    --shortened_watchdog.watchdog_deadline;
    CHECK(watchdog_pad.restore(shortened_watchdog));
    CHECK(watchdog_pad.restore(configured));
    watchdog_pad.advance_to(clean_aot::sio::kPadConfigurationWatchdogCycles - 1U);
    CHECK(watchdog_pad.state().configuration_mode);
    watchdog_pad.advance_to(configured.watchdog_deadline);
    CHECK(!watchdog_pad.state().configuration_mode);
    CHECK(!watchdog_pad.state().analog_mode && !watchdog_pad.state().led_on &&
          watchdog_pad.state().mode_locked && !watchdog_pad.state().rumble_configured);
    CHECK(!watchdog_pad.snapshot().watchdog_active);
    const std::array<std::uint8_t, 5> digital_poll = {
        0x01U, 0x42U, 0x00U, 0x00U, 0x00U};
    const auto expired_reply = exchange_pad_packet(watchdog_pad, digital_poll,
                                                    configured.watchdog_deadline + 1U);
    CHECK(expired_reply[2].value == 0x00U);

    PadDevice exit_pad;
    (void)exchange_pad_packet(exit_pad, enter_config, 40U);
    const std::array<std::uint8_t, 9> exit_config = {
        0x01U, 0x43U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const auto exit_reply = exchange_pad_packet(exit_pad, exit_config, 100U);
    CHECK(exit_reply[2].value == 0x5AU);
    const auto exited = exit_pad.snapshot();
    CHECK(!exited.state.configuration_mode && exited.watchdog_active &&
          exited.watchdog_deadline > exited.last_cycle &&
          exited.watchdog_deadline - exited.last_cycle <=
              clean_aot::sio::kPadConfigurationWatchdogCycles &&
          exited.configuration_used &&
          !exited.id_high_suppressed);
    PadDevice exit_replay;
    CHECK(exit_replay.restore(exited));
    exit_pad.advance_to(exited.watchdog_deadline - 1U);
    exit_replay.advance_to(exited.watchdog_deadline - 1U);
    CHECK(!exit_pad.state().mode_locked && !exit_replay.state().mode_locked &&
          exit_pad.snapshot().watchdog_active &&
          exit_replay.snapshot().watchdog_active);
    const auto boundary_snapshot = exit_pad.snapshot();
    PadDevice boundary_replay;
    CHECK(boundary_replay.restore(boundary_snapshot));
    exit_pad.advance_to(exited.watchdog_deadline);
    exit_replay.advance_to(exited.watchdog_deadline);
    boundary_replay.advance_to(exited.watchdog_deadline);
    const auto expired_exit = exit_pad.snapshot();
    const auto expired_exit_replay = exit_replay.snapshot();
    CHECK(exit_pad.state().mode_locked && exit_replay.state().mode_locked &&
          boundary_replay.state().mode_locked &&
          !expired_exit.watchdog_active &&
          !expired_exit_replay.watchdog_active &&
          expired_exit.state.input.buttons ==
              expired_exit_replay.state.input.buttons);

    PadDevice button_pad;
    CHECK(button_pad.set_analog_mode(false));
    (void)exchange_pad_packet(button_pad, enter_config, 100U);
    CHECK(button_pad.state().configuration_mode);
    CHECK(button_pad.press_analog_button(200U));
    CHECK(button_pad.state().analog_mode && button_pad.state().led_on &&
          button_pad.state().mode_locked && !button_pad.state().rumble_configured);
    CHECK(!button_pad.press_analog_button(201U));

    const auto poll_reply = exchange_pad_packet(button_pad, digital_poll, 300U);
    CHECK(poll_reply[1].value == 0x73U);
    CHECK(poll_reply[2].value == 0x00U);

    // The machine normally advances the bus, not the device directly. The
    // SIO scheduler must therefore expire the pad watchdog during idle time.
    PadDevice bus_pad;
    Sio0 bus_sio;
    CHECK(bus_sio.attach_controller(Port::One, bus_pad));
    begin_bus(bus_sio);
    Cycle bus_cycle = 0U;
    for (const std::uint8_t byte : enter_config) {
        (void)exchange_byte(bus_sio, byte, bus_cycle);
    }
    end_bus(bus_sio, bus_cycle);
    const auto bus_configured = bus_pad.snapshot();
    CHECK(bus_configured.watchdog_active && bus_pad.state().configuration_mode);
    CHECK(bus_sio.advance_to(bus_configured.watchdog_deadline));
    CHECK(!bus_pad.state().configuration_mode && bus_pad.state().mode_locked);
}

void check_card_header(const std::array<std::uint8_t, MemoryCard::kImageSize>& image) {
    CHECK(image[0] == 0x4DU && image[1] == 0x43U);
    std::uint8_t checksum = 0;
    for (std::size_t index = 0; index < 127U; ++index) {
        checksum = static_cast<std::uint8_t>(checksum ^ image[index]);
    }
    CHECK(checksum == image[127]);
    CHECK(image[128] == 0xA0U && image[136] == 0xFFU && image[137] == 0xFFU);
}

struct WriteLog {
    unsigned count = 0;
    clean_aot::sio::WriteReceipt last{};
    MemoryCard* card = nullptr;
    std::uint64_t callback_protocol_fingerprint = 0;
    bool callback_saw_pending_write = true;
};

void write_callback(void* opaque, const clean_aot::sio::WriteReceipt& receipt) noexcept {
    auto* log = static_cast<WriteLog*>(opaque);
    ++log->count;
    log->last = receipt;
    if (log->card != nullptr) {
        const auto card_snapshot = log->card->snapshot();
        log->callback_protocol_fingerprint = card_snapshot.protocol_fingerprint;
        log->callback_saw_pending_write = card_snapshot.pending_write_valid;
    }
}

void test_memory_card_protocol() {
    std::array<std::uint8_t, MemoryCard::kImageSize> image{};
    CHECK(MemoryCard::format_new_image(image));
    check_card_header(image);
    for (std::size_t index = 0; index < MemoryCard::kSectorSize; ++index) {
        image[3U * MemoryCard::kSectorSize + index] = static_cast<std::uint8_t>(0x80U + index);
    }
    MemoryCard card(image);
    WriteLog log{};
    log.card = &card;
    card.set_write_callback(write_callback, &log);
    Sio0 sio;
    PadDevice competing_pad;
    CHECK(sio.attach_controller(Port::One, competing_pad));
    CHECK(sio.attach_memory_card(Port::One, card));
    begin_bus(sio);
    Cycle cycle = 0U;
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x52U, cycle) == 0x08U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x03U, cycle) == 0x00U);
    const Cycle before_late_ack = cycle;
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(cycle - before_late_ack == sio.cycles_per_byte() +
                                         clean_aot::sio::kMemoryCardAckDelayCycles +
                                         clean_aot::sio::kMemoryCardReadLateAckExtraCycles +
                                         clean_aot::sio::kAckPulseCycles);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x03U);
    std::uint8_t checksum = 0x03U;
    for (std::size_t index = 0; index < MemoryCard::kSectorSize; ++index) {
        const std::uint8_t value = exchange_byte(sio, 0x00U, cycle);
        CHECK(value == static_cast<std::uint8_t>(0x80U + index));
        checksum = static_cast<std::uint8_t>(checksum ^ value);
    }
    CHECK(exchange_byte(sio, 0x00U, cycle) == checksum);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x47U);
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x53U, cycle) == 0x08U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x04U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x80U);
    end_bus(sio, cycle);

    const auto before_write = image;
    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x57U, cycle) == 0x08U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x03U, cycle) == 0x00U);
    std::array<std::uint8_t, MemoryCard::kSectorSize> replacement{};
    std::uint8_t write_checksum = 0x03U;
    for (std::size_t index = 0; index < replacement.size(); ++index) {
        replacement[index] = static_cast<std::uint8_t>(index ^ 0x5AU);
        (void)exchange_byte(sio, replacement[index], cycle);
        write_checksum = static_cast<std::uint8_t>(write_checksum ^ replacement[index]);
    }
    CHECK(log.count == 0U);
    (void)exchange_byte(sio, write_checksum, cycle);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x47U);
    CHECK(log.count == 1U && log.last.sector == 3U);
    CHECK(log.last.generation == 1U);
    CHECK(log.last.image_fingerprint == card.image_fingerprint());
    CHECK(!log.callback_saw_pending_write);
    CHECK(log.callback_protocol_fingerprint == card.snapshot().protocol_fingerprint);
    CHECK(image != before_write);
    CHECK(card.flag() == 0x00U);
    end_bus(sio, cycle);

    const auto after_good_write = image;
    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x57U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x03U, cycle) == 0x00U);
    for (const std::uint8_t byte : replacement) {
        (void)exchange_byte(sio, byte, cycle);
    }
    CHECK(log.count == 1U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == replacement.back());
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x4EU);
    CHECK(log.count == 1U);
    CHECK(image == after_good_write);
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x52U, cycle) == 0x04U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x04U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x04U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    end_bus(sio, cycle);

    const std::uint8_t flag_before_invalid = card.flag();
    card.on_select(cycle);
    CHECK(card.exchange(0x81U, cycle).acknowledged);
    const ByteTransfer invalid_command = card.exchange(0x99U, cycle);
    CHECK(invalid_command.value == flag_before_invalid);
    CHECK(!invalid_command.acknowledged);
    card.on_deselect(cycle);

    CHECK(competing_pad.set_analog_mode(true));
    const std::uint32_t generation = card.write_generation();
    const std::uint8_t card_flag = card.flag();
    sio.reset(cycle);
    CHECK(competing_pad.state().analog_mode);
    CHECK(card.write_generation() == generation);
    CHECK(card.flag() == card_flag);
}

void test_invalid_card_and_snapshot_guards() {
    std::array<std::uint8_t, 64> undersized_image{};
    MemoryCard invalid_card(undersized_image);
    CHECK(!invalid_card.valid());
    Sio0 sio;
    CHECK(!sio.attach_memory_card(Port::One, invalid_card));
    CHECK(sio.last_error() == MmioError::InvalidImage);

    std::array<std::uint8_t, MemoryCard::kImageSize> image{};
    CHECK(MemoryCard::format_new_image(image));
    MemoryCard card(image);
    card.on_select(0U);
    CHECK(card.exchange(0x81U, 0U).acknowledged);
    const std::uint8_t flag_before = card.flag();
    const ByteTransfer invalid = card.exchange(0x00U, 1U);
    CHECK(invalid.value == flag_before && !invalid.acknowledged);
    CHECK(card.flag() == flag_before);
    card.on_deselect(2U);

    card.on_select(3U);
    CHECK(card.exchange(0x81U, 3U).acknowledged);
    CHECK(card.exchange(0x52U, 4U).value == card.flag());
    CHECK(card.exchange(0x00U, 5U).value == 0x5AU);
    CHECK(card.exchange(0x00U, 6U).value == 0x5DU);
    CHECK(card.exchange(0x01U, 7U).value == 0x00U);
    CHECK(card.exchange(0x00U, 8U).value == 0x01U);
    card.on_deselect(9U);

    // A ReadData snapshot must prove that its sector is in range before a
    // later byte can index the caller-owned image.
    card.on_select(20U);
    CHECK(card.exchange(0x81U, 20U).acknowledged);
    CHECK(card.exchange(0x52U, 21U).acknowledged);
    CHECK(card.exchange(0x00U, 22U).value == 0x5AU);
    CHECK(card.exchange(0x00U, 23U).value == 0x5DU);
    CHECK(card.exchange(0x00U, 24U).value == 0x00U);
    CHECK(card.exchange(0x01U, 25U).value == 0x00U);
    CHECK(card.exchange(0x00U, 26U).value == 0x5CU);
    CHECK(card.exchange(0x00U, 27U).value == 0x5DU);
    CHECK(card.exchange(0x00U, 28U).value == 0x00U);
    CHECK(card.exchange(0x00U, 29U).value == 0x01U);
    const auto read_data_snapshot = card.snapshot();
    CHECK(read_data_snapshot.data_index == 0U && read_data_snapshot.valid_sector);
    auto forged_read_data = read_data_snapshot;
    forged_read_data.address = 0xFFFFU;
    forged_read_data.valid_sector = false;
    CHECK(!card.restore(forged_read_data));
    CHECK(card.snapshot().protocol_fingerprint == read_data_snapshot.protocol_fingerprint);
    CHECK(card.exchange(0x00U, 30U).value == image[MemoryCard::kSectorSize]);
    card.on_deselect(31U);

    card.on_select(10U);
    CHECK(card.exchange(0x81U, 10U).acknowledged);
    CHECK(card.exchange(0x57U, 11U).acknowledged);
    CHECK(card.exchange(0x00U, 12U).acknowledged);
    CHECK(card.exchange(0x00U, 13U).acknowledged);
    CHECK(card.exchange(0x00U, 14U).acknowledged);
    CHECK(card.exchange(0x01U, 15U).acknowledged);
    const auto saved = card.snapshot();
    const std::uint8_t original_header = image[0];
    image[0] ^= 0x01U;
    CHECK(!card.restore(saved));
    image[0] = original_header;
    CHECK(card.restore(saved));
    card.on_deselect(16U);
}

void test_snapshots_and_randomized_malformed_streams() {
    std::array<std::uint8_t, MemoryCard::kImageSize> image{};
    CHECK(MemoryCard::format_new_image(image));
    MemoryCard card(image);
    card.on_select(0U);
    CHECK(card.exchange(0x81U, 0U).acknowledged);
    CHECK(card.exchange(0x57U, 0U).value == 0x08U);
    CHECK(card.exchange(0x00U, 0U).value == 0x5AU);
    CHECK(card.exchange(0x00U, 0U).value == 0x5DU);
    (void)card.exchange(0x00U, 0U);
    (void)card.exchange(0x01U, 0U);
    std::array<std::uint8_t, MemoryCard::kSectorSize> payload{};
    for (std::size_t index = 0; index < 41U; ++index) {
        payload[index] = static_cast<std::uint8_t>(index * 3U);
        (void)card.exchange(payload[index], 0U);
    }
    const auto saved = card.snapshot();
    for (std::size_t index = 41U; index < payload.size(); ++index) {
        (void)card.exchange(payload[index], 0U);
    }
    (void)card.exchange(0x00U, 0U);
    CHECK(card.restore(saved));
    CHECK(card.snapshot().protocol_fingerprint == saved.protocol_fingerprint);
    card.on_deselect(0U);

    std::array<std::uint8_t, MemoryCard::kImageSize> second_image{};
    CHECK(MemoryCard::format_new_image(second_image));
    MemoryCard second(second_image);
    for (std::uint32_t seed = 1U; seed <= 512U; ++seed) {
        std::uint32_t random = seed * 747796405U + 2891336453U;
        card.on_select(seed);
        second.on_select(seed);
        for (unsigned chunk = 0U; chunk < 32U; ++chunk) {
            random = random * 1664525U + 1013904223U;
            const std::uint8_t value = static_cast<std::uint8_t>(random >> 24U);
            const ByteTransfer first = card.exchange(value, seed);
            const ByteTransfer other = second.exchange(value, seed);
            CHECK(first.value == other.value && first.acknowledged == other.acknowledged);
        }
        CHECK(card.snapshot().protocol_fingerprint == second.snapshot().protocol_fingerprint);
        card.on_deselect(seed);
        second.on_deselect(seed);
    }

    Sio0 sio;
    PadDevice pad;
    CHECK(sio.attach_controller(Port::One, pad));
    begin_bus(sio);
    Cycle cycle = 0U;
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x01U, cycle).accepted);
    CHECK(sio.advance_to(100U));
    const auto sio_saved = sio.snapshot();
    const auto pad_saved = pad.snapshot();
    CHECK(sio.advance_to(sio.cycles_per_byte()));
    CHECK(sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, sio.cycles_per_byte()).accepted);
    CHECK(sio.restore(sio_saved));
    CHECK(pad.restore(pad_saved));
    CHECK(sio.advance_to(sio.cycles_per_byte()));
    const auto replayed_address = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte,
                                           sio.cycles_per_byte());
    CHECK(replayed_address.accepted && replayed_address.value == 0xFFU);
    cycle = sio.cycles_per_byte();
    drain_device_ack(sio, cycle);
    CHECK(sio.deselect(cycle).accepted);

    const auto first = sio.snapshot();
    CHECK(sio.restore(first));
    CHECK((sio.status() & (clean_aot::sio::kStatusTxReady | clean_aot::sio::kStatusTxIdle)) ==
          (clean_aot::sio::kStatusTxReady | clean_aot::sio::kStatusTxIdle));
    CHECK(sio.irq_pending());
    const Cycle current = sio.cycle();
    CHECK(sio.advance_to(current + 1U));
    CHECK(!sio.advance_to(current));
    CHECK(sio.last_error() == MmioError::TimeWentBackwards);
}

void test_generation_safe_device_hotplug() {
    std::array<std::uint8_t, MemoryCard::kImageSize> image_one{};
    std::array<std::uint8_t, MemoryCard::kImageSize> image_two{};
    std::array<std::uint8_t, MemoryCard::kImageSize> replacement_image{};
    CHECK(MemoryCard::format_new_image(image_one));
    CHECK(MemoryCard::format_new_image(image_two));
    CHECK(MemoryCard::format_new_image(replacement_image));
    MemoryCard card_one(image_one), card_two(image_two), replacement(replacement_image);
    PadDevice pad_one, pad_two, replacement_pad;
    Sio0 sio;
    CHECK(sio.attach_memory_card(Port::One, card_one));
    CHECK(sio.attach_memory_card(Port::Two, card_two));
    CHECK(sio.attach_controller(Port::One, pad_one));
    CHECK(sio.attach_controller(Port::Two, pad_two));

    begin_bus(sio);
    Cycle cycle = 0U;
    CHECK(clock_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0x52U, cycle).accepted);
    const auto pending_removal = sio.snapshot();
    const auto pending_card = card_one.snapshot();
    CHECK(pending_removal.ack_pending && pending_removal.tx_count == 1U);
    CHECK(sio.advance_to(pending_removal.ack_begin - 1U));
    cycle = pending_removal.ack_begin - 1U;
    CHECK(sio.detach_memory_card(Port::One, card_one));
    CHECK(!sio.dsr_active() && !sio.irq_pending());
    CHECK(sio.next_event_cycle() == cycle + sio.cycles_per_byte());
    cycle += sio.cycles_per_byte();
    CHECK(sio.advance_to(cycle));
    const auto removed_reply = sio.read(clean_aot::sio::kDataOffset,
                                        MmioWidth::Byte, cycle);
    CHECK(removed_reply.accepted && removed_reply.value == 0xFFU);
    CHECK(sio.next_event_cycle() == clean_aot::sio::kNoEventCycle);
    end_bus(sio, cycle);

    CHECK(!sio.detach_memory_card(Port::One, card_two));
    CHECK(sio.last_error() == MmioError::NoDevice);
    CHECK(!sio.detach_memory_card(Port::One, card_one));
    CHECK(sio.last_error() == MmioError::NoDevice);
    CHECK(sio.detach_controller(Port::One, pad_one));

    CHECK(sio.attach_memory_card(Port::One, card_one));
    CHECK(sio.restore(pending_removal));
    CHECK(card_one.restore(pending_card));
    cycle = pending_removal.cycle;
    CHECK(sio.advance_to(pending_removal.ack_begin));
    cycle = pending_removal.ack_begin;
    CHECK(sio.dsr_active() && sio.irq_pending());
    CHECK(sio.detach_memory_card(Port::One, card_one));
    CHECK(!sio.dsr_active() && sio.irq_pending());
    CHECK(sio.next_event_cycle() == cycle + sio.cycles_per_byte());
    cycle += sio.cycles_per_byte();
    CHECK(sio.advance_to(cycle));
    const auto asserted_removal_reply = sio.read(
        clean_aot::sio::kDataOffset, MmioWidth::Byte, cycle);
    CHECK(asserted_removal_reply.accepted &&
          asserted_removal_reply.value == 0xFFU);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(sio.control() |
                                               clean_aot::sio::kControlAcknowledge),
                    cycle).accepted);
    CHECK(!sio.irq_pending());
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(clock_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(sio.next_event_cycle() == clean_aot::sio::kNoEventCycle);
    end_bus(sio, cycle);

    CHECK(sio.attach_memory_card(Port::One, replacement));
    CHECK(sio.attach_controller(Port::One, replacement_pad));
    begin_bus(sio, cycle);
    CHECK(clock_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(sio.next_event_cycle() == cycle + clean_aot::sio::kMemoryCardAckDelayCycles);
    drain_device_ack(sio, cycle);
    CHECK(sio.select(Port::Two, cycle).accepted);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    end_bus(sio, cycle);

    CHECK(sio.detach_memory_card(Port::One, replacement));
    CHECK(sio.detach_memory_card(Port::Two, card_two));
    CHECK(sio.detach_controller(Port::One, replacement_pad));
    CHECK(sio.detach_controller(Port::Two, pad_two));
}

} // namespace

int main() {
    test_registers_and_timing();
    test_wide_data_register_access();
    test_tx_holding_register_and_irq_restore();
    test_address_ack_boundary_and_cycle_bounds();
    test_sio_snapshot_timing_and_error_guards();
    test_chip_select_aborts_stale_transfers();
    test_fifo_irq_and_reset();
    test_digital_and_dualshock_pad();
    test_rumble_and_analog_poll();
    test_pad_protocol_edges();
    test_pad_watchdog_and_analog_button();
    test_memory_card_protocol();
    test_invalid_card_and_snapshot_guards();
    test_snapshots_and_randomized_malformed_streams();
    test_generation_safe_device_hotplug();
    std::puts("clean_aot_sio_tests: 15 suites passed");
    return 0;
}
