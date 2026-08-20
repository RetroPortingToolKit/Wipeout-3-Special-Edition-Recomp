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

std::uint8_t exchange_byte(Sio0& sio, std::uint8_t value, Cycle& cycle) {
    const auto write = sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, value, cycle);
    CHECK(write.accepted);
    cycle += sio.cycles_per_byte();
    const auto read = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, cycle);
    CHECK(read.accepted);
    return static_cast<std::uint8_t>(read.value);
}

void begin_bus(Sio0& sio, Cycle cycle = 0U) {
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

    CHECK(!sio.read(clean_aot::sio::kDataOffset, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnsupportedWidth);
    CHECK(!sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnsupportedWidth);
    CHECK(!sio.read(clean_aot::sio::kModeOffset + 1U, MmioWidth::Halfword, 0U).accepted);
    CHECK(sio.last_error() == MmioError::Misaligned);
    CHECK(!sio.read(0x20U, MmioWidth::Byte, 0U).accepted);
    CHECK(sio.last_error() == MmioError::UnknownRegister);

    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x0002U, 0U).accepted);
    CHECK(sio.write(clean_aot::sio::kBaudOffset, MmioWidth::Halfword, 0x0088U, 0U).accepted);
    CHECK(sio.cycles_per_bit() == 2176U);
    CHECK(sio.cycles_per_byte() == 17408U);

    CHECK(sio.write(clean_aot::sio::kModeOffset, MmioWidth::Halfword, 0x0000U, 0U).accepted);
    CHECK(sio.cycles_per_bit() == 136U);
    CHECK(sio.cycles_per_byte() == 1088U);
    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(clean_aot::sio::kControlTxEnable |
                                                clean_aot::sio::kControlRxEnable), 0U)
              .accepted);
    CHECK(sio.write(clean_aot::sio::kDataOffset, MmioWidth::Byte, 0xA5U, 0U).accepted);
    const auto in_flight = sio.read(clean_aot::sio::kStatusOffset, MmioWidth::Word, 100U);
    CHECK(in_flight.accepted);
    CHECK((in_flight.value & 0x00000400U) == 0U);
    CHECK(((in_flight.value >> 11U) & 0x001FFFFFU) == 988U);
    CHECK(!sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 100U).accepted);
    CHECK(sio.last_error() == MmioError::ReceiveEmpty);
    CHECK(sio.advance_to(1088U));
    const auto received = sio.read(clean_aot::sio::kDataOffset, MmioWidth::Byte, 1088U);
    CHECK(received.accepted && received.value == 0xFFU);

    CHECK(sio.write(clean_aot::sio::kControlOffset, MmioWidth::Halfword,
                    static_cast<std::uint16_t>(0x8000U), 1088U)
              .accepted == false);
    CHECK(sio.last_error() == MmioError::UnsupportedValue);
    CHECK(log.rising_edges == 0U);
}

void test_fifo_irq_and_reset() {
    IrqLog log{};
    Sio0 sio(Sio0::Callbacks{irq_callback, &log});
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
    CHECK(exchange_byte(sio, 0x01U, cycle) == 0xFFU);
    CHECK(sio.dsr_active());
    CHECK(sio.advance_to(cycle + Sio0::kDefaultAckPulseCycles - 1U));
    cycle += Sio0::kDefaultAckPulseCycles - 1U;
    CHECK(sio.dsr_active());
    CHECK(sio.advance_to(cycle + 1U));
    cycle += 1U;
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
};

void write_callback(void* opaque, const clean_aot::sio::WriteReceipt& receipt) noexcept {
    auto* log = static_cast<WriteLog*>(opaque);
    ++log->count;
    log->last = receipt;
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
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
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
    CHECK(image != before_write);
    CHECK(card.flag() == 0x00U);
    end_bus(sio, cycle);

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
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x4EU);
    CHECK(log.count == 1U);
    end_bus(sio, cycle);

    begin_bus(sio, cycle);
    CHECK(exchange_byte(sio, 0x81U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x52U, cycle) == 0x04U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5AU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x04U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x00U);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5CU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0x5DU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    CHECK(exchange_byte(sio, 0x00U, cycle) == 0xFFU);
    end_bus(sio, cycle);
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
    CHECK(sio.deselect(sio.cycles_per_byte()).accepted);

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

} // namespace

int main() {
    test_registers_and_timing();
    test_fifo_irq_and_reset();
    test_digital_and_dualshock_pad();
    test_rumble_and_analog_poll();
    test_memory_card_protocol();
    test_snapshots_and_randomized_malformed_streams();
    std::puts("clean_aot_sio_tests: 6 suites passed");
    return 0;
}
